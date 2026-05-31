#!/usr/bin/env python3
"""
层次2验证: 旧排序(pnl×fills) vs 新排序(pnl×EFU)
=====================================================
模拟每个book snapshot下两种方法选档→模拟成交→对比markout PnL

输出:
  - 每档位的平均fill率、平均markout
  - 新旧方法top-3的总markout对比
"""

from __future__ import annotations
import os, sys, argparse
import numpy as np
import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from markout_eval import load_book, load_trades
from fill_hazard_analysis import (
    compute_taker_reach_events, estimate_hazard_curve,
    DEPTH_GRID, N_LEVELS, find_files
)

DATA_ROOT = "/data/kraken"
TAUS = [0.1, 0.5, 1.0, 5.0, 10.0, 30.0, 60.0]
K_ORDER_USD = 50.0
T_HORIZON = 60.0
MAX_PER_SIDE = 3

# ═══════════════════════════════════════════════════════════
# 加载 EFU 参数
# ═══════════════════════════════════════════════════════════

def load_efu_model(pair_name: str):
    """从 prob_params CSV 加载分段指数模型，返回 (seg_bounds, ten_b_list, k_list)"""
    prob_dir = "config/prob_params/kraken"
    buy_f = os.path.join(prob_dir, f"{pair_name}_buy.csv")
    sell_f = os.path.join(prob_dir, f"{pair_name}_sell.csv")

    def _load(path):
        if not os.path.exists(path):
            return None
        df = pd.read_csv(path, header=None)
        ends = df.iloc[:, 2].values.astype(float)
        ten_bs = df.iloc[:, 3].values.astype(float)
        ks = df.iloc[:, 4].values.astype(float)
        return ends, ten_bs, ks

    ask_model = _load(buy_f)   # _buy.csv = ASK fill
    bid_model = _load(sell_f)  # _sell.csv = BID fill
    return ask_model, bid_model


def expected_fill_usd(cum_depth: float, order_usd: float,
                       ends: np.ndarray, ten_bs: np.ndarray,
                       ks: np.ndarray, T_sec: float) -> float:
    """分段指数 EFU (线性近似, 与 C++ TailFillModel 一致)"""
    lo = cum_depth
    hi = cum_depth + order_usd
    total = 0.0

    for j in range(len(ends)):
        seg_lo = ends[j - 1] if j > 0 else 0.0
        seg_hi = ends[j]
        a = max(lo, seg_lo)
        b = min(hi, seg_hi)
        if b <= a:
            continue
        tb = ten_bs[j]
        k = ks[j]
        if abs(k) < 1e-12:
            total += tb * (b - a)
        else:
            total += tb * (np.exp(k * b) - np.exp(k * a)) / k
    return total * (T_sec / 86400.0)


# ═══════════════════════════════════════════════════════════
# 核心: 模拟两种排名在单个book snapshot的选档
# ═══════════════════════════════════════════════════════════

def simulate_ranking(
    bk: pd.DataFrame,
    tr: pd.DataFrame,
    drift_df: pd.DataFrame,
    ask_model, bid_model,
):
    """
    对每个book snapshot模拟选档→成交→markout。
    返回两种方法的结果DataFrame。
    """
    bk_ts = bk["ts_ns"].values.astype(np.int64)
    t_ts = tr["ts_ns"].values.astype(np.int64)
    t_px = tr["trade_px"].values.astype(float)
    t_qty = tr["trade_qty"].values.astype(float)
    t_side = tr["side_int"].values.astype(np.int8)

    # 提取OB数据
    bids_p = np.column_stack([bk[f"bid_{i}"].values for i in range(N_LEVELS)])
    bids_q = np.column_stack([bk[f"bid_q_{i}"].values for i in range(N_LEVELS)])
    asks_p = np.column_stack([bk[f"ask_{i}"].values for i in range(N_LEVELS)])
    asks_q = np.column_stack([bk[f"ask_q_{i}"].values for i in range(N_LEVELS)])

    # DriftTable pnl (按level)
    ask_pnl = np.full(N_LEVELS, np.nan)
    bid_pnl = np.full(N_LEVELS, np.nan)
    ask_fills = np.full(N_LEVELS, 1.0)
    bid_fills = np.full(N_LEVELS, 1.0)
    if drift_df is not None and "tau_sec" in drift_df.columns:
        tau60 = drift_df[drift_df["tau_sec"] == 60.0]
        for lv in range(N_LEVELS):
            a = tau60[(tau60["maker_side"] == "ASK") & (tau60["level"] == lv)]
            b = tau60[(tau60["maker_side"] == "BID") & (tau60["level"] == lv)]
            if not a.empty:
                ask_pnl[lv] = a["pnl_mid_bps"].iloc[0]
                ask_fills[lv] = a["fills_per_day"].iloc[0] if "fills_per_day" in a.columns else 1.0
            if not b.empty:
                bid_pnl[lv] = b["pnl_mid_bps"].iloc[0]
                bid_fills[lv] = b["fills_per_day"].iloc[0] if "fills_per_day" in b.columns else 1.0

    results_old: list[dict] = []
    results_new: list[dict] = []

    # 抽样: 每N个snapshot取一个, 避免太多重复
    step = max(1, len(bk_ts) // 2000)
    for si in range(0, len(bk_ts), step):
        mid = 0.5 * (bids_p[si, 0] + asks_p[si, 0])
        if mid <= 0:
            continue

        # 计算累积USD深度
        cum_ask = np.zeros(N_LEVELS)
        cum_bid = np.zeros(N_LEVELS)
        ca, cb = 0.0, 0.0
        for lv in range(N_LEVELS):
            cum_ask[lv] = ca
            cum_bid[lv] = cb
            if asks_p[si, lv] > mid and asks_q[si, lv] > 0:
                ca += asks_p[si, lv] * asks_q[si, lv]
            if bids_p[si, lv] > 0 and bids_q[si, lv] > 0:
                cb += bids_p[si, lv] * bids_q[si, lv]

        # 收集候选档位
        class Cand:
            def __init__(self, lv, px, pnl, fills, efu):
                self.lv = lv; self.px = px; self.pnl = pnl; self.fills = fills; self.efu = efu

        ask_cands, bid_cands = [], []
        for lv in range(N_LEVELS):
            # ASK
            ap = asks_p[si, lv]
            if ap > mid and ap < mid * 1.1:
                pnl = ask_pnl[lv] if np.isfinite(ask_pnl[lv]) else 5.0
                fills = ask_fills[lv]
                efu = 0.0
                if ask_model is not None and pnl > 0.5:
                    efu = expected_fill_usd(cum_ask[lv], K_ORDER_USD,
                                            ask_model[0], ask_model[1], ask_model[2], T_HORIZON)
                if pnl > 0.5:
                    ask_cands.append(Cand(lv, ap, pnl, fills, efu))
            # BID
            bp = bids_p[si, lv]
            if bp > 0 and bp < mid:
                pnl = bid_pnl[lv] if np.isfinite(bid_pnl[lv]) else 5.0
                fills = bid_fills[lv]
                efu = 0.0
                if bid_model is not None and pnl > 0.5:
                    efu = expected_fill_usd(cum_bid[lv], K_ORDER_USD,
                                            bid_model[0], bid_model[1], bid_model[2], T_HORIZON)
                if pnl > 0.5:
                    bid_cands.append(Cand(lv, bp, pnl, fills, efu))

        # 旧排序: pnl × fills
        ask_old = sorted(ask_cands, key=lambda c: c.pnl * c.fills, reverse=True)[:MAX_PER_SIDE]
        bid_old = sorted(bid_cands, key=lambda c: c.pnl * c.fills, reverse=True)[:MAX_PER_SIDE]

        # 新排序: pnl × EFU (fallback到 fills)
        ask_new = sorted(ask_cands,
                         key=lambda c: c.pnl * (c.efu if c.efu > 0 else c.fills),
                         reverse=True)[:MAX_PER_SIDE]
        bid_new = sorted(bid_cands,
                         key=lambda c: c.pnl * (c.efu if c.efu > 0 else c.fills),
                         reverse=True)[:MAX_PER_SIDE]

        # 模拟成交: 找该snapshot之后的trades
        t0 = bk_ts[si]
        t1 = t0 + int(60 * 1e9)  # 60秒窗口
        t_mask = (t_ts > t0) & (t_ts <= t1)
        future_px = t_px[t_mask]
        future_side = t_side[t_mask]
        future_qty = t_qty[t_mask]

        # 对每组选档, 判断是否成交+计算markout
        def eval_cands(cands, side_label, side_int):
            for c in cands:
                # 判断成交: 是否有同方向trade价格穿越
                if side_int == 1:  # BUY/BID: taker SELL, price <= bid_price
                    filled = np.any((future_side == -1) & (future_px <= c.px))
                else:  # SELL/ASK: taker BUY, price >= ask_price
                    filled = np.any((future_side == 1) & (future_px >= c.px))

                # 计算markout: 60s后mid变化
                t2 = t0 + int(60 * 1e9)
                idx2 = np.searchsorted(bk_ts, t2, side="right") - 1
                if idx2 >= si and idx2 < len(bk_ts):
                    mid_future = 0.5 * (bids_p[idx2, 0] + asks_p[idx2, 0])
                    if side_int == 1:  # BID fill: markout = future_mid - fill_price
                        mo = (mid_future - c.px) / c.px * 1e4
                    else:  # ASK fill: markout = fill_price - future_mid
                        mo = (c.px - mid_future) / c.px * 1e4
                else:
                    mo = np.nan

                yield {
                    "level": c.lv, "side": side_label, "pnl_bps": c.pnl,
                    "fills_per_day": c.fills, "efu": c.efu if c.efu > 0 else c.fills,
                    "filled": filled, "markout_60s_bps": mo,
                }

        for r in eval_cands(ask_old, "ASK", -1):
            r["method"] = "old"
            results_old.append(r)
        for r in eval_cands(bid_old, "BID", 1):
            r["method"] = "old"
            results_old.append(r)
        for r in eval_cands(ask_new, "ASK", -1):
            r["method"] = "new"
            results_new.append(r)
        for r in eval_cands(bid_new, "BID", 1):
            r["method"] = "new"
            results_new.append(r)

    return pd.DataFrame(results_old), pd.DataFrame(results_new)


# ═══════════════════════════════════════════════════════════
# 汇总
# ═══════════════════════════════════════════════════════════

def summarize(df_old: pd.DataFrame, df_new: pd.DataFrame, symbol_name: str):
    """打印对比结果"""
    print(f"\n{'='*80}")
    print(f"  Ranking Validation: {symbol_name}")
    print(f"{'='*80}")

    for label, df in [("OLD (pnl×fills)", df_old), ("NEW (pnl×EFU)", df_new)]:
        valid_mo = df["markout_60s_bps"].dropna()
        n = len(df)
        n_filled = df["filled"].sum()
        fill_pct = n_filled / n * 100 if n > 0 else 0
        avg_mo = valid_mo.mean()
        avg_pnl = df["pnl_bps"].mean()
        avg_efu = df["efu"].mean()

        print(f"\n  {label}:")
        print(f"    Samples: {n}, Filled: {n_filled} ({fill_pct:.1f}%)")
        print(f"    Avg pnl_bps: {avg_pnl:+.2f}, Avg EFU: {avg_efu:.4f}")
        print(f"    Avg markout_60s: {avg_mo:+.2f} bps (all)")
        # 只看成交的
        filled_mo = valid_mo[df["filled"]]
        if len(filled_mo) > 0:
            print(f"    Avg markout_60s (filled only): {filled_mo.mean():+.2f} bps")
            print(f"    Markout>0 frac: {(filled_mo > 0).mean()*100:.1f}%")

    # 直接对比
    mo_old = df_old["markout_60s_bps"].dropna()
    mo_new = df_new["markout_60s_bps"].dropna()
    if len(mo_old) > 0 and len(mo_new) > 0:
        delta = mo_new.mean() - mo_old.mean()
        print(f"\n  >>> NEW - OLD markout delta: {delta:+.3f} bps")
        if delta > 0:
            print(f"  >>> 新模型优于旧模型 ✓")
        else:
            print(f"  >>> 新模型未优于旧模型 ✗")

    # 分side看
    for side in ["ASK", "BID"]:
        print(f"\n  --- {side} ---")
        for label, df in [("OLD", df_old), ("NEW", df_new)]:
            sub = df[df["side"] == side]
            mo = sub["markout_60s_bps"].dropna()
            if len(mo) > 0:
                print(f"  {label}: markout={mo.mean():+.2f}, fill%={sub['filled'].mean()*100:.1f}%")


# ═══════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--symbol", type=str, default="XBTUSD")
    parser.add_argument("--date", type=str, default="2026-03-20")
    args = parser.parse_args()

    sd = args.symbol
    sn_map = {"XBTUSD": "BTC/USD", "ETHUSD": "ETH/USD", "EURUSD": "EUR/USD",
              "SOLUSD": "SOL/USD", "XRPUSD": "XRP/USD", "GBPUSD": "GBP/USD",
              "USDTUSD": "USDT/USD"}
    sn = sn_map.get(sd, sd)

    # 加载1天数据
    book_f, trade_f, ok = find_files(sd, args.date)
    if not ok:
        print(f"Data not found for {sd}/{args.date}")
        return
    print(f"Loading {sd} {args.date}...")
    bk = load_book(book_f)
    tr = load_trades(trade_f)

    # 加载 DriftTable
    base = sd.lower()
    drift_path = f"drift_tables/drift_{base}.csv"
    if not os.path.exists(drift_path):
        drift_path = f"python/drift_tables/drift_{base}.csv"
    if not os.path.exists(drift_path):
        # 尝试从output/hazard找
        drift_path2 = f"output/hazard/hazard_curve_{base}.csv"
        if os.path.exists(drift_path2):
            drift_path = drift_path2
    if os.path.exists(drift_path) and drift_path.endswith(".csv"):
        drift_df = pd.read_csv(drift_path)
        if "tau_sec" not in drift_df.columns:
            print(f"WARNING: no tau_sec column in {drift_path}, using default pnl")
            drift_df = None
    else:
        print(f"WARNING: no drift table at {drift_path}, using default pnl=5bps")
        drift_df = None

    # 加载 EFU 模型
    pair = sn.replace("/", "-")
    ask_model, bid_model = load_efu_model(pair)
    has_efu = ask_model is not None and bid_model is not None
    print(f"EFU model: {'loaded' if has_efu else 'NOT FOUND (fallback to fills)'}")

    # 运行
    df_old, df_new = simulate_ranking(bk, tr, drift_df, ask_model, bid_model)
    summarize(df_old, df_new, sn)


if __name__ == "__main__":
    main()
