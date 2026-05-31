#!/usr/bin/env python3
"""
分析1: DriftTable pnl_bps 日间稳定性
对每个 (symbol, side, level, tau) 统计跨天 pnl 的均值/标准差/正天数比例
用法: python analyze_stability.py
"""

from __future__ import annotations
import os, sys
import numpy as np
import pandas as pd

# 复用现有 drift_table.py 的核心函数
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from drift_table import (
    compute_level_drift, load_book, load_trades, find_files,
    DATA_ROOT, N_LEVELS, TAUS_SEC, SYMBOLS
)

# 覆盖配置 — 用全量数据
DATES = sorted([
    f.split("_")[0] for f in os.listdir(os.path.join(DATA_ROOT, "book_snapshot_25", "XBTUSD"))
    if f.endswith(".csv.gz")
])
print(f"日期: {DATES[0]} ~ {DATES[-1]} ({len(DATES)} 天)")

# 关注的 symbol
FOCUS = {
    "XBTUSD": "BTC/USD",
    "ETHUSD": "ETH/USD",
    "EURUSD": "EUR/USD",
    "GBPUSD": "GBP/USD",
    "SOLUSD": "SOL/USD",
    "XRPUSD": "XRP/USD",
    "USDTUSD": "USDT/USD",
    "USDCUSD": "USDC/USD",
}
FOCUS_TAU = 60.0  # 主要关注 60s 窗口

all_daily = []
for sd, sn in FOCUS.items():
    print(f"\n--- {sn} ({sd}) ---")
    for d in DATES:
        book_f, trade_f, ok = find_files(sd, d)
        if not ok:
            continue
        try:
            bk = load_book(book_f)
            tr = load_trades(trade_f)
            res = compute_level_drift(bk, tr, taus_sec=TAUS_SEC, n_levels=N_LEVELS)
            if not res.empty:
                res["date"] = d
                res["symbol"] = sn
                all_daily.append(res)
                print(f"  {d}: {len(res)} rows", end="")
                # 只看 tau=60 的 pnl>0 档位
                sub = res[(res["tau_sec"] == FOCUS_TAU) & (res["pnl_mid_bps"] > 0)]
                ask_levels = sub[sub["maker_side"] == "ASK"]["level"].tolist()
                bid_levels = sub[sub["maker_side"] == "BID"]["level"].tolist()
                print(f"  ASK+:{ask_levels[:5]}  BID+:{bid_levels[:5]}")
            else:
                print(f"  {d}: no data")
        except Exception as e:
            print(f"  {d}: ERROR {e}")

if not all_daily:
    print("无数据!"); sys.exit(1)

combined = pd.concat(all_daily, ignore_index=True)

# ===== 稳定性统计 =====
print(f"\n{'='*120}")
print(f"  分析1: PnL 日间稳定性 (tau={FOCUS_TAU}s, {len(DATES)}天)")
print(f"{'='*120}")

# 按 (symbol, maker_side, level) 聚合
gp = combined[combined["tau_sec"] == FOCUS_TAU].groupby(
    ["symbol", "maker_side", "level"], as_index=False
)

stats = gp.agg(
    pnl_mean=("pnl_mid_bps", "mean"),
    pnl_std=("pnl_mid_bps", "std"),
    pnl_min=("pnl_mid_bps", "min"),
    pnl_max=("pnl_mid_bps", "max"),
    pnl_p25=("pnl_mid_bps", lambda x: np.percentile(x, 25)),
    pnl_p75=("pnl_mid_bps", lambda x: np.percentile(x, 75)),
    cap_mean=("cap_mid_bps", "mean"),
    drift_mean=("drift_mid_bps", "mean"),
    n_days=("date", "nunique"),
    total_fills=("fill_count", "sum"),
    avg_notional=("total_notional", "mean"),
)
stats["fills_per_day"] = stats["total_fills"] / stats["n_days"]
stats["pnl_cv"] = stats["pnl_std"] / stats["pnl_mean"].abs()  # 变异系数
stats["pnl_snr"] = stats["pnl_mean"] / (stats["pnl_std"] + 1e-9)  # 信噪比
stats["pos_frac"] = np.nan  # 正天数占比, 后面填充

# 逐 (symbol, side, level) 计算正天数占比
pos_rows = []
for (sym, side, lv), grp in combined[combined["tau_sec"] == FOCUS_TAU].groupby(
    ["symbol", "maker_side", "level"]
):
    pos_frac = (grp["pnl_mid_bps"] > 0).mean()
    pos_rows.append({"symbol": sym, "maker_side": side, "level": lv, "pos_frac": pos_frac})
pos_df = pd.DataFrame(pos_rows)
stats = stats.drop(columns=["pos_frac"], errors="ignore").merge(
    pos_df, on=["symbol", "maker_side", "level"], how="left"
)

# 按 symbol 分别打印
for sym in FOCUS.values():
    sub = stats[stats["symbol"] == sym].copy()
    if sub.empty:
        continue

    # 稳定盈利: pnl_mean > 0, pos_frac >= 0.7, n_days >= 10
    stable_ask = sub[(sub["maker_side"] == "ASK") & (sub["pnl_mean"] > 0) & (sub["pos_frac"] >= 0.7)]
    stable_bid = sub[(sub["maker_side"] == "BID") & (sub["pnl_mean"] > 0) & (sub["pos_frac"] >= 0.7)]

    print(f"\n{'='*100}")
    print(f"  {sym}")
    print(f"  稳定盈利档位 (pnl_mean>0, pos_frac>=0.7):")
    print(f"    ASK: {sorted(stable_ask['level'].tolist())}")
    print(f"    BID: {sorted(stable_bid['level'].tolist())}")

    # 最佳档位 — pnl_mean × fills_per_day 最大
    sub["score"] = sub["pnl_mean"] * sub["fills_per_day"]
    best_ask = sub[sub["maker_side"] == "ASK"].nlargest(5, "score")
    best_bid = sub[sub["maker_side"] == "BID"].nlargest(5, "score")

    print(f"\n  {'Level':>5} {'Side':>4} {'pnl_mean':>8} {'pnl_std':>8} {'SNR':>6} {'pos_frac':>8} {'fills/d':>8} {'cap_mean':>8} {'drift_mean':>10}")
    print(f"  {'-'*5} {'-'*4} {'-'*8} {'-'*8} {'-'*6} {'-'*8} {'-'*8} {'-'*8} {'-'*10}")

    # 打印 pnl_mean>0 且 fills/d>5 的档位
    good = sub[(sub["pnl_mean"] > 0) & (sub["fills_per_day"] > 5)].copy()
    good = good.sort_values("score", ascending=False)
    for _, r in good.iterrows():
        print(f"  {int(r['level']):>5} {r['maker_side']:>4} "
              f"{r['pnl_mean']:>+8.2f} {r['pnl_std']:>8.2f} {r['pnl_snr']:>6.1f} "
              f"{r['pos_frac']:>8.0%} {r['fills_per_day']:>8.0f} "
              f"{r['cap_mean']:>+8.2f} {r['drift_mean']:>+10.2f}")

# ===== 跨 symbol 汇总 =====
print(f"\n{'='*120}")
print(f"  全局: 按币种汇总 — '甜点区' (pnl_mean>0, fills/d>5, pos_frac>=0.5)")
print(f"{'='*120}")
print(f"  {'Symbol':<10} {'Side':>4} {'甜点档位':>20} {'最佳level':>8} {'最佳pnl':>8} {'fills/d':>8}")
print(f"  {'-'*10} {'-'*4} {'-'*20} {'-'*8} {'-'*8} {'-'*8}")

for sym in FOCUS.values():
    sub = stats[stats["symbol"] == sym]
    for side in ["ASK", "BID"]:
        ss = sub[(sub["maker_side"] == side) & (sub["pnl_mean"] > 0) & (sub["fills_per_day"] > 5) & (sub["pos_frac"] >= 0.5)]
        if ss.empty:
            continue
        sweet = sorted(ss["level"].tolist())
        best = ss.loc[ss["pnl_mean"].idxmax()]
        sweet_str = str(sweet[:6]) + ("..." if len(sweet) > 6 else "")
        print(f"  {sym:<10} {side:>4} {sweet_str:>20} {int(best['level']):>8} {best['pnl_mean']:>+8.2f} {best['fills_per_day']:>8.0f}")

# 保存详细数据
stats.to_csv("python/analysis1_stability.csv", index=False)
print(f"\n详细数据: python/analysis1_stability.csv")
