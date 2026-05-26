"""
OB 档位 drift 表生成器

逐 OB 档位统计: 在该档位被成交后, 持有 tau 秒的 cap / drift / pnl。
输出可直接用于下单决策: 查表 → net_edge = cap - drift → 决定挂不挂、挂多少。

用法:
  # 单币种单日
  python drift_table.py --symbol XBTUSD --date 2026-03-09

  # 批量多日
  python drift_table.py --symbol XBTUSD --date 2026-03-09,2026-03-10,2026-03-11

  # 全部币种全部日期
  python drift_table.py --all

输出:
  drift_tables/drift_<symbol>.csv            # 全量聚合表
  drift_tables/drift_<symbol>_daily.csv      # 逐日明细 (调试用)
  drift_tables/drift_<symbol>_by_vol.csv     # 按 vol 分层

设计原则:
  - 不依赖 FP, 纯 OB 档位分桶 (dist = log(price / mid_OB))
  - 区分 BID/ASK, 每档独立统计
  - 多 tau 窗口
  - 支持 vol 分层
"""

# pyright: reportUnknownMemberType=false, reportUnknownArgumentType=false, reportUnknownVariableType=false, reportAny=false, reportUnnecessaryCast=false

from __future__ import annotations

import argparse
import os
import sys
from collections.abc import Hashable, Sequence
from dataclasses import dataclass
from typing import TypedDict, cast

import numpy as np
import pandas as pd
from numpy.typing import NDArray

# 复用 markout_eval 的数据加载函数 (脚本直跑时用绝对 import)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from markout_eval import load_book, load_trades  # noqa: E402  # pyright: ignore[reportImplicitRelativeImport]

Float64Array = NDArray[np.float64]
Int64Array = NDArray[np.int64]
Int8Array = NDArray[np.int8]

# ═══════════════════════════════════════════════════════════
# 类型
# ═══════════════════════════════════════════════════════════


class LevelDriftRow(TypedDict):
    maker_side: str
    level: int
    tau_sec: float
    avg_dist_bps: float
    cap_mid_bps: float
    drift_mid_bps: float
    pnl_mid_bps: float
    fill_count: int
    total_notional: float


class VolInfoRow(TypedDict):
    date: str
    daily_vol_bps: float


@dataclass(frozen=True)
class CliArgs:
    symbol: str | None
    date: str | None
    all_symbols: bool
    print_levels: str | None


# ═══════════════════════════════════════════════════════════
# 工具
# ═══════════════════════════════════════════════════════════


def asof_idx(sorted_ts: Int64Array, query_ts: Int64Array) -> Int64Array:
    """每个 query_ts 找到 ≤ 它 的最大 sorted_ts 索引。超出返回 -1。"""
    idx = np.searchsorted(sorted_ts, query_ts, side="right") - 1
    if len(sorted_ts) > 0 and len(query_ts) > 0:
        idx[(query_ts < sorted_ts[0]) | (query_ts > sorted_ts[-1])] = -1
    return idx


def _col_i64(df: pd.DataFrame, name: str) -> Int64Array:
    return np.asarray(df[name], dtype=np.int64)


def _col_i8(df: pd.DataFrame, name: str) -> Int8Array:
    return np.asarray(df[name], dtype=np.int8)


def _col_f64(df: pd.DataFrame, name: str) -> Float64Array:
    return np.asarray(df[name], dtype=np.float64)


# ═══════════════════════════════════════════════════════════
# 配置
# ═══════════════════════════════════════════════════════════

DATA_ROOT = "/data/kraken"
OUTPUT_DIR = "drift_tables"

TAUS_SEC = (0.1, 0.5, 1.0, 5.0, 10.0, 30.0, 60.0, 300.0)
N_LEVELS = 25

# 常用币种
SYMBOLS: dict[str, str] = {
    "XBTUSD": "BTC/USD",
    "ETHUSD": "ETH/USD",
    "SOLUSD": "SOL/USD",
    "XRPUSD": "XRP/USD",
    "EURUSD": "EUR/USD",
    "GBPUSD": "GBP/USD",
    "USDTUSD": "USDT/USD",
}


# ═══════════════════════════════════════════════════════════
# 核心: 逐档位 drift 计算
# ═══════════════════════════════════════════════════════════


def compute_level_drift(
    df_book: pd.DataFrame,
    df_trade: pd.DataFrame,
    taus_sec: Sequence[float] = TAUS_SEC,
    n_levels: int = N_LEVELS,
) -> pd.DataFrame:
    """
    对每个 OB 档位 (0 ~ n_levels-1), 统计该档位被成交后的 cap / drift / pnl。

    与 eval_markout 的区别:
      - 不做 spread 过滤 (不假设你在哪个档位下单)
      - 对 OB 每一档独立统计: level 0 = best bid/ask, level 1 = second, ...
      - 输出可以直接查: "挂在 level i → 期望 PnL 是多少"

    Returns:
        DataFrame: level, maker_side, tau_sec, cap_bps, drift_bps, pnl_bps, fill_count, notional
    """
    bk_ts = _col_i64(df_book, "ts_ns")

    bid_p_list = [_col_f64(df_book, f"bid_{i}") for i in range(n_levels)]
    bid_q_list = [_col_f64(df_book, f"bid_q_{i}") for i in range(n_levels)]
    ask_p_list = [_col_f64(df_book, f"ask_{i}") for i in range(n_levels)]
    ask_q_list = [_col_f64(df_book, f"ask_q_{i}") for i in range(n_levels)]

    bid0, ask0 = bid_p_list[0], ask_p_list[0]
    mid0 = 0.5 * (bid0 + ask0)

    t_ts = _col_i64(df_trade, "ts_ns")
    t_side = _col_i8(df_trade, "side_int")
    t_qty = _col_f64(df_trade, "trade_qty")

    t_bk_idx = asof_idx(bk_ts, t_ts)
    ok = t_bk_idx >= 0
    t_bk_idx = t_bk_idx[ok]
    t_side = t_side[ok]
    t_qty = t_qty[ok]

    n_bk = len(bk_ts)
    buy_w = cast(Float64Array, np.where(t_side == 1, t_qty, 0.0))
    sell_w = cast(Float64Array, np.where(t_side == -1, t_qty, 0.0))
    Q_buy = np.bincount(t_bk_idx, weights=buy_w, minlength=n_bk).astype(np.float64)
    Q_sell = np.bincount(t_bk_idx, weights=sell_w, minlength=n_bk).astype(np.float64)

    active = (mid0 > 0) & ((Q_buy > 0) | (Q_sell > 0))
    act_idx = np.flatnonzero(active)
    if act_idx.size == 0:
        return pd.DataFrame()

    taus_ns = [int(round(t * 1e9)) for t in taus_sec]
    results: list[LevelDriftRow] = []

    block = 100_000
    for s0 in range(0, act_idx.size, block):
        s1 = min(act_idx.size, s0 + block)
        idx = act_idx[s0:s1]

        t0 = bk_ts[idx]
        m0 = mid0[idx]
        Qb = Q_buy[idx]
        Qs = Q_sell[idx]

        for level in range(n_levels):
            ask_p_l = ask_p_list[level][idx]
            ask_q_l = ask_q_list[level][idx]

            cum_prev = np.zeros(len(idx), dtype=np.float64)
            for prev_l in range(level):
                cum_prev += ask_q_list[prev_l][idx]
            rem = np.maximum(0.0, Qb - cum_prev)
            fill = np.minimum(ask_q_l, rem)
            notional = fill * ask_p_l

            valid = (fill > 0) & (ask_p_l > 0) & np.isfinite(ask_p_l)
            if not np.any(valid):
                continue

            v_idx = np.flatnonzero(valid)
            fill_px = ask_p_l[v_idx]
            fill_notional = notional[v_idx]
            fill_mid = m0[v_idx]
            cap = (fill_px - fill_mid) / fill_px
            dist_bps = np.log(fill_px / fill_mid) * 1e4

            for j, tau_ns in enumerate(taus_ns):
                t1 = t0[v_idx] + tau_ns
                idx_mid1 = asof_idx(bk_ts, t1)
                ok1 = idx_mid1 >= 0
                if not np.any(ok1):
                    continue

                mid1 = np.full(len(v_idx), np.nan, dtype=np.float64)
                mid1[ok1] = mid0[idx_mid1[ok1]]
                drift = (mid1 - fill_mid) / fill_px

                ok_fin = ok1 & np.isfinite(drift)
                if not np.any(ok_fin):
                    continue

                w = fill_notional[ok_fin]
                results.append(
                    LevelDriftRow(
                        maker_side="ASK",
                        level=level,
                        tau_sec=float(taus_sec[j]),
                        avg_dist_bps=float(np.average(dist_bps[ok_fin], weights=w)),
                        cap_mid_bps=float(np.average(cap[ok_fin] * 1e4, weights=w)),
                        drift_mid_bps=float(np.average(drift[ok_fin] * 1e4, weights=w)),
                        pnl_mid_bps=float(
                            np.average((cap[ok_fin] - drift[ok_fin]) * 1e4, weights=w)
                        ),
                        fill_count=int(np.sum(ok_fin)),
                        total_notional=float(np.sum(w)),
                    )
                )

        for level in range(n_levels):
            bid_p_l = bid_p_list[level][idx]
            bid_q_l = bid_q_list[level][idx]

            cum_prev = np.zeros(len(idx), dtype=np.float64)
            for prev_l in range(level):
                cum_prev += bid_q_list[prev_l][idx]
            rem = np.maximum(0.0, Qs - cum_prev)
            fill = np.minimum(bid_q_l, rem)
            notional = fill * bid_p_l

            valid = (fill > 0) & (bid_p_l > 0) & np.isfinite(bid_p_l)
            if not np.any(valid):
                continue

            v_idx = np.flatnonzero(valid)
            fill_px = bid_p_l[v_idx]
            fill_notional = notional[v_idx]
            fill_mid = m0[v_idx]
            cap = -(fill_px - fill_mid) / fill_px
            dist_bps = np.log(fill_mid / fill_px) * 1e4

            for j, tau_ns in enumerate(taus_ns):
                t1 = t0[v_idx] + tau_ns
                idx_mid1 = asof_idx(bk_ts, t1)
                ok1 = idx_mid1 >= 0
                if not np.any(ok1):
                    continue

                mid1 = np.full(len(v_idx), np.nan, dtype=np.float64)
                mid1[ok1] = mid0[idx_mid1[ok1]]
                drift = -(mid1 - fill_mid) / fill_px

                ok_fin = ok1 & np.isfinite(drift)
                if not np.any(ok_fin):
                    continue

                w = fill_notional[ok_fin]
                results.append(
                    LevelDriftRow(
                        maker_side="BID",
                        level=level,
                        tau_sec=float(taus_sec[j]),
                        avg_dist_bps=float(np.average(dist_bps[ok_fin], weights=w)),
                        cap_mid_bps=float(np.average(cap[ok_fin] * 1e4, weights=w)),
                        drift_mid_bps=float(np.average(drift[ok_fin] * 1e4, weights=w)),
                        pnl_mid_bps=float(
                            np.average((cap[ok_fin] - drift[ok_fin]) * 1e4, weights=w)
                        ),
                        fill_count=int(np.sum(ok_fin)),
                        total_notional=float(np.sum(w)),
                    )
                )

    return pd.DataFrame(results)


# ═══════════════════════════════════════════════════════════
# 批量和聚合
# ═══════════════════════════════════════════════════════════


def find_files(symbol_dir: str, date: str) -> tuple[str, str, bool]:
    book_f = os.path.join(
        DATA_ROOT, "book_snapshot_25", symbol_dir, f"{date}_{symbol_dir}.csv.gz"
    )
    trade_f = os.path.join(
        DATA_ROOT, "trades", symbol_dir, f"{date}_{symbol_dir}.csv.gz"
    )
    return book_f, trade_f, os.path.exists(book_f) and os.path.exists(trade_f)


def compute_daily_vol(symbol_dir: str, date: str) -> float | None:
    """计算日波动率 (1min return std, bps)"""
    book_f, _, ok = find_files(symbol_dir, date)
    if not ok:
        return None
    try:
        bk = load_book(book_f)
        mid = 0.5 * (_col_f64(bk, "bid0") + _col_f64(bk, "ask0"))
        step = 300  # ~1min
        rets = np.diff(np.log(mid[::step]))
        if len(rets) > 10:
            return float(np.std(rets) * 10000)
    except Exception:
        pass
    return None


def _aggregate_grouped(df: pd.DataFrame, group_cols: list[str]) -> pd.DataFrame:
    rows: list[dict[str, Hashable | float | int]] = []
    for key, g in df.groupby(group_cols, sort=False):
        if len(group_cols) == 1:
            keys: tuple[Hashable, ...] = (cast(Hashable, key),)
        else:
            keys = cast(tuple[Hashable, ...], key)
        w = _col_f64(g, "total_notional")
        row: dict[str, Hashable | float | int] = {
            col: val for col, val in zip(group_cols, keys, strict=False)
        }
        fill_count_arr = np.asarray(g["fill_count"], dtype=np.int64)
        total_notional_arr = _col_f64(g, "total_notional")
        row.update(
            {
                "avg_dist_bps": float(np.average(_col_f64(g, "avg_dist_bps"), weights=w)),
                "cap_mid_bps": float(np.average(_col_f64(g, "cap_mid_bps"), weights=w)),
                "drift_mid_bps": float(
                    np.average(_col_f64(g, "drift_mid_bps"), weights=w)
                ),
                "pnl_mid_bps": float(np.average(_col_f64(g, "pnl_mid_bps"), weights=w)),
                "fill_count": int(fill_count_arr.sum()),
                "total_notional": float(total_notional_arr.sum()),
                "n_days": int(g["date"].nunique()),
            }
        )
        rows.append(row)
    return pd.DataFrame(rows)


def run_symbol_dates(
    symbol_dir: str, symbol_name: str, dates: list[str]
) -> tuple[pd.DataFrame, pd.DataFrame | None] | tuple[None, None]:
    """跑一个币种的多天数据, 输出聚合表和逐日表。"""
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    all_daily: list[pd.DataFrame] = []
    vol_info: list[VolInfoRow] = []

    for d in dates:
        book_f, trade_f, ok = find_files(symbol_dir, d)
        if not ok:
            print(f"  ⚠ {d} 文件缺失, 跳过")
            continue

        print(f"  {d} ...", end=" ", flush=True)
        try:
            df_book = load_book(book_f)
            df_trade = load_trades(trade_f)
            res = compute_level_drift(df_book, df_trade)
            if not res.empty:
                res["date"] = d
                res["symbol"] = symbol_name
                all_daily.append(res)
                print(f"ok ({len(res)} 行)")
            else:
                print("无有效数据")

            vol = compute_daily_vol(symbol_dir, d)
            if vol is not None:
                vol_info.append(VolInfoRow(date=d, daily_vol_bps=vol))
        except Exception as e:
            print(f"失败: {e}")

    if not all_daily:
        print(f"  ✗ {symbol_name}: 无有效数据")
        return None, None

    combined = pd.concat(all_daily, ignore_index=True)

    agg = _aggregate_grouped(combined, ["symbol", "maker_side", "level", "tau_sec"])
    agg["fills_per_day"] = agg["fill_count"] / agg["n_days"]

    vol_agg: pd.DataFrame | None = None
    if vol_info:
        vol_df = pd.DataFrame(vol_info)
        if len(vol_df) >= 5:
            lo = float(vol_df["daily_vol_bps"].quantile(0.33))
            hi = float(vol_df["daily_vol_bps"].quantile(0.67))
            vol_df["vol_regime"] = "med"
            vol_df.loc[vol_df["daily_vol_bps"] <= lo, "vol_regime"] = "low"
            vol_df.loc[vol_df["daily_vol_bps"] >= hi, "vol_regime"] = "high"

            combined_vol = pd.merge(combined, vol_df[["date", "vol_regime"]], on="date")
            vol_agg = _aggregate_grouped(
                combined_vol,
                ["symbol", "vol_regime", "maker_side", "level", "tau_sec"],
            )

    base = symbol_dir.lower()
    agg.to_csv(f"{OUTPUT_DIR}/drift_{base}.csv", index=False)
    combined.to_csv(f"{OUTPUT_DIR}/drift_{base}_daily.csv", index=False)
    if vol_agg is not None:
        vol_agg.to_csv(f"{OUTPUT_DIR}/drift_{base}_by_vol.csv", index=False)

    print(f"  → drift_{base}.csv ({len(agg)} 行聚合)")
    print(f"  → drift_{base}_daily.csv ({len(combined)} 行逐日)")
    if vol_agg is not None:
        print(f"  → drift_{base}_by_vol.csv ({len(vol_agg)} 行按vol)")

    return agg, vol_agg


def _row_float(row: pd.Series, col: str) -> float:
    return float(cast(float | np.floating, row[col]))


def print_level_grid(agg: pd.DataFrame, symbol_name: str, tau: float = 60.0) -> None:
    """打印单币种单 tau 的档位-PnL 网格, 方便手动调试。"""
    sub = agg[(agg["symbol"] == symbol_name) & (agg["tau_sec"] == tau)]
    if sub.empty:
        return

    print(f"\n{'=' * 100}")
    print(f"  {symbol_name} — 档位 drift 表 (tau={tau}s)")
    print(f"{'=' * 100}")
    header = (
        f"{'Level':>5}  {'ASK dist':>8} {'ASK cap':>8} {'ASK drift':>9} {'ASK pnl':>8}  |  "
        + f"{'BID dist':>8} {'BID cap':>8} {'BID drift':>9} {'BID pnl':>8}  {'fills/d':>8}"
    )
    print(header)
    sep = (
        f"{'-' * 5}  {'-' * 8} {'-' * 8} {'-' * 9} {'-' * 8}  |  "
        + f"{'-' * 8} {'-' * 8} {'-' * 9} {'-' * 8}  {'-' * 8}"
    )
    print(sep)

    level_vals = np.asarray(sub["level"], dtype=np.int64)
    unique_levels = np.unique(level_vals)
    levels = sorted(int(lv) for lv in unique_levels.astype(int).tolist())
    for lv in levels:
        ask = sub[(sub["level"] == lv) & (sub["maker_side"] == "ASK")]
        bid = sub[(sub["level"] == lv) & (sub["maker_side"] == "BID")]
        a = ask.iloc[0] if not ask.empty else None
        b = bid.iloc[0] if not bid.empty else None
        a_pnl = f"{_row_float(a, 'pnl_mid_bps'):+.2f}" if a is not None else "   N/A"
        a_cap = f"{_row_float(a, 'cap_mid_bps'):+.2f}" if a is not None else "   N/A"
        a_drift = f"{_row_float(a, 'drift_mid_bps'):+.2f}" if a is not None else "    N/A"
        a_dist = f"{_row_float(a, 'avg_dist_bps'):.1f}" if a is not None else "    N/A"
        b_pnl = f"{_row_float(b, 'pnl_mid_bps'):+.2f}" if b is not None else "   N/A"
        b_cap = f"{_row_float(b, 'cap_mid_bps'):+.2f}" if b is not None else "   N/A"
        b_drift = f"{_row_float(b, 'drift_mid_bps'):+.2f}" if b is not None else "    N/A"
        b_dist = f"{_row_float(b, 'avg_dist_bps'):.1f}" if b is not None else "    N/A"
        a_fills = _row_float(a, "fills_per_day") if a is not None else 0.0
        b_fills = _row_float(b, "fills_per_day") if b is not None else 0.0
        fills = f"{max(a_fills, b_fills):.0f}"
        line = (
            f"{lv:>5}  {a_dist:>8} {a_cap:>8} {a_drift:>9} {a_pnl:>8}  |  "
            + f"{b_dist:>8} {b_cap:>8} {b_drift:>9} {b_pnl:>8}  {fills:>8}"
        )
        print(line)

    pos_ask = sub[(sub["maker_side"] == "ASK") & (sub["pnl_mid_bps"] > 0)][
        "level"
    ].tolist()
    pos_bid = sub[(sub["maker_side"] == "BID") & (sub["pnl_mid_bps"] > 0)][
        "level"
    ].tolist()
    summary = (
        f"\n  PnL>0 档位: ASK={pos_ask[:6]}{'...' if len(pos_ask) > 6 else ''}  "
        + f"BID={pos_bid[:6]}{'...' if len(pos_bid) > 6 else ''}"
    )
    print(summary)


# ═══════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════


def _ns_str(ns: argparse.Namespace, name: str) -> str | None:
    value: object = getattr(ns, name)
    return value if isinstance(value, str) else None


def _parse_cli() -> CliArgs:
    parser = argparse.ArgumentParser(description="OB 档位 drift 表生成器")
    _ = parser.add_argument("--symbol", type=str, help="币种目录名 (如 XBTUSD)")
    _ = parser.add_argument(
        "--date", type=str, help="日期, 逗号分隔 (如 2026-03-09,2026-03-10)"
    )
    _ = parser.add_argument("--all", action="store_true", help="跑全部币种全部日期")
    _ = parser.add_argument(
        "--print-levels", type=str, help="打印指定币种的档位表 (不重算, 读已有CSV)"
    )
    ns = parser.parse_args()
    return CliArgs(
        symbol=_ns_str(ns, "symbol"),
        date=_ns_str(ns, "date"),
        all_symbols=getattr(ns, "all") is True,
        print_levels=_ns_str(ns, "print_levels"),
    )


def main() -> None:
    args = _parse_cli()

    if args.print_levels is not None:
        symbol_key = args.print_levels
        base = symbol_key.lower()
        agg = pd.read_csv(f"{OUTPUT_DIR}/drift_{base}.csv")
        name = SYMBOLS.get(symbol_key, symbol_key)
        for tau in (1.0, 60.0):
            print_level_grid(agg, name, tau)
        return

    if args.all_symbols:
        all_dates: set[str] = set()
        for sd in SYMBOLS:
            bdir = os.path.join(DATA_ROOT, "book_snapshot_25", sd)
            if os.path.isdir(bdir):
                for f in os.listdir(bdir):
                    if f.endswith(".csv.gz"):
                        all_dates.add(f.split("_")[0])
        dates = sorted(all_dates)
        symbols_to_run = list(SYMBOLS.items())
    elif args.symbol and args.date:
        symbol_key = args.symbol
        symbol_name = SYMBOLS.get(symbol_key, symbol_key)
        symbols_to_run = [(symbol_key, symbol_name)]
        dates = [d.strip() for d in args.date.split(",")]
    else:
        parser = argparse.ArgumentParser(description="OB 档位 drift 表生成器")
        parser.print_help()
        return

    print(f"币种: {len(symbols_to_run)} 个, 日期: {len(dates)} 天")
    print(f"输出目录: {OUTPUT_DIR}/\n")

    for sd, sn in symbols_to_run:
        print(f"--- {sn} ({sd}) ---")
        _ = run_symbol_dates(sd, sn, dates)


if __name__ == "__main__":
    main()
