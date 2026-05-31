#!/usr/bin/env python3
"""
Phase 0: Taker Reach Rate 形状探索
=====================================
从 book_snapshot_25 + trades 构造每笔 taker 的吃穿深度 Y_j，
估计 ĉ(x) = #{Y_j ≥ x} / total_days，画 4 张诊断图。

P0.1: log c(x) vs x（指数假设检验）
P0.2: Isotonic 单调性验证
P0.3: BID vs ASK 分侧对比
P0.4: c(x) × Edge(x) 甜点曲线

用法:
  python fill_hazard_analysis.py --symbol XBTUSD
  python fill_hazard_analysis.py --symbol XBTUSD --date 2026-03-09,2026-03-10
  python fill_hazard_analysis.py --all  # 全部 symbol

输出:
  output/hazard/ 目录下:
    taker_reach_events_<symbol>.csv     # 每笔 trade 的 Y_j
    hazard_curve_<symbol>.csv           # ĉ(x) 各深度 + Edge
    hazard_<symbol>_4panels.png         # 4 张诊断图
"""

from __future__ import annotations
import argparse, os, sys
from collections.abc import Sequence
from typing import Optional

import numpy as np
import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from markout_eval import load_book, load_trades  # pyright: ignore[reportImplicitRelativeImport]

# ── 配置 ──────────────────────────────────────────────
DATA_ROOT = "/data/kraken"
OUTPUT_DIR = "output/hazard"
N_LEVELS = 25
# 诊断用的深度网格 (USD)
DEPTH_GRID = [0, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000]
# 候选分段边界 (Phase 1 用)
SEGMENT_EDGES = [0, 50, 100, 200, 500, 1000, 2000, 5000, 10000]
TAUS_SEC = (0.1, 0.5, 1.0, 5.0, 10.0, 30.0, 60.0, 300.0)

# Kraken spot pairs from constant.h + display name + quote currency
# 格式: (symbol_dir, display_name, quote_currency)
# quote="USD" → 无需转换; 其他 → 需要加载 quote/USD pair 做汇率
KRAKEN_PAIRS: list[tuple[str, str, str]] = [
    # USD quote
    ("XBTUSD", "BTC/USD", "USD"),
    ("ETHUSD", "ETH/USD", "USD"),
    ("SOLUSD", "SOL/USD", "USD"),
    ("XRPUSD", "XRP/USD", "USD"),
    ("EURUSD", "EUR/USD", "USD"),
    ("GBPUSD", "GBP/USD", "USD"),
    ("USDTUSD", "USDT/USD", "USD"),
    ("USDCUSD", "USDC/USD", "USD"),
    # EUR quote
    ("XBTEUR", "BTC/EUR", "EUR"),
    ("ETHEUR", "ETH/EUR", "EUR"),
    ("SOLEUR", "SOL/EUR", "EUR"),
    ("XRPEUR", "XRP/EUR", "EUR"),
    ("USDTEUR", "USDT/EUR", "EUR"),
    ("USDCEUR", "USDC/EUR", "EUR"),
    # GBP quote
    ("XBTGBP", "BTC/GBP", "GBP"),
    ("ETHGBP", "ETH/GBP", "GBP"),
    ("SOLGBP", "SOL/GBP", "GBP"),
    ("XRPGBP", "XRP/GBP", "GBP"),
    ("USDTGBP", "USDT/GBP", "GBP"),
    ("USDCGBP", "USDC/GBP", "GBP"),
    # USDT quote
    ("XBTUSDT", "BTC/USDT", "USDT"),
    ("ETHUSDT", "ETH/USDT", "USDT"),
    ("SOLUSDT", "SOL/USDT", "USDT"),
    ("XRPUSDT", "XRP/USDT", "USDT"),
    ("USDCUSDT", "USDC/USDT", "USDT"),
    # USDC quote
    ("XBTUSDC", "BTC/USDC", "USDC"),
]

# quote → USD pair mapping
QUOTE_TO_USD_PAIR: dict[str, str] = {
    "EUR": "EURUSD",
    "GBP": "GBPUSD",
    "USDT": "USDTUSD",
    "USDC": "USDCUSD",
}

# ── 工具 ──────────────────────────────────────────────

def _col_i64(df: pd.DataFrame, name: str) -> np.ndarray:
    return np.asarray(df[name], dtype=np.int64)

def _col_f64(df: pd.DataFrame, name: str) -> np.ndarray:
    return np.asarray(df[name], dtype=np.float64)

def find_files(symbol_dir: str, date: str) -> tuple[str, str, bool]:
    book_f = os.path.join(DATA_ROOT, "book_snapshot_25", symbol_dir,
                          f"{date}_{symbol_dir}.csv.gz")
    trade_f = os.path.join(DATA_ROOT, "trades", symbol_dir,
                           f"{date}_{symbol_dir}.csv.gz")
    return book_f, trade_f, os.path.exists(book_f) and os.path.exists(trade_f)

def get_dates(symbol_dir: str) -> list[str]:
    d = os.path.join(DATA_ROOT, "book_snapshot_25", symbol_dir)
    if not os.path.isdir(d):
        return []
    return sorted([f.split("_")[0] for f in os.listdir(d) if f.endswith(".csv.gz")])

# ── P0.1 核心: 构造 taker reach events ──────────────────

def compute_taker_reach_events(
    df_book: pd.DataFrame,
    df_trade: pd.DataFrame,
    quote_usd_factor: np.ndarray | None = None,
    n_levels: int = N_LEVELS,
) -> pd.DataFrame:
    """
    对每笔 trade 计算其吃穿的累计 USD 深度 Y_j = consume_depth_usd。

    算法:
      1. 把 trades 按时间对齐到最近的 book snapshot（前一个）
      2. 对每笔 trade:
         - buy taker → 从 best ask 开始累加 ask_qty × ask_price，
           直到累计 base qty ≥ trade.qty
         - sell taker → 从 best bid 开始累加 bid_qty × bid_price，
           直到累计 base qty ≥ trade.qty
      3. 如果 quote ≠ USD，用 quote_usd_factor 将深度转为 USD
      4. 记录: consume_depth_usd, side, price, qty, mid, spread, ts

    注意: Kraken public trades 无 aggressor side。用价格位置推断：
      trade.price >= best_ask → buy taker (吃掉 asks)
      trade.price <= best_bid → sell taker (吃掉 bids)
      在 [best_bid, best_ask] 之间 → 跳过（无法判定方向）

    quote_usd_factor: shape (n_snapshots,) 或 None。如果非 None，
      每档的 cum_depth 乘以此因子转为 USD（用于非 USD-quote pair）。
    """
    bk_ts = _col_i64(df_book, "ts_ns")

    # 提取 OB 各档价格/数量
    bid_p = np.column_stack(
        [_col_f64(df_book, f"bid_{i}") for i in range(n_levels)])
    bid_q = np.column_stack(
        [_col_f64(df_book, f"bid_q_{i}") for i in range(n_levels)])
    ask_p = np.column_stack(
        [_col_f64(df_book, f"ask_{i}") for i in range(n_levels)])
    ask_q = np.column_stack(
        [_col_f64(df_book, f"ask_q_{i}") for i in range(n_levels)])

    # 计算 best bid/ask + mid + spread (bps)
    best_bid = bid_p[:, 0]
    best_ask = ask_p[:, 0]
    mid = 0.5 * (best_bid + best_ask)
    spread_bps = np.where(mid > 0, (best_ask - best_bid) / mid * 1e4, 0)

    # 计算每档的累计 USD depth (从 best 开始)
    # ASK 侧: cum_ask_usd[i] = Σ_{j=0}^{i} ask_p[j] × ask_q[j]
    cum_ask_usd = np.cumsum(ask_p * ask_q, axis=1)  # shape: (n_snap, n_levels)
    # BID 侧: cum_bid_usd[i] = Σ_{j=0}^{i} bid_p[j] × bid_q[j]
    cum_bid_usd = np.cumsum(bid_p * bid_q, axis=1)

    t_ts = _col_i64(df_trade, "ts_ns")
    t_price = _col_f64(df_trade, "trade_px")
    t_qty = _col_f64(df_trade, "trade_qty")

    # 每笔 trade 对齐到最近的 book snapshot（前一个，即 trade 发生时的 OB 状态）
    bk_idx_arr = np.searchsorted(bk_ts, t_ts, side="right") - 1
    valid = (bk_idx_arr >= 0) & (bk_idx_arr < len(bk_ts))
    bk_idx_arr = bk_idx_arr[valid]
    t_price = t_price[valid]
    t_qty = t_qty[valid]
    t_ts_f = t_ts[valid]

    n = len(bk_idx_arr)
    results: list[dict] = []

    for i in range(n):
        bi = bk_idx_arr[i]
        px = t_price[i]
        qty = t_qty[i]
        ts = t_ts_f[i]

        if qty <= 0 or px <= 0:
            continue

        b0 = best_bid[bi]
        a0 = best_ask[bi]
        m = mid[bi]
        spd = spread_bps[bi]

        if m <= 0:
            continue

        # 判定方向
        if px >= a0:
            side = "buy_taker"   # 买方主动 → 吃 ASK
        elif px <= b0:
            side = "sell_taker"  # 卖方主动 → 吃 BID
        else:
            continue  # 在 spread 内，无法判定

        qty_left = qty
        max_cum_usd = 0.0

        if side == "buy_taker":
            for lv in range(n_levels):
                ap = ask_p[bi, lv]
                aq = ask_q[bi, lv]
                if ap <= 0 or aq <= 0:
                    continue
                eat = min(qty_left, aq)
                max_cum_usd = cum_ask_usd[bi, lv]
                qty_left -= eat
                if qty_left <= 1e-12:
                    break
        else:  # sell_taker
            for lv in range(n_levels):
                bp = bid_p[bi, lv]
                bq = bid_q[bi, lv]
                if bp <= 0 or bq <= 0:
                    continue
                eat = min(qty_left, bq)
                max_cum_usd = cum_bid_usd[bi, lv]
                qty_left -= eat
                if qty_left <= 1e-12:
                    break

        # 非 USD quote → 转 USD
        if quote_usd_factor is not None:
            max_cum_usd *= quote_usd_factor[bi]

        if max_cum_usd > 0:
            results.append({
                "timestamp_ns": int(ts),
                "side": side,
                "price": float(px),
                "qty": float(qty),
                "notional_usd": float(px * qty),
                "consume_depth_usd": float(max_cum_usd),
                "best_bid": float(b0),
                "best_ask": float(a0),
                "mid": float(m),
                "spread_bps": float(spd),
                "book_idx": int(bi),
            })

    return pd.DataFrame(results)


# ── P0.1b: 从 reach events 估计 ĉ(x) ───────────────────

def estimate_hazard_curve(
    events: pd.DataFrame,
    depth_grid: Sequence[float],
    total_days: float,
) -> pd.DataFrame:
    """
    ĉ(x) = #{Y_j ≥ x} / total_days    (events/day)

    对 BID 侧: sell_taker 的 Y_j（吃掉 bids → maker bid fill）
    对 ASK 侧: buy_taker 的 Y_j（吃掉 asks → maker ask fill）

    注意: 这里 BID fill 对应 sell_taker events, ASK fill 对应 buy_taker events。
    """
    rows = []
    for x in depth_grid:
        # ASK side: buy_taker 吃到深度 x
        n_ask = int(((events["side"] == "buy_taker") &
                    (events["consume_depth_usd"] >= x)).sum())
        # BID side: sell_taker 吃到深度 x
        n_bid = int(((events["side"] == "sell_taker") &
                    (events["consume_depth_usd"] >= x)).sum())

        rows.append({
            "depth_usd": float(x),
            "c_per_day_ask": float(n_ask / total_days),
            "c_per_day_bid": float(n_bid / total_days),
            "c_per_day_total": float((n_ask + n_bid) / total_days),
            "n_events_ask": n_ask,
            "n_events_bid": n_bid,
        })
    return pd.DataFrame(rows)


# ── P0.2: Isotonic 单调性 ──────────────────────────────

def isotonic_regression(y: np.ndarray, w: Optional[np.ndarray] = None) -> np.ndarray:
    """
    Pool Adjacent Violators (PAV) — 保序回归，强制单调递减。
    y_hat[i] ≤ y_hat[i-1] 对所有 i>0。
    """
    n = len(y)
    if w is None:
        w = np.ones(n, dtype=np.float64)
    y_hat = y.copy().astype(np.float64)
    w_hat = w.copy().astype(np.float64)

    i = 0
    blocks: list[tuple[float, float, int, int]] = []  # (sum_wy, sum_w, start, end)
    while i < n:
        wy = y_hat[i] * w_hat[i]
        sw = w_hat[i]
        start = i
        end = i + 1
        # pool backward if violates monotonic decreasing
        while blocks and (wy / sw) > (blocks[-1][0] / blocks[-1][1]):
            wy += blocks[-1][0]
            sw += blocks[-1][1]
            start = blocks[-1][2]
            blocks.pop()
        blocks.append((wy, sw, start, end))
        i += 1

    for wy, sw, start, end in blocks:
        y_hat[start:end] = wy / sw
    return y_hat


# ── P0.4: 加载 DriftTable pnl ──────────────────────────

def load_drift_pnl(symbol_name: str) -> Optional[pd.DataFrame]:
    """从已有的 drift 表读取各档位 pnl（tau=60s）。"""
    base = symbol_name.lower().replace("/", "").replace("btc", "xbt")
    path = os.path.join("drift_tables", f"drift_{base}.csv")
    if not os.path.exists(path):
        # 尝试从 python/ 目录下查找
        path2 = os.path.join(os.path.dirname(__file__), "..", "drift_tables",
                             f"drift_{base}.csv")
        if os.path.exists(path2):
            path = path2
        else:
            return None
    df = pd.read_csv(path)
    df = df[df["tau_sec"] == 60.0]
    return df


def map_pnl_to_depth(
    hazard_df: pd.DataFrame,
    drift_df: pd.DataFrame,
    symbol_dir: str,
) -> pd.DataFrame:
    """
    把 DriftTable 的 level → pnl 映射到 depth → edge。
    用 avg_dist_bps 近似 USD depth: depth_usd ≈ mid × (exp(dist_bps/1e4) - 1)
    这里做简化：直接用 level 对应的 depth_bin。
    更精确的做法是 Phase 1 从数据里算 level→depth 映射。
    """
    # 先用 level 2, 5, 10, 15, 20 的 pnl 近似，后续可改进
    out = hazard_df.copy()

    # 为每个 depth_bin 找最接近的 level pnl
    for side_tag, side_name in [("c_per_day_ask", "ASK"), ("c_per_day_bid", "BID")]:
        edge_col = f"edge_{'ask' if side_name == 'ASK' else 'bid'}_bps"
        score_col = f"score_{'ask' if side_name == 'ASK' else 'bid'}"

        edge_vals = np.full(len(out), np.nan)
        if drift_df is not None:
            side_df = drift_df[drift_df["maker_side"] == side_name]
            # 按 avg_dist_bps 排序
            side_df = side_df.sort_values("avg_dist_bps")
            levels = side_df["level"].values
            pnls = side_df["pnl_mid_bps"].values
            dists = side_df["avg_dist_bps"].values

            # 简单映射: 用 dist_bps 最接近 depth_usd 的档位
            for idx, row in out.iterrows():
                depth = row["depth_usd"]
                if depth <= 0:
                    continue
                # 粗略: depth_usd ≈ mid × dist_bps/1e4 × Q_typical
                # 这里直接用 dist_bps 作为近似排序，后续 Phase 1 改进
                # 暂时用 level 作为 proxy
                if len(levels) > 0:
                    edge_vals[idx] = pnls[-1]  # fallback
        out[edge_col] = edge_vals
        if not np.all(np.isnan(edge_vals)):
            out[score_col] = out[side_tag] * out[edge_col]

    return out


# ── 画图 ───────────────────────────────────────────────

def plot_4panels(
    hazard_df: pd.DataFrame,
    symbol_name: str,
    output_path: str,
):
    """画 Phase 0 的 4 张诊断图。"""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(2, 2, figsize=(16, 12))
    fig.suptitle(f"Phase 0: Taker Reach Rate — {symbol_name}", fontsize=14,
                 fontweight="bold")

    x = hazard_df["depth_usd"].values
    c_ask = hazard_df["c_per_day_ask"].values
    c_bid = hazard_df["c_per_day_bid"].values
    c_total = hazard_df["c_per_day_total"].values

    mask = x > 0
    xp = x[mask]
    c_ask_p = np.maximum(c_ask[mask], 1e-6)
    c_bid_p = np.maximum(c_bid[mask], 1e-6)
    c_total_p = np.maximum(c_total[mask], 1e-6)

    # ── 图 1: c(x) vs x ──
    ax1 = axes[0, 0]
    ax1.plot(xp, c_ask_p, "o-", label="ASK fill (buy_taker)", markersize=4,
             color="C3")
    ax1.plot(xp, c_bid_p, "s-", label="BID fill (sell_taker)", markersize=4,
             color="C0")
    ax1.set_xlabel("Cumulative USD Depth")
    ax1.set_ylabel("c(x) [events/day]")
    ax1.set_title("P0.1: Reach Rate vs Depth")
    ax1.legend()
    ax1.set_yscale("log")
    ax1.grid(True, alpha=0.3)

    # ── 图 2: log c(x) vs x (指数检验) ──
    ax2 = axes[0, 1]
    ax2.plot(xp, np.log(c_ask_p), "o-", label="ASK fill", markersize=4,
             color="C3")
    ax2.plot(xp, np.log(c_bid_p), "s-", label="BID fill", markersize=4,
             color="C0")

    # 拟合线性趋势线 (浅档)
    shallow = xp <= 1000
    if shallow.sum() >= 3:
        for y, c, lbl in [(xp[shallow], np.log(c_ask_p[shallow]), "ASK linear"),
                           (xp[shallow], np.log(c_bid_p[shallow]), "BID linear")]:
            if len(y) >= 3:
                coeffs = np.polyfit(y, c, 1)
                ax2.plot(y, np.polyval(coeffs, y), "--", alpha=0.5,
                         label=f"{lbl} (k={coeffs[0]:.6f})")

    ax2.set_xlabel("Cumulative USD Depth")
    ax2.set_ylabel("log c(x)")
    ax2.set_title("P0.1: log c(x) vs x — 线性 → 指数模型OK")
    ax2.legend(fontsize=8)
    ax2.grid(True, alpha=0.3)

    # ── 图 3: log c(x) vs log x (幂律检验) ──
    ax3 = axes[1, 0]
    ax3.plot(np.log(xp), np.log(c_ask_p), "o-", label="ASK fill", markersize=4,
             color="C3")
    ax3.plot(np.log(xp), np.log(c_bid_p), "s-", label="BID fill", markersize=4,
             color="C0")

    if shallow.sum() >= 3:
        for y_log, c_log, lbl in [
            (np.log(xp[shallow]), np.log(c_ask_p[shallow]), "ASK"),
            (np.log(xp[shallow]), np.log(c_bid_p[shallow]), "BID"),
        ]:
            if len(y_log) >= 3:
                coeffs = np.polyfit(y_log, c_log, 1)
                ax3.plot(y_log, np.polyval(coeffs, y_log), "--", alpha=0.5,
                         label=f"{lbl} (slope={coeffs[0]:.3f})")

    ax3.set_xlabel("log(USD Depth)")
    ax3.set_ylabel("log c(x)")
    ax3.set_title("P0.1: log c(x) vs log x — 线性 → 幂律更合适")
    ax3.legend(fontsize=8)
    ax3.grid(True, alpha=0.3)

    # ── 图 4: c(x) × Edge(x) vs x (甜点曲线) ──
    ax4 = axes[1, 1]
    has_edge = False
    for col, lbl, clr in [("score_ask", "ASK score", "C3"),
                           ("score_bid", "BID score", "C0")]:
        if col in hazard_df.columns:
            sv = hazard_df[col].values[mask]
            sv_f = np.where(np.isfinite(sv), sv, 0)
            ax4.plot(xp, sv_f, "o-", label=lbl, markersize=4, color=clr)
            has_edge = True
    if not has_edge:
        ax4.text(0.5, 0.5, "Edge data not available\n(run drift_table.py first)",
                 transform=ax4.transAxes, ha="center", va="center", fontsize=12)
        ax4.set_title("P0.4: Sweet Spot (need DriftTable data)")
    else:
        ax4.set_title("P0.4: c(x) × Edge(x) — 甜点区")
    ax4.set_xlabel("Cumulative USD Depth")
    ax4.set_ylabel("Score [events/day × bps]")
    ax4.legend(fontsize=8)
    ax4.grid(True, alpha=0.3)
    # 标注最大值
    if has_edge:
        for col, lbl in [("score_ask", "ASK"), ("score_bid", "BID")]:
            if col in hazard_df.columns:
                sv = hazard_df[col].values[mask]
                if len(sv) > 0:
                    imax = np.nanargmax(sv)
                    ax4.annotate(f"{lbl} max @ ${xp[imax]:.0f}",
                                 (xp[imax], sv[imax]),
                                 fontsize=8, alpha=0.7)

    plt.tight_layout()
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    fig.savefig(output_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  → {output_path}")


# ── Main ───────────────────────────────────────────────

def run_phase0(
    symbol_dir: str,
    symbol_name: str,
    quote: str,
    dates: list[str],
) -> Optional[tuple[pd.DataFrame, pd.DataFrame]]:
    """对一个 symbol 跑完 Phase 0 全部。quote="USD" 则无需汇率转换。"""
    print(f"\n{'='*60}")
    print(f"Phase 0: {symbol_name} ({symbol_dir})  quote={quote}")
    print(f"Dates: {len(dates)} days")

    all_events: list[pd.DataFrame] = []
    total_seconds = 0.0

    # 非 USD quote: 加载对应 USD pair 做汇率
    usd_pair = QUOTE_TO_USD_PAIR.get(quote, "")
    need_conversion = (quote != "USD" and usd_pair != "")

    for d in dates:
        book_f, trade_f, ok = find_files(symbol_dir, d)
        if not ok:
            continue
        try:
            bk = load_book(book_f)
            tr = load_trades(trade_f)

            quote_usd_factor = None
            if need_conversion:
                # 加载 quote/USD pair 的 book，取 mid
                usd_book_f, _, usd_ok = find_files(usd_pair, d)
                if usd_ok:
                    usd_bk = load_book(usd_book_f)
                    # 对齐时间戳: 取 ≤ 目标 book ts 的最近 USD mid
                    usd_ts = _col_i64(usd_bk, "ts_ns")
                    usd_mid = 0.5 * (_col_f64(usd_bk, "bid0") + _col_f64(usd_bk, "ask0"))
                    bk_t = _col_i64(bk, "ts_ns")
                    idx = np.searchsorted(usd_ts, bk_t, side="right") - 1
                    idx = np.clip(idx, 0, len(usd_ts) - 1)
                    quote_usd_factor = usd_mid[idx]
                else:
                    print(f"  {d}: WARNING {usd_pair} data missing, skipping")
                    continue

            events = compute_taker_reach_events(bk, tr, quote_usd_factor)
            if not events.empty:
                all_events.append(events)
                day_sec = (bk["ts_ns"].max() - bk["ts_ns"].min()) / 1e9
                total_seconds += day_sec
                print(f"  {d}: {len(events)} events, {day_sec/3600:.1f}h"
                      f"{' (conv ' + usd_pair + ')' if need_conversion else ''}")
        except Exception as e:
            print(f"  {d}: ERROR {e}")

    if not all_events:
        print(f"  No valid data for {symbol_name}")
        return None

    events_all = pd.concat(all_events, ignore_index=True)
    total_days = total_seconds / 86400.0
    print(f"  Total: {len(events_all)} events, {total_days:.1f} days")

    # 保存 reach events
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    events_path = os.path.join(OUTPUT_DIR,
                               f"taker_reach_events_{symbol_dir.lower()}.csv")
    events_all.to_csv(events_path, index=False)
    print(f"  → {events_path}")

    # 估计 ĉ(x)
    hazard_df = estimate_hazard_curve(events_all, DEPTH_GRID, total_days)

    # Isotonic 单调性
    for col in ["c_per_day_ask", "c_per_day_bid", "c_per_day_total"]:
        y_raw = hazard_df[col].values
        y_iso = isotonic_regression(y_raw)
        hazard_df[col + "_iso"] = y_iso

    # 加载 DriftTable Edge
    drift_df = load_drift_pnl(symbol_name)
    hazard_df = map_pnl_to_depth(hazard_df, drift_df, symbol_dir)

    # 保存 hazard curve
    curve_path = os.path.join(OUTPUT_DIR,
                              f"hazard_curve_{symbol_dir.lower()}.csv")
    hazard_df.to_csv(curve_path, index=False)
    print(f"  → {curve_path}")

    # 画图
    plot_path = os.path.join(OUTPUT_DIR,
                             f"hazard_{symbol_dir.lower()}_4panels.png")
    plot_4panels(hazard_df, symbol_name, plot_path)

    # ── 日志诊断结论 ──
    print(f"\n  === {symbol_name} 诊断结论 ===")
    mask = hazard_df["depth_usd"].values > 0
    xp = hazard_df["depth_usd"].values[mask]
    for side_tag, side_name in [("c_per_day_ask", "ASK fill"),
                                 ("c_per_day_bid", "BID fill")]:
        y = np.log(np.maximum(hazard_df[side_tag].values[mask], 1e-6))
        if len(xp) >= 3:
            k = np.polyfit(xp[:6], y[:6], 1)[0]  # 浅档线性拟合
            print(f"  {side_name}: k={k:.6f} (浅档指数衰减率), "
                  f"base={np.exp(y[0]):.1f} events/day")

    return events_all, hazard_df


def main():
    parser = argparse.ArgumentParser(description="Phase 0: Taker Reach Rate 分析")
    parser.add_argument("--symbol", type=str, help="symbol 目录名 (如 XBTUSD)")
    parser.add_argument("--date", type=str, help="日期, 逗号分隔")
    parser.add_argument("--all", action="store_true", help="全部 symbol")
    args = parser.parse_args()

    if args.all:
        tasks = KRAKEN_PAIRS
    elif args.symbol:
        # 在 KRAKEN_PAIRS 中查找匹配
        tasks = [(sd, sn, q) for sd, sn, q in KRAKEN_PAIRS if sd == args.symbol]
        if not tasks:
            tasks = [(args.symbol, args.symbol, "USD")]
    else:
        parser.print_help()
        return

    if args.date:
        dates = [d.strip() for d in args.date.split(",")]
    else:
        dates = None

    for sd, sn, quote in tasks:
        if dates is None:
            dl = get_dates(sd)
        else:
            dl = dates
        if not dl:
            print(f"  {sn}: no dates")
            continue
        run_phase0(sd, sn, quote, dl)

    print(f"\nDone. Output: {OUTPUT_DIR}/")


if __name__ == "__main__":
    main()
