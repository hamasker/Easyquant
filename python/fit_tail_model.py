#!/usr/bin/env python3
"""
Phase 1: 分段指数拟合 → prob_params CSV (TailFillModel 6列格式)

输入: Phase 0 的 output/hazard/hazard_curve_*.csv
输出: config/prob_params/kraken/<PAIR>_buy.csv / _sell.csv

每行6列(无表头): slope, intercept, end, ten_b, k, prefix_end

用法:
  python fit_tail_model.py --all          # 全部 pair
  python fit_tail_model.py --pair XBTUSD  # 单个
"""

from __future__ import annotations
import os, sys, argparse
import numpy as np
import pandas as pd

# ── 配置 ──────────────────────────────────────────────
SEGMENT_EDGES = [0, 200, 1000, 5000, 10000]
HAZARD_DIR = "output/hazard"
OUTPUT_DIR = "config/prob_params/kraken"

# 27 Kraken pairs from constant.h
KRAKEN_PAIRS = [
    ("XBTUSD", "BTC-USD"), ("ETHUSD", "ETH-USD"), ("SOLUSD", "SOL-USD"),
    ("XRPUSD", "XRP-USD"), ("EURUSD", "EUR-USD"), ("GBPUSD", "GBP-USD"),
    ("USDTUSD", "USDT-USD"), ("USDCUSD", "USDC-USD"),
    ("XBTEUR", "BTC-EUR"), ("ETHEUR", "ETH-EUR"), ("SOLEUR", "SOL-EUR"),
    ("XRPEUR", "XRP-EUR"), ("USDTEUR", "USDT-EUR"), ("USDCEUR", "USDC-EUR"),
    ("XBTGBP", "BTC-GBP"), ("ETHGBP", "ETH-GBP"), ("SOLGBP", "SOL-GBP"),
    ("XRPGBP", "XRP-GBP"), ("USDTGBP", "USDT-GBP"), ("USDCGBP", "USDC-GBP"),
    ("XBTUSDT", "BTC-USDT"), ("ETHUSDT", "ETH-USDT"), ("SOLUSDT", "SOL-USDT"),
    ("XRPUSDT", "XRP-USDT"), ("USDCUSDT", "USDC-USDT"),
    ("XBTUSDC", "BTC-USDC"),
]

# ── 核心: 分段指数拟合 ─────────────────────────────────

def fit_piecewise_exp(
    depth: np.ndarray,
    c_obs: np.ndarray,
    edges: list[float],
    min_events: int = 10,
) -> list[dict]:
    """
    在预设分段边界上拟合 c(x) = ten_b × exp(k × x)。

    对每段 [edges[j], edges[j+1]):
      1. 取该段内所有 depth grid 点
      2. WLS 拟合: log(c) = log(ten_b) + k × x  (权重=sqrt(c_obs))
      3. 计算 prefix_end = ∫_0^end c(x)dx
      4. 确保 k ≤ 0（强制单调递减）

    Returns:
        list of dict with keys: slope, intercept, end, ten_b, k, prefix_end
    """
    # 只保留有足够 events 的深度点
    mask = (c_obs >= min_events) & np.isfinite(np.log(np.maximum(c_obs, 1e-6)))
    x = depth[mask]
    y = np.log(np.maximum(c_obs[mask], 1e-6))

    results = []
    cum_prefix = 0.0

    # 全局浅档 k（depth ≤ 500，用于 fallback）
    shallow = x <= 500
    global_k = -0.001
    if shallow.sum() >= 2:
        xs_s = x[shallow]
        ys_s = y[shallow]
        ws_s = np.sqrt(np.exp(ys_s))
        ws_s = np.maximum(ws_s, 1e-6)
        w_sum = ws_s.sum()
        wx_mean = (ws_s * xs_s).sum() / w_sum
        wy_mean = (ws_s * ys_s).sum() / w_sum
        wxx = (ws_s * (xs_s - wx_mean) ** 2).sum() / w_sum
        wxy = (ws_s * (xs_s - wx_mean) * (ys_s - wy_mean)).sum() / w_sum
        if wxx > 1e-15:
            global_k = min(wxy / wxx, 0.0)

    for j in range(len(edges) - 1):
        lo, hi = edges[j], edges[j + 1]
        seg_mask = (x >= lo) & (x < hi)

        if seg_mask.sum() < 2:
            # fallback: 用全局 k，ten_b 取段左端点的 c 值或上一段延续
            if results:
                prev = results[-1]
                c_at_lo = prev["ten_b"] * np.exp(prev["k"] * lo)
            else:
                c_at_lo = max(c_obs[0], 1e-6)
            ten_b = max(c_at_lo, 1e-6)
            k = global_k if results else global_k
            intercept = np.log(ten_b)
            slope = k
        else:
            xs = x[seg_mask]
            ys = y[seg_mask]
            ws = np.sqrt(np.exp(ys))
            ws = np.maximum(ws, 1e-6)

            w_sum = ws.sum()
            wx_mean = (ws * xs).sum() / w_sum
            wy_mean = (ws * ys).sum() / w_sum
            wxx = (ws * (xs - wx_mean) ** 2).sum() / w_sum
            wxy = (ws * (xs - wx_mean) * (ys - wy_mean)).sum() / w_sum

            if wxx > 1e-15 and w_sum >= 2:
                k = min(wxy / wxx, 0.0)
            else:
                k = global_k
            intercept = wy_mean - k * wx_mean
            ten_b = np.exp(intercept)
            slope = k

        # 计算本段对 prefix 的贡献: ∫_lo^hi c(x)dx
        if abs(k) < 1e-12:
            integral_this_seg = ten_b * (hi - lo)
        else:
            integral_this_seg = ten_b * (np.exp(k * hi) - np.exp(k * lo)) / k
        prefix_end = cum_prefix + max(integral_this_seg, 0.0)

        results.append({
            "slope": float(slope),
            "intercept": float(intercept),
            "end": float(hi),
            "ten_b": float(ten_b),
            "k": float(k),
            "prefix_end": float(prefix_end),
        })
        cum_prefix = prefix_end

    return results


# ── 主流程 ─────────────────────────────────────────────

def process_pair(symbol_dir: str, pair_name: str):
    """对一个 pair 拟合 BID+ASK 两侧，输出两个 CSV。"""
    curve_path = os.path.join(HAZARD_DIR, f"hazard_curve_{symbol_dir.lower()}.csv")
    if not os.path.exists(curve_path):
        print(f"  SKIP {pair_name}: hazard curve not found ({curve_path})")
        return

    df = pd.read_csv(curve_path)
    depth = df["depth_usd"].values
    c_ask = df["c_per_day_ask"].values
    c_bid = df["c_per_day_bid"].values

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # ASK fill = buy_taker → 输出为 _buy.csv（与 Relaxquant fit_prob 一致）
    ask_segments = fit_piecewise_exp(depth, c_ask, SEGMENT_EDGES)
    ask_df = pd.DataFrame(ask_segments)
    ask_out = os.path.join(OUTPUT_DIR, f"{pair_name}_buy.csv")
    ask_df.to_csv(ask_out, index=False, header=False)
    print(f"  {pair_name} ASK → {ask_out} ({len(ask_segments)} segments)")

    # BID fill = sell_taker → 输出为 _sell.csv
    bid_segments = fit_piecewise_exp(depth, c_bid, SEGMENT_EDGES)
    bid_df = pd.DataFrame(bid_segments)
    bid_out = os.path.join(OUTPUT_DIR, f"{pair_name}_sell.csv")
    bid_df.to_csv(bid_out, index=False, header=False)
    print(f"  {pair_name} BID → {bid_out} ({len(bid_segments)} segments)")

    # 打印关键参数
    if ask_segments:
        s0 = ask_segments[0]
        print(f"    ASK seg0: ten_b={s0['ten_b']:.1f}, k={s0['k']:.6f}, "
              f"depth={s0['end']:.0f}")
    if bid_segments:
        s0 = bid_segments[0]
        print(f"    BID seg0: ten_b={s0['ten_b']:.1f}, k={s0['k']:.6f}, "
              f"depth={s0['end']:.0f}")


def main():
    parser = argparse.ArgumentParser(description="Phase 1: 分段指数拟合")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--pair", type=str)
    args = parser.parse_args()

    if args.all:
        tasks = KRAKEN_PAIRS
    elif args.pair:
        tasks = [(args.pair, args.pair.replace("XBT", "BTC").replace("USD", "-USD")
                  .replace("EUR", "-EUR").replace("GBP", "-GBP")
                  .replace("USDT", "-USDT").replace("USDC", "-USDC"))]
        # 更好的映射
        for sd, pn in KRAKEN_PAIRS:
            if sd == args.pair:
                tasks = [(sd, pn)]
                break
    else:
        parser.print_help()
        return

    for sd, pn in tasks:
        process_pair(sd, pn)

    print(f"\nDone. Output: {OUTPUT_DIR}/")


if __name__ == "__main__":
    main()
