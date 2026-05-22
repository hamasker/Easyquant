#!/usr/bin/env python3
"""
将 Parquet book_snapshot + CSV.gz trades 转为紧凑二进制格式。

输出格式 (每条 record):
  [type: uint8_t(1B)][timestamp_ns: int64_t(8B)][inst_id: char(28B)][data...]

  type=1 BBO:    {bid_px:double, bid_qty:double, ask_px:double, ask_qty:double, local_ns:int64_t} = 40B
  type=2 Depth:  {ob_level:uint16_t, local_ns:int64_t, bids[5].px, bids[5].qty, asks[5].px, asks[5].qty} = 2+8+5*8+5*8+5*8+5*8 = 170B
  type=3 Trade:  {price:double, qty:double, side:int8_t, local_ns:int64_t} = 25B

用法:
  python convert_parquet_to_bin.py <exchange> <symbol> <YYYY-MM-DD> [YYYY-MM-DD]
  python convert_parquet_to_bin.py binance BTCUSDT 2025-12-02
"""

import struct
import sys
import os
import gzip
from pathlib import Path

DATA_ROOT = Path("/data")

EXCHANGE_MAP = {
    "binance": ("binance", "bn"),
    "kraken": ("kraken", "krk"),
    "okex": ("okex", "ok"),
    "coinbase": ("coinbase", "cb"),
}

# Binary record types
REC_BBO = 1
REC_DEPTH = 2
REC_TRADE = 3

# Record header: type(1B) + ts(8B) + inst_id(28B) = 37B
HDR_FMT = "<Bq28s"
HDR_SIZE = struct.calcsize(HDR_FMT)

# BBO record data: bid_px, bid_qty, ask_px, ask_qty, local_ns = 5 * 8 = 40B
BBO_FMT = "<ddddq"
BBO_SIZE = struct.calcsize(BBO_FMT)

# Depth record: ob_level(2B) + local_ns(8B) + bids[5].px(40B) + bids[5].qty(40B) + asks[5].px(40B) + asks[5].qty(40B) = 170B
# Bids/asks: each [px(double), qty(double)] = 16B, 5 levels = 80B per side
DEPTH_FMT = "<Hq" + "d" * 5 + "d" * 5 + "d" * 5 + "d" * 5
DEPTH_SIZE = struct.calcsize(DEPTH_FMT)

# Trade record: price, qty, side(1B), local_ns = 25B
TRADE_FMT = "<ddbq"
TRADE_SIZE = struct.calcsize(TRADE_FMT)

def load_book_snapshots(exchange, symbol, dates):
    """Load Parquet book_snapshot_5, yield (ts_ns, inst_id_28, depth_dict)"""
    import pandas as pd
    snap_dir = DATA_ROOT / exchange / "book_snapshot_5" / symbol.upper()
    if not snap_dir.is_dir():
        snap_dir = DATA_ROOT / exchange / "book_snapshot_5" / symbol

    for date in dates:
        for f in sorted(snap_dir.glob(f"{date}*")):
            print(f"  Reading {f.name}...")
            if f.suffix == ".parquet":
                df = pd.read_parquet(f)
            elif f.suffix == ".gz":
                df = pd.read_csv(f)
            else:
                continue

            for _, row in df.iterrows():
                yield row["timestamp"], row


def load_trades(exchange, symbol, dates):
    """Load CSV.gz trades"""
    trade_dir = None
    for d in [DATA_ROOT / exchange / "trades" / symbol.upper(),
              DATA_ROOT / exchange / "trades" / symbol]:
        if d.is_dir():
            trade_dir = d
            break
    if not trade_dir:
        return

    for date in dates:
        for f in sorted(trade_dir.glob(f"{date}*.gz")):
            print(f"  Reading {f.name}...")
            with gzip.open(f, "rt") as fh:
                header = fh.readline().strip()
                cols = header.split(",")
                for line in fh:
                    parts = line.strip().split(",")
                    yield {c: v for c, v in zip(cols, parts)}


QUOTES = ["usdt", "usdc", "busd", "usd", "eur", "gbp", "btc", "eth", "sol", "xrp", "dai", "tusd"]

def symbol_to_inst28(symbol, exchange):
    """Convert exchange raw symbol → C++ InstrumentId char[28] internal format"""
    ex = EXCHANGE_MAP.get(exchange, (exchange, exchange))[1]
    sym = symbol.lower().replace("/", "_").replace("-", "_")

    # Kraken: XBT → btc
    if ex == "krk":
        sym = sym.replace("xbt", "btc")

    # 确保有 _ 分隔 base/quote (C++ 要求)
    if "_" not in sym:
        for q in QUOTES:
            if sym.endswith(q) and len(sym) > len(q):
                sym = sym[:-len(q)] + "_" + q
                break

    if not sym.endswith("." + ex):
        sym = sym + "." + ex

    return sym.encode("ascii").ljust(28, b"\x00")[:28]


def encode_depth(ts_ns, inst_id_28, row):
    header = struct.pack(HDR_FMT, REC_DEPTH, int(ts_ns), inst_id_28)
    bids_px = [0.0] * 5
    bids_qty = [0.0] * 5
    asks_px = [0.0] * 5
    asks_qty = [0.0] * 5
    ob_level = 0
    for i in range(5):
        bp = row.get(f"bids[{i}].price", 0) or 0
        bq = row.get(f"bids[{i}].amount", 0) or 0
        ap = row.get(f"asks[{i}].price", 0) or 0
        aq = row.get(f"asks[{i}].amount", 0) or 0
        if bp and ap:
            ob_level = max(ob_level, i + 1)
        bids_px[i] = float(bp)
        bids_qty[i] = float(bq)
        asks_px[i] = float(ap)
        asks_qty[i] = float(aq)
    local_ns = int(row.get("local_timestamp", ts_ns))
    body = struct.pack(DEPTH_FMT, ob_level, int(local_ns),
                       *bids_px, *bids_qty, *asks_px, *asks_qty)
    return header + body


def encode_trade(ts_ns, inst_id_28, row):
    header = struct.pack(HDR_FMT, REC_TRADE, int(ts_ns), inst_id_28)
    price = float(row.get("price", 0))
    qty = float(row.get("amount", 0))
    side = 1 if str(row.get("side", "buy")).lower() in ("buy", "b") else 2
    local_ns = int(row.get("local_timestamp", ts_ns))
    body = struct.pack(TRADE_FMT, price, qty, side, int(local_ns))
    return header + body


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)

    exchange = sys.argv[1]
    symbol = sys.argv[2].upper()
    dates = sys.argv[3:]

    inst_id_28 = symbol_to_inst28(symbol, exchange)
    out_path = Path(f"/data/bin/backtest_{exchange}_{symbol.lower()}_{dates[0]}.bin")

    import pandas as pd  # ensure pandas available

    records = []

    # Load book_snapshots → depth records
    for ts_ns, row in load_book_snapshots(exchange, symbol, dates):
        records.append((int(ts_ns), encode_depth(ts_ns, inst_id_28, row)))

    # Load trades
    for row in load_trades(exchange, symbol, dates):
        ts_ns = int(row["timestamp"])
        records.append((ts_ns, encode_trade(ts_ns, inst_id_28, row)))

    # Sort by timestamp
    records.sort(key=lambda x: x[0])

    # Write binary file
    out_path.parent.mkdir(parents=True, exist_ok=True)
    total = 0
    with open(out_path, "wb") as f:
        for _, rec in records:
            f.write(rec)
            total += len(rec)

    print(f"\nDone: {len(records)} records → {out_path} ({total / 1024 / 1024:.1f} MB)")

if __name__ == "__main__":
    main()
