#!/usr/bin/env python3
"""
抓取各交易所所有标的的 base info（tick_size, min_qty, contract_multiplier 等），
输出 CSV 供策略加载。

用法: python python/fetch_base_info.py [-o output.csv]
"""

import json
import csv
import sys
import os
from urllib.request import urlopen, Request

OUTPUT = sys.argv[1] if len(sys.argv) > 1 else "ext/coinsimulator/config/InstrumentBaseInfo.csv"

os.environ['http_proxy'] = os.environ.get('http_proxy', 'http://172.31.240.1:7990')
os.environ['https_proxy'] = os.environ.get('https_proxy', 'http://172.31.240.1:7990')

HEADERS = {'User-Agent': 'Mozilla/5.0'}

def fetch(url):
    req = Request(url, headers=HEADERS)
    return json.loads(urlopen(req, timeout=15).read())

rows = []

# ========== Binance 现货 ==========
print("[Binance Spot] fetching...")
try:
    data = fetch("https://api.binance.com/api/v3/exchangeInfo")
    for s in data.get("symbols", []):
        if s["status"] != "TRADING": continue
        tick_size = next((f["stepSize"] for f in s["filters"] if f["filterType"] == "LOT_SIZE"), "")
        min_qty = next((f["minQty"] for f in s["filters"] if f["filterType"] == "LOT_SIZE"), "")
        price_tick = next((f["tickSize"] for f in s["filters"] if f["filterType"] == "PRICE_FILTER"), "")
        base = s["baseAsset"].lower()
        quote = s["quoteAsset"].lower()
        rows.append(["binance", f"{base}_{quote}.bn", price_tick, tick_size, min_qty, 1, "spot", base, quote])
    print(f"  {len(rows)} instruments so far")
except Exception as e:
    print(f"  FAILED: {e}")

# ========== Binance 永续 ==========
print("[Binance Swap] fetching...")
try:
    data = fetch("https://fapi.binance.com/fapi/v1/exchangeInfo")
    for s in data.get("symbols", []):
        if s["status"] != "TRADING" or s["contractType"] != "PERPETUAL": continue
        tick_size = next((f["stepSize"] for f in s["filters"] if f["filterType"] == "LOT_SIZE"), "")
        min_qty = next((f["minQty"] for f in s["filters"] if f["filterType"] == "LOT_SIZE"), "")
        price_tick = next((f["tickSize"] for f in s["filters"] if f["filterType"] == "PRICE_FILTER"), "")
        base = s["baseAsset"].lower()
        quote = s["quoteAsset"].lower()
        rows.append(["binance", f"{base}_{quote}_swap.bn", price_tick, tick_size, min_qty, 1, "swap", base, quote])
    print(f"  {len(rows)} instruments so far")
except Exception as e:
    print(f"  FAILED: {e}")

# ========== Kraken 现货 ==========
print("[Kraken] fetching...")
try:
    data = fetch("https://api.kraken.com/0/public/AssetPairs")
    for pair, info in data.get("result", {}).items():
        if isinstance(info, dict) and not pair.endswith(".d"):
            base = info.get("base", "").lower()
            quote = info.get("quote", "").lower()
            pair_name = f"{base}_{quote}_spot.krk"
            tick = info.get("tick_size", info.get("cost_min", ""))
            min_qty = info.get("ordermin", "")
            rows.append(["kraken", pair_name, tick, tick, min_qty, 1, "spot", base, quote])
    print(f"  {len(rows)} instruments so far")
except Exception as e:
    print(f"  FAILED: {e}")

# ========== OKX 现货 ==========
print("[OKX] fetching...")
try:
    data = fetch("https://www.okx.com/api/v5/public/instruments?instType=SPOT")
    for s in data.get("data", []):
        base = s["baseCcy"].lower()
        quote = s["quoteCcy"].lower()
        pair_name = f"{base}_{quote}_spot.ok"
        tick = s.get("tickSz", "")
        min_qty = s.get("minSz", "")
        rows.append(["okex", pair_name, tick, tick, min_qty, 1, "spot", base, quote])
    print(f"  {len(rows)} instruments so far")
except Exception as e:
    print(f"  FAILED: {e}")

# ========== Coinbase 现货 ==========
print("[Coinbase] fetching...")
try:
    data = fetch("https://api.exchange.coinbase.com/products")
    for s in data:
        if s.get("trading_disabled", True): continue
        base = s["base_currency"].lower()
        quote = s["quote_currency"].lower()
        pair_name = f"{base}_{quote}_spot.cb"
        tick = s.get("quote_increment", "")
        min_qty = s.get("base_min_size", "")
        rows.append(["coinbase", pair_name, tick, tick, min_qty, 1, "spot", base, quote])
    print(f"  {len(rows)} instruments so far")
except Exception as e:
    print(f"  FAILED: {e}")

# ========== 写 CSV ==========
os.makedirs(os.path.dirname(OUTPUT) or ".", exist_ok=True)
with open(OUTPUT, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["exchange", "inst_str", "tick_size", "lot_size", "min_qty", "multiplier", "inst_type", "base", "quote"])
    w.writerows(rows)

print(f"\nDone: {len(rows)} instruments → {OUTPUT}")
