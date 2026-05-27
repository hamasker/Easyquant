#!/usr/bin/env python3
"""Coinbase top-N volume pairs, USD normalized.

1. GET /products/volume-summary → filter USD pairs
2. Parallel fetch ticker prices for top candidates
3. volume_usd = spot_volume_24hour × price
4. Sort by USD volume, output top N as JSON array
"""

import json
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib.request import Request, urlopen

N = int(sys.argv[1]) if len(sys.argv) > 1 else 20
PRICE_POOL = 150  # fetch prices for top 150 candidates


def fetch_json(url):
    try:
        req = Request(url, headers={"User-Agent": "Easyquant/1.0"})
        with urlopen(req, timeout=5) as resp:
            return json.loads(resp.read().decode())
    except Exception:
        return None


# 1. volume summary
vol_data = fetch_json("https://api.exchange.coinbase.com/products/volume-summary")
if not vol_data:
    print("[]")
    sys.exit(0)

# filter USD, keep non-zero volume
candidates = []
for p in vol_data:
    if not isinstance(p, dict):
        continue
    if p.get("quote_currency", "") != "USD":
        continue
    vol_str = p.get("spot_volume_24hour", "") or "0"
    try:
        vol = float(vol_str)
    except ValueError:
        continue
    if vol > 0:
        candidates.append((vol, p["id"], p["base_currency"]))

candidates.sort(key=lambda x: x[0], reverse=True)
candidates = candidates[:PRICE_POOL]

# 2. parallel fetch prices
def get_price(pid):
    data = fetch_json(f"https://api.exchange.coinbase.com/products/{pid}/ticker")
    if data and "price" in data:
        try:
            return float(data["price"])
        except (ValueError, TypeError):
            pass
    return None

priced = []
with ThreadPoolExecutor(max_workers=20) as pool:
    futures = {pool.submit(get_price, c[1]): c for c in candidates}
    for f in as_completed(futures):
        vol, pid, base = futures[f]
        price = f.result()
        if price and price > 0:
            usd_vol = vol * price
            priced.append((usd_vol, f"{base}_USD"))

# 3. sort by USD volume, output top N
priced.sort(key=lambda x: x[0], reverse=True)
result = [{"symbol": sym, "usd_volume": vol} for vol, sym in priced[:N]]
print(json.dumps(result))
