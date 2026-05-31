#!/usr/bin/env python3
"""批量验证: 多pair×多天 ranking 对比"""
import os, sys, numpy as np, pandas as pd
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validate_ranking import simulate_ranking, load_efu_model, find_files
from markout_eval import load_book, load_trades

PAIRS = {
    "XBTUSD": "BTC-USD", "ETHUSD": "ETH-USD", "EURUSD": "EUR-USD",
    "SOLUSD": "SOL-USD", "XRPUSD": "XRP-USD",
}
DATES = [d for d in os.listdir("/data/kraken/book_snapshot_25/XBTUSD")
         if d.endswith(".csv.gz")][:10]  # 前10天

all_results = []
for sd, pn in PAIRS.items():
    # 加载 EFU 模型
    ask_m, bid_m = load_efu_model(pn)
    has_efu = ask_m is not None and bid_m is not None
    if not has_efu:
        print(f"SKIP {pn}: no EFU model")
        continue

    # 加载 DriftTable
    base = sd.lower()
    drift_path = f"drift_tables/drift_{base}.csv"
    if not os.path.exists(drift_path):
        print(f"  Generating drift table for {sd}...")
        dates_str = ",".join(d.replace(f"_{sd}.csv.gz", "") for d in DATES)
        os.system(f"python3 python/drift_table.py --symbol {sd} "
                  f"--date {dates_str} 2>/dev/null | tail -3")
    drift_df = pd.read_csv(drift_path) if os.path.exists(drift_path) else None

    for d_file in DATES:
        date = d_file.replace(f"_{sd}.csv.gz", "")
        book_f, trade_f, ok = find_files(sd, date)
        if not ok:
            continue
        try:
            bk = load_book(book_f)
            tr = load_trades(trade_f)
            df_old, df_new = simulate_ranking(bk, tr, drift_df, ask_m, bid_m)
            if df_old.empty:
                continue

            # 只看成交的
            old_f = df_old[df_old["filled"]]
            new_f = df_new[df_new["filled"]]

            all_results.append({
                "pair": pn, "date": date,
                "old_fills": len(old_f),
                "new_fills": len(new_f),
                "old_markout": old_f["markout_60s_bps"].mean(),
                "new_markout": new_f["markout_60s_bps"].mean(),
                "old_levels_mean": df_old["level"].mean(),
                "new_levels_mean": df_new["level"].mean(),
            })
            print(f"{pn} {date}: OLD fills={len(old_f)} mo={old_f['markout_60s_bps'].mean():+.2f} "
                  f"NEW fills={len(new_f)} mo={new_f['markout_60s_bps'].mean():+.2f}")
        except Exception as e:
            print(f"{pn} {date}: ERROR {e}")

# 汇总
df_all = pd.DataFrame(all_results)
if df_all.empty:
    print("No results")
    sys.exit(1)

print(f"\n{'='*80}")
print(f"  汇总: {len(df_all)} 天 × {df_all['pair'].nunique()} pairs")
print(f"{'='*80}")

for pn in df_all["pair"].unique():
    sub = df_all[df_all["pair"] == pn]
    print(f"\n  {pn} ({len(sub)} days):")
    print(f"    OLD: fills={sub['old_fills'].mean():.0f}/d, markout={sub['old_markout'].mean():+.3f} bps, "
          f"avg level={sub['old_levels_mean'].mean():.1f}")
    print(f"    NEW: fills={sub['new_fills'].mean():.0f}/d, markout={sub['new_markout'].mean():+.3f} bps, "
          f"avg level={sub['new_levels_mean'].mean():.1f}")
    delta_mo = sub["new_markout"].mean() - sub["old_markout"].mean()
    delta_fills = (sub["new_fills"].mean() - sub["old_fills"].mean()) / sub["old_fills"].mean() * 100
    print(f"    Δmarkout: {delta_mo:+.3f} bps, Δfills: {delta_fills:+.1f}%")

# 全局
print(f"\n  === 全局 ===")
print(f"  OLD markout: {df_all['old_markout'].mean():+.3f} bps")
print(f"  NEW markout: {df_all['new_markout'].mean():+.3f} bps")
print(f"  Δmarkout: {df_all['new_markout'].mean()-df_all['old_markout'].mean():+.3f} bps")
print(f"  OLD fills/d: {df_all['old_fills'].mean():.0f}")
print(f"  NEW fills/d: {df_all['new_fills'].mean():.0f}")
