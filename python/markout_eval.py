"""
OB+vol 做市可行性评估 — 精简版 markout 分析

核心问题: 在 Kraken 上用 OB mid ± half_spread 挂 maker 单, 成交后的 markout 是正还是负?

输入: Kraken 某 forex pair 的 book snapshot + trade 数据
输出: 多窗口 markout 统计表 + 结论

用法:
  python markout_eval.py --book eur_usd_book.parquet --trade eur_usd_trade.csv.gz \
      --half_spread 5 --output markout_eurusd.csv
"""

import argparse
import os
import re
import sys
from dataclasses import dataclass

import numpy as np
import pandas as pd


# ═══════════════════════════════════════════════════════════════
# 数据加载
# ═══════════════════════════════════════════════════════════════

def load_book(path: str) -> pd.DataFrame:
    """加载 order book snapshot。自动检测 parquet / csv.gz / csv。"""
    # 先尝试 parquet (有些文件扩展名是 .csv.gz 但实际是 parquet)
    try:
        df = pd.read_parquet(path)
    except Exception:
        if path.endswith(".gz"):
            df = pd.read_csv(path, compression="gzip")
        else:
            df = pd.read_csv(path)

    # 统一时间戳列名, 转 ns int64
    ts_col = None
    for c in ["local_timestamp", "timestamp", "ts"]:
        if c in df.columns:
            ts_col = c
            break
    if ts_col is None:
        raise ValueError(f"找不到时间戳列, columns: {list(df.columns)}")
    df["ts_ns"] = _to_ns(df[ts_col])

    # 提取 book 列（bids[0].price / bids[i].price 两种格式）
    bid_p, bid_q, ask_p, ask_q = _extract_book_cols(df)
    if len(bid_p) == 0:
        raise ValueError("无法匹配 book 列")

    df["bid0"] = df[bid_p[0]]
    df["bid_q0"] = df[bid_q[0]]
    df["ask0"] = df[ask_p[0]]
    df["ask_q0"] = df[ask_q[0]]

    # 额外保留前 n_levels 档用于多层填充计算
    n_avail = min(len(bid_p), 25)
    cols = {}
    for i in range(n_avail):
        cols[f"bid_{i}"] = df[bid_p[i]]
        cols[f"bid_q_{i}"] = df[bid_q[i]]
        cols[f"ask_{i}"] = df[ask_p[i]]
        cols[f"ask_q_{i}"] = df[ask_q[i]]
    df = pd.concat([df, pd.DataFrame(cols, index=df.index)], axis=1)

    df = df.sort_values("ts_ns").reset_index(drop=True)
    return df


def load_trades(path: str) -> pd.DataFrame:
    """加载 trade 数据。支持 csv.gz / parquet。"""
    ext = os.path.splitext(path)[1]
    if ext == ".gz":
        df = pd.read_csv(path, compression="gzip")
    elif path.endswith(".parquet"):
        df = pd.read_parquet(path)
    else:
        df = pd.read_csv(path)

    # 时间戳
    ts_col = None
    for c in ["local_timestamp", "timestamp", "ts"]:
        if c in df.columns:
            ts_col = c
            break
    if ts_col is None:
        raise ValueError(f"找不到时间戳列, columns: {list(df.columns)}")
    df["ts_ns"] = _to_ns(df[ts_col])

    # side 列
    side_col = None
    for c in ["side", "aggressor_side", "taker_side"]:
        if c in df.columns:
            side_col = c
            break
    if side_col is None:
        raise ValueError(f"找不到 side 列, columns: {list(df.columns)}")
    df["side_int"] = _parse_side(df[side_col])

    # price / qty
    px_col = "price" if "price" in df.columns else "px"
    qty_col = None
    for c in ["amount", "qty", "quantity", "volume"]:
        if c in df.columns:
            qty_col = c
            break
    if qty_col is None:
        raise ValueError(f"找不到成交量列")

    df["trade_px"] = df[px_col].astype(float)
    df["trade_qty"] = df[qty_col].astype(float)
    df = df.sort_values("ts_ns").reset_index(drop=True)
    return df


# ═══════════════════════════════════════════════════════════════
# 核心算法: 模拟 maker 成交 + markout 计算
# ═══════════════════════════════════════════════════════════════

def eval_markout(
    df_book: pd.DataFrame,
    df_trade: pd.DataFrame,
    half_spread_bps: float,
    taus_sec=(0.1, 0.5, 1.0, 5.0, 10.0, 30.0, 60.0, 300.0),
    order_size_usd: float = 100.0,
    n_levels: int = 5,
) -> pd.DataFrame:
    """
    模拟 OB+vol 做市: 以 OB mid ± half_spread 挂单, 计算 markout。

    Args:
        df_book: book snapshot, 需含 ts_ns, bid0, bid_q0, ask0, ask_q0, bid_i, ask_i, bid_q_i, ask_q_i
        df_trade: trade 数据, 需含 ts_ns, side_int, trade_px, trade_qty
        half_spread_bps: 挂单半价差 (bps)
        taus_sec: markout 窗口 (秒)
        order_size_usd: 每笔订单 USD 价值
        n_levels: 用于填充模拟的 book 深度层数

    Returns:
        DataFrame: 每行 = (maker_side, tau_sec) 的 markout 统计
    """
    bk_ts = df_book["ts_ns"].to_numpy(np.int64)
    bid_p_list = [df_book[f"bid_{i}"].to_numpy(np.float64) for i in range(n_levels)]
    bid_q_list = [df_book[f"bid_q_{i}"].to_numpy(np.float64) for i in range(n_levels)]
    ask_p_list = [df_book[f"ask_{i}"].to_numpy(np.float64) for i in range(n_levels)]
    ask_q_list = [df_book[f"ask_q_{i}"].to_numpy(np.float64) for i in range(n_levels)]

    # mid / micro
    bid0, ask0 = bid_p_list[0], ask_p_list[0]
    bidq0, askq0 = bid_q_list[0], ask_q_list[0]
    mid0 = 0.5 * (bid0 + ask0)
    den0 = bidq0 + askq0
    micro0 = np.where(den0 > 0, (bid0 * askq0 + ask0 * bidq0) / den0, mid0)

    # 做市报价 (OB mid ± half_spread, 替代 FP)
    spread_frac = half_spread_bps * 1e-4
    fp_bid = mid0 * (1.0 - spread_frac)
    fp_ask = mid0 * (1.0 + spread_frac)

    # ——— trades → book period 聚合 ———
    t_ts = df_trade["ts_ns"].to_numpy(np.int64)
    t_side = df_trade["side_int"].to_numpy(np.int8)
    t_qty = df_trade["trade_qty"].to_numpy(np.float64)
    t_px = df_trade["trade_px"].to_numpy(np.float64)

    # 每个 trade 归属到最近的前一个 book snapshot
    t_bk_idx = _asof_idx(bk_ts, t_ts)
    ok = t_bk_idx >= 0
    t_bk_idx = t_bk_idx[ok]
    t_side = t_side[ok]
    t_qty = t_qty[ok]
    t_px_v = t_px[ok]

    n_bk = len(bk_ts)

    # 每个 book snapshot 期间买入/卖出量 (用 taker side 指示 trade 方向)
    Q_buy = np.bincount(t_bk_idx, weights=t_qty * (t_side == 1), minlength=n_bk).astype(np.float64)
    Q_sell = np.bincount(t_bk_idx, weights=t_qty * (t_side == -1), minlength=n_bk).astype(np.float64)

    active = (mid0 > 0) & ((Q_buy > 0) | (Q_sell > 0))
    act_idx = np.flatnonzero(active)
    if act_idx.size == 0:
        print("⚠ 无有效 book-trade 匹配记录, 请检查数据的时间范围是否重合")
        return pd.DataFrame()

    # ——— 逐 book snapshot 模拟 fill ———
    results = []  # (maker_side, tau_sec, drift_mid, cap_mid, pnl_mid, notional)

    block = 100_000
    taus_ns = [int(round(t * 1e9)) for t in taus_sec]

    for s0 in range(0, act_idx.size, block):
        s1 = min(act_idx.size, s0 + block)
        idx = act_idx[s0:s1]
        m = len(idx)

        t0 = bk_ts[idx]
        fp_b = fp_bid[idx]
        fp_a = fp_ask[idx]
        m0 = mid0[idx]
        u0 = micro0[idx]
        Qb = Q_buy[idx]
        Qs = Q_sell[idx]

        # ——— ASK maker (taker buy, 我的卖单被吃) ———
        mask_ask = Qb > 0
        if np.any(mask_ask):
            i_ask = np.flatnonzero(mask_ask)
            Q_i = Qb[i_ask]
            t0_i = t0[i_ask]

            # 沿 ask book 填充
            for level in range(n_levels):
                ask_p_l = ask_p_list[level][idx][i_ask]
                ask_q_l = ask_q_list[level][idx][i_ask]
                cum_prev = np.zeros(len(i_ask))
                for prev_l in range(level):
                    cum_prev += ask_q_list[prev_l][idx][i_ask]
                rem = np.maximum(0.0, Q_i - cum_prev)
                fill = np.minimum(ask_q_l, rem)
                w = fill * fp_a[i_ask]  # notional weight
                valid = (w > 0) & (ask_p_l > 0) & np.isfinite(ask_p_l)
                if not np.any(valid):
                    continue

                i_v = np.flatnonzero(valid)
                P = ask_p_l[i_v]
                # 只统计价格 >= 我们报价的成交 (挂 ask 在 fp_ask, 被 ≥fp_ask 的 taker buy 吃掉)
                above_quote = P >= fp_a[i_ask][i_v]
                if not np.any(above_quote):
                    continue
                i_v = i_v[above_quote]
                P = P[above_quote]
                cap_mid = (P - m0[i_ask][i_v]) / P  # 吃到的 spread

                for j, tau_ns in enumerate(taus_ns):
                    t1 = t0_i[i_v] + tau_ns
                    idx_mid1 = _asof_idx(bk_ts, t1)
                    ok1 = idx_mid1 >= 0
                    if not np.any(ok1):
                        continue
                    mid1 = np.full(len(i_v), np.nan)
                    mid1[ok1] = mid0[idx_mid1[ok1]]
                    drift_mid = (mid1 - m0[i_ask][i_v]) / P  # ASK: mid涨→drift正→不利
                    pnl = cap_mid - drift_mid

                    ok_fin = ok1 & np.isfinite(drift_mid) & np.isfinite(pnl)
                    w_ok = w[i_v][ok_fin]
                    results.append({
                        "maker_side": "ASK",
                        "tau_sec": taus_sec[j],
                        "drift_mid_bps": np.average(drift_mid[ok_fin] * 1e4, weights=w_ok),
                        "cap_mid_bps": np.average(cap_mid[ok_fin] * 1e4, weights=w_ok),
                        "pnl_mid_bps": np.average(pnl[ok_fin] * 1e4, weights=w_ok),
                        "fill_count": int(np.sum(ok_fin)),
                        "notional": float(np.sum(w_ok)),
                    })

        # ——— BID maker (taker sell, 我的买单被吃) ———
        mask_bid = Qs > 0
        if np.any(mask_bid):
            i_bid = np.flatnonzero(mask_bid)
            Q_i = Qs[i_bid]
            t0_i = t0[i_bid]

            for level in range(n_levels):
                bid_p_l = bid_p_list[level][idx][i_bid]
                bid_q_l = bid_q_list[level][idx][i_bid]
                cum_prev = np.zeros(len(i_bid))
                for prev_l in range(level):
                    cum_prev += bid_q_list[prev_l][idx][i_bid]
                rem = np.maximum(0.0, Q_i - cum_prev)
                fill = np.minimum(bid_q_l, rem)
                w = fill * fp_b[i_bid]
                valid = (w > 0) & (bid_p_l > 0) & np.isfinite(bid_p_l)
                if not np.any(valid):
                    continue

                i_v = np.flatnonzero(valid)
                P = bid_p_l[i_v]
                # 只统计价格 <= 我们报价的成交 (挂 bid 在 fp_bid, 被 ≤fp_bid 的 taker sell 吃掉)
                below_quote = P <= fp_b[i_bid][i_v]
                if not np.any(below_quote):
                    continue
                i_v = i_v[below_quote]
                P = P[below_quote]
                cap_mid = -(P - m0[i_bid][i_v]) / P  # 吃到的 spread

                for j, tau_ns in enumerate(taus_ns):
                    t1 = t0_i[i_v] + tau_ns
                    idx_mid1 = _asof_idx(bk_ts, t1)
                    ok1 = idx_mid1 >= 0
                    if not np.any(ok1):
                        continue
                    mid1 = np.full(len(i_v), np.nan)
                    mid1[ok1] = mid0[idx_mid1[ok1]]
                    drift_mid = -(mid1 - m0[i_bid][i_v]) / P  # BID: mid跌→drift正→不利
                    pnl = cap_mid - drift_mid

                    ok_fin = ok1 & np.isfinite(drift_mid) & np.isfinite(pnl)
                    w_ok = w[i_v][ok_fin]
                    results.append({
                        "maker_side": "BID",
                        "tau_sec": taus_sec[j],
                        "drift_mid_bps": np.average(drift_mid[ok_fin] * 1e4, weights=w_ok),
                        "cap_mid_bps": np.average(cap_mid[ok_fin] * 1e4, weights=w_ok),
                        "pnl_mid_bps": np.average(pnl[ok_fin] * 1e4, weights=w_ok),
                        "fill_count": int(np.sum(ok_fin)),
                        "notional": float(np.sum(w_ok)),
                    })

    if not results:
        return pd.DataFrame()
    return pd.DataFrame(results)


# ═══════════════════════════════════════════════════════════════
# 输出与可视化
# ═══════════════════════════════════════════════════════════════

def print_summary(df: pd.DataFrame, half_spread_bps: float):
    """打印可读的 markout 总结"""
    if df.empty:
        print("无有效数据")
        return

    print(f"\n{'='*70}")
    print(f"  OB+Vol Markout 评估 — half_spread = {half_spread_bps} bps")
    print(f"{'='*70}")

    for tau in sorted(df["tau_sec"].unique()):
        sub = df[df["tau_sec"] == tau]
        bid = sub[sub["maker_side"] == "BID"]
        ask = sub[sub["maker_side"] == "ASK"]

        b_pnl = _wmean(bid, "pnl_mid_bps") if len(bid) else float("nan")
        a_pnl = _wmean(ask, "pnl_mid_bps") if len(ask) else float("nan")
        b_drift = _wmean(bid, "drift_mid_bps") if len(bid) else float("nan")
        a_drift = _wmean(ask, "drift_mid_bps") if len(ask) else float("nan")
        b_cap = _wmean(bid, "cap_mid_bps") if len(bid) else float("nan")
        a_cap = _wmean(ask, "cap_mid_bps") if len(ask) else float("nan")
        n_bid = int(bid["fill_count"].sum()) if len(bid) else 0
        n_ask = int(ask["fill_count"].sum()) if len(ask) else 0
        combined = (b_pnl * n_bid + a_pnl * n_ask) / (n_bid + n_ask) if (n_bid + n_ask) > 0 else float("nan")

        print(f"  tau={tau:6.1f}s  |  bid: cap={b_cap:+.2f} drift={b_drift:+.2f} pnl={b_pnl:+.2f} (n={n_bid})"
              f"  |  ask: cap={a_cap:+.2f} drift={a_drift:+.2f} pnl={a_pnl:+.2f} (n={n_ask})"
              f"  |  combined pnl={combined:+.2f} bps")

    print(f"\n  解读:")
    print(f"    pnl = cap - drift  (>0 赚钱, <0 亏钱)")
    print(f"    cap  = 成交时吃到的 spread")
    print(f"    drift = 成交后价格往不利方向移动的幅度")
    print(f"    如果 pnl 在所有窗口 > 0 → OB+vol 做市可行")
    print(f"    如果 pnl 在短窗口 > 0 但长窗口 < 0 → 需要快速平仓但 taker fee 不允许")
    print(f"    如果 pnl 在所有窗口 < 0 → 纯 OB+vol 做市不可行, 需要额外信号")


# ═══════════════════════════════════════════════════════════════
# 工具函数
# ═══════════════════════════════════════════════════════════════

def _to_ns(series: pd.Series) -> np.ndarray:
    """将时间戳转为 int64 ns。自动检测单位 (ms/us/ns)。"""
    arr = series.dropna().astype(np.int64).to_numpy()
    if len(arr) == 0:
        return arr
    sample = abs(int(arr[0]))
    digits = len(str(sample))
    if digits <= 13:
        return arr * 1_000_000  # ms → ns
    elif digits <= 16:
        return arr * 1_000  # us → ns
    return arr


def _extract_book_cols(df: pd.DataFrame):
    """提取 book 列名: bids[0].price / bid0.price 两种格式"""
    cols = df.columns.tolist()

    def pick(pattern):
        out = []
        for c in cols:
            m = re.fullmatch(pattern, c)
            if m:
                out.append((int(m.group(1)), c))
        out.sort(key=lambda x: x[0])
        return [c for _, c in out]

    # 格式1: bids[0].price, bids[0].amount
    bid_p = pick(r"bids?\[(\d+)\]\.price")
    bid_q = pick(r"bids?\[(\d+)\]\.amount")
    ask_p = pick(r"asks?\[(\d+)\]\.price")
    ask_q = pick(r"asks?\[(\d+)\]\.amount")

    # 格式2: askN.price / askN.amount (book_snapshot_25 格式)
    if not bid_p:
        bid_p = pick(r"bid(\d+)\.price")
        bid_q = pick(r"bid(\d+)\.amount")
        ask_p = pick(r"ask(\d+)\.price")
        ask_q = pick(r"ask(\d+)\.amount")

    # 格式3: bid_0, bid_q0 等 (已处理的中间格式)
    if not bid_p:
        bid_p = pick(r"bid_?(\d+)$")
        bid_q = pick(r"bid_?q_?(\d+)$")
        ask_p = pick(r"ask_?(\d+)$")
        ask_q = pick(r"ask_?q_?(\d+)$")

    return bid_p, bid_q, ask_p, ask_q


def _parse_side(series: pd.Series) -> np.ndarray:
    """解析 side 列 → int8 (1=buy, -1=sell)"""
    s = series.astype(str).str.strip().str.lower().to_numpy()
    return np.where(
        s == "buy", 1,
        np.where(s == "sell", -1,
                 np.where(s == "1", 1,
                          np.where(s == "-1", -1, 0)))
    ).astype(np.int8)


def _asof_idx(sorted_ts: np.ndarray, query_ts: np.ndarray) -> np.ndarray:
    """每个 query_ts 找到 ≤ 它 的最大 sorted_ts 索引。超出返回 -1。"""
    idx = np.searchsorted(sorted_ts, query_ts, side="right") - 1
    if len(sorted_ts) > 0 and len(query_ts) > 0:
        idx[(query_ts < sorted_ts[0]) | (query_ts > sorted_ts[-1])] = -1
    return idx


def _wmean(df_sub: pd.DataFrame, col: str) -> float:
    """加权平均 (按 notional 权重)"""
    if df_sub.empty or df_sub["notional"].sum() <= 0:
        return float("nan")
    return float(np.average(df_sub[col], weights=df_sub["notional"]))


# ═══════════════════════════════════════════════════════════════
# 主入口
# ═══════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description="OB+vol 做市 markout 评估")
    parser.add_argument("--book", required=True, help="Kraken book snapshot 文件 (parquet/csv.gz)")
    parser.add_argument("--trade", required=True, help="Kraken trade 文件 (csv.gz/parquet)")
    parser.add_argument("--half_spread", type=float, default=5.0,
                        help="做市半价差 (bps), 默认 5.0")
    parser.add_argument("--output", default="", help="输出 CSV 路径 (可选)")
    parser.add_argument("--n_levels", type=int, default=5, help="book 深度层数, 默认 5")
    args = parser.parse_args()

    print(f"加载 book: {args.book}")
    df_book = load_book(args.book)
    print(f"  {len(df_book)} 行, 时间范围: {df_book['ts_ns'].min()} ~ {df_book['ts_ns'].max()}")

    print(f"加载 trade: {args.trade}")
    df_trade = load_trades(args.trade)
    print(f"  {len(df_trade)} 行, buy={int((df_trade.side_int==1).sum())} sell={int((df_trade.side_int==-1).sum())}")

    print(f"\n计算 markout (half_spread={args.half_spread}bps, {args.n_levels} levels)...")
    result = eval_markout(df_book, df_trade, args.half_spread,
                          n_levels=args.n_levels)

    print_summary(result, args.half_spread)

    if args.output and not result.empty:
        result.to_csv(args.output, index=False)
        print(f"\n结果已保存: {args.output}")


if __name__ == "__main__":
    main()
