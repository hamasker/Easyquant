#!/usr/bin/env python3
"""
批量 OB+vol 做市 markout 评估

配置区在最下面, 直接改 SYMBOLS / SPREADS / DATES 即可。

用法:
  python batch_markout.py              # 跑配置里的所有组合
  python batch_markout.py --today      # 只跑今天
  python batch_markout.py --dry-run    # 只看会跑哪些, 不实际跑
"""

import argparse
import os
import sys
from datetime import datetime
from concurrent.futures import ProcessPoolExecutor, as_completed

import numpy as np
import pandas as pd

# 导入 markout 核心函数
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from markout_eval import load_book, load_trades, eval_markout


# ═══════════════════════════════════════════════════════
# 配置区 — 直接改这里
# ═══════════════════════════════════════════════════════

# 数据根路径 (book_snapshot_25 和 trades 的公共父目录)
DATA_ROOT = "/data/kraken"

# 币对: {显示名: book子目录名}
# book_snapshot_25 和 trades 下都有对应的子目录
SYMBOLS = {
    "EUR/USD": "EURUSD",
    "GBP/USD": "GBPUSD",
    # "AUD/USD": "AUDUSD",
    # "CHF/USD": "CHFUSD",
    # "USDT/USD": "USDTUSD",
    # "USDC/USD": "USDCUSD",
    # "BTC/USD": "BTCUSD",
}

# 做市半价差 (bps) — 跑多个对比
SPREADS = [1, 2, 3, 5]

# 日期列表 (YYYY-MM-DD)
DATES = [
    "2026-03-09", "2026-03-10", "2026-03-11",
    "2026-03-12", "2026-03-13", "2026-03-14",
    "2026-03-15", "2026-03-16", "2026-03-17",
    "2026-03-18", "2026-03-19", "2026-03-20",
    "2026-03-21", "2026-03-22", "2026-03-23",
]

# markout 时间窗口 (秒)
TAUS = [0.1, 0.5, 1.0, 5.0, 10.0, 30.0, 60.0, 300.0]

# OB 深度档数
N_LEVELS = 25

# 输出目录
OUTPUT_DIR = "markout_results"

# 并行进程数 (None = 自动)
MAX_WORKERS = 4

# ═══════════════════════════════════════════════════════


def find_files(symbol_dir: str, date: str):
    """根据 symbol 和 date 找到 book 和 trade 文件路径。"""
    book_dir = os.path.join(DATA_ROOT, "book_snapshot_25", symbol_dir)
    trade_dir = os.path.join(DATA_ROOT, "trades", symbol_dir)

    book_file = os.path.join(book_dir, f"{date}_{symbol_dir}.csv.gz")
    trade_file = os.path.join(trade_dir, f"{date}_{symbol_dir}.csv.gz")

    ok = os.path.exists(book_file) and os.path.exists(trade_file)
    return book_file, trade_file, ok


def run_one(symbol_name: str, symbol_dir: str, date: str, hs: float):
    """跑单个 (symbol, date, spread) 组合, 返回 DataFrame 或 None。"""
    book_f, trade_f, ok = find_files(symbol_dir, date)
    if not ok:
        return None

    try:
        df_book = load_book(book_f)
        df_trade = load_trades(trade_f)
        result = eval_markout(df_book, df_trade, half_spread_bps=hs,
                              taus_sec=TAUS, n_levels=N_LEVELS)
        if result is None or result.empty:
            return None

        result["symbol"] = symbol_name
        result["date"] = date
        result["half_spread_bps"] = hs
        # 按 notional 加权聚合到 (symbol, date, spread, side, tau) 级别
        agg = result.groupby(["symbol", "date", "half_spread_bps", "maker_side", "tau_sec"], as_index=False).apply(
            lambda g: pd.Series({
                "drift_mid_bps": np.average(g["drift_mid_bps"], weights=g["notional"]),
                "cap_mid_bps": np.average(g["cap_mid_bps"], weights=g["notional"]),
                "pnl_mid_bps": np.average(g["pnl_mid_bps"], weights=g["notional"]),
                "fill_count": g["fill_count"].sum(),
                "total_notional": g["notional"].sum(),
            })
        )
        return agg
    except Exception as e:
        print(f"  ⚠ {symbol_name} {date} spread={hs}bps: {e}")
        return None


def print_table(rows: list):
    """打印汇总表。"""
    if not rows:
        print("无数据")
        return

    df = pd.DataFrame(rows)
    # 按 symbol + spread 汇总所有天的加权平均
    summary = df.groupby(["symbol", "half_spread_bps", "tau_sec"], as_index=False).apply(
        lambda g: pd.Series({
            "pnl_mid_bps": np.average(g["pnl_mid_bps"], weights=g["total_notional"]),
            "avg_fills_per_day": g["fill_count"].sum() / g["date"].nunique(),
        })
    )

    print(f"\n{'='*90}")
    print(f"  Markout 评估汇总 (加权平均, {df['date'].nunique()} 天)")
    print(f"{'='*90}")
    print(f"{'Symbol':<10} {'Spread':>7} {'tau=0.1s':>9} {'tau=1s':>9} {'tau=5s':>9} {'tau=60s':>9} {'tau=300s':>9}  {'fills/天':>9}")
    print(f"{'-'*10} {'-'*7} {'-'*9} {'-'*9} {'-'*9} {'-'*9} {'-'*9}  {'-'*9}")

    for sym in sorted(summary["symbol"].unique()):
        for hs in sorted(summary["half_spread_bps"].unique()):
            sub = summary[(summary["symbol"] == sym) & (summary["half_spread_bps"] == hs)]
            vals = []
            for t in [0.1, 1.0, 5.0, 60.0, 300.0]:
                s = sub[sub["tau_sec"] == t]
                vals.append(f"{s['pnl_mid_bps'].iloc[0]:+.2f}" if len(s) else "   N/A")
            fills = sub["avg_fills_per_day"].max()
            print(f"{sym:<10} {hs:>5}bps {vals[0]:>9} {vals[1]:>9} {vals[2]:>9} {vals[3]:>9} {vals[4]:>9}  {fills:>9.0f}")


def main():
    parser = argparse.ArgumentParser(description="批量 markout 评估")
    parser.add_argument("--today", action="store_true", help="只跑今天")
    parser.add_argument("--dry-run", action="store_true", help="只显示计划, 不跑")
    args = parser.parse_args()

    dates = [datetime.now().strftime("%Y-%m-%d")] if args.today else DATES
    tasks = [(sn, sd, d, hs) for (sn, sd) in SYMBOLS.items() for d in dates for hs in SPREADS]

    # 过滤有效文件
    valid_tasks = []
    for sn, sd, d, hs in tasks:
        _, _, ok = find_files(sd, d)
        if ok:
            valid_tasks.append((sn, sd, d, hs))

    print(f"计划运行 {len(valid_tasks)} 个任务 ({len(SYMBOLS)} symbols × {len(dates)} dates × {len(SPREADS)} spreads)")
    if args.dry_run:
        for sn, sd, d, hs in valid_tasks[:20]:
            print(f"  {sn:10s} {d} spread={hs}bps")
        if len(valid_tasks) > 20:
            print(f"  ... 还有 {len(valid_tasks)-20} 个")
        return

    # 运行
    all_rows = []
    n_done = 0
    with ProcessPoolExecutor(max_workers=MAX_WORKERS) as ex:
        futures = {ex.submit(run_one, sn, sd, d, hs): (sn, d, hs) for sn, sd, d, hs in valid_tasks}
        for f in as_completed(futures):
            sn, d, hs = futures[f]
            try:
                res = f.result()
                if res is not None:
                    all_rows.append(res)
                n_done += 1
                if n_done % 10 == 0:
                    print(f"  进度: {n_done}/{len(valid_tasks)}")
            except Exception as e:
                print(f"  ⚠ {sn} {d} spread={hs}bps: {e}")

    # 保存
    if all_rows:
        os.makedirs(OUTPUT_DIR, exist_ok=True)
        combined = pd.concat(all_rows, ignore_index=True)
        out_path = os.path.join(OUTPUT_DIR, f"markout_batch_{datetime.now():%Y%m%d_%H%M}.csv")
        combined.to_csv(out_path, index=False)
        print(f"\n详细数据: {out_path}")

        # 汇总行
        summary_rows = []
        for (sn, hs, tau), grp in combined.groupby(["symbol", "half_spread_bps", "tau_sec"]):
            summary_rows.append({
                "symbol": sn, "half_spread_bps": hs, "tau_sec": tau,
                "pnl_mid_bps": np.average(grp["pnl_mid_bps"], weights=grp["total_notional"]),
                "cap_mid_bps": np.average(grp["cap_mid_bps"], weights=grp["total_notional"]),
                "drift_mid_bps": np.average(grp["drift_mid_bps"], weights=grp["total_notional"]),
                "avg_fills_per_day": grp["fill_count"].sum() / grp["date"].nunique(),
                "days": grp["date"].nunique(),
            })
        print_table(summary_rows)
    else:
        print("无有效结果")


if __name__ == "__main__":
    main()
