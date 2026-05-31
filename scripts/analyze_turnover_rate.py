#!/usr/bin/env python3
"""快速统计 backtest trade 数据的 turnover，用于校准 fp_turnover_usd 阈值。

Trade record 格式:
  [type:1B][ts_ns:8B][inst_id:28B][price:8B][qty:8B][side:1B][local_ns:8B] = 62B
"""

import os
import sys
import struct
import mmap

HDR_SIZE = 37
REC_TRADE = 3
TRADE_REC_SIZE = 62  # HDR_SIZE + 25

def process_trade_file(filepath):
    """统计单个 trade .bin 的成交量"""
    try:
        fsize = os.path.getsize(filepath)
        if fsize == 0:
            return 0, 0, 0, 0

        with open(filepath, 'rb') as f:
            with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as m:
                total_volume = 0.0   # price * qty 累加 (USD turnover)
                total_records = 0
                first_ts = None
                last_ts = None
                offset = 0
                size = len(m)

                while offset + TRADE_REC_SIZE <= size:
                    rectype = m[offset]
                    if rectype != REC_TRADE:
                        # skip unknown (shouldn't happen in a trade file)
                        offset += 1
                        continue

                    ts_ns = struct.unpack_from('<q', m, offset + 1)[0]
                    price = struct.unpack_from('<d', m, offset + HDR_SIZE)[0]
                    qty = struct.unpack_from('<d', m, offset + HDR_SIZE + 8)[0]

                    if price > 0 and qty > 0:
                        total_volume += price * qty
                        total_records += 1
                        if first_ts is None:
                            first_ts = ts_ns
                        last_ts = ts_ns

                    offset += TRADE_REC_SIZE

                return total_volume, total_records, first_ts, last_ts
    except Exception as e:
        print(f"  ERROR: {e}", file=sys.stderr)
        return 0, 0, 0, 0

def main():
    data_root = '/data/bin'

    print("Exchange           Pairs   Records      Turnover(USD)   TimeSpan    Rate($/s)")
    print("-" * 85)

    grand_total_vol = 0.0
    grand_total_records = 0
    grand_first_ts = None
    grand_last_ts = None

    for exchange in sorted(os.listdir(data_root)):
        trade_dir = os.path.join(data_root, exchange, 'trade')
        if not os.path.isdir(trade_dir):
            continue

        ex_vol = 0.0
        ex_records = 0
        ex_first = None
        ex_last = None
        pair_count = 0

        for date_dir in sorted(os.listdir(trade_dir)):
            date_path = os.path.join(trade_dir, date_dir)
            if not os.path.isdir(date_path):
                continue
            for f in sorted(os.listdir(date_path)):
                if not f.endswith('.bin'):
                    continue
                filepath = os.path.join(date_path, f)
                vol, recs, first, last = process_trade_file(filepath)
                if vol > 0:
                    pair_count += 1
                    ex_vol += vol
                    ex_records += recs
                    if first and (ex_first is None or first < ex_first):
                        ex_first = first
                    if last and (ex_last is None or last > ex_last):
                        ex_last = last

        if ex_vol > 0:
            span_s = (ex_last - ex_first) / 1e9 if ex_first and ex_last else 0
            rate = ex_vol / span_s if span_s > 0 else 0
            print(f"{exchange:15s}  {pair_count:4d}   {ex_records:>9,}  {ex_vol:>16,.0f}  {span_s:>8,.0f}s  {rate:>12,.0f}")
            grand_total_vol += ex_vol
            grand_total_records += ex_records
            if ex_first and (grand_first_ts is None or ex_first < grand_first_ts):
                grand_first_ts = ex_first
            if ex_last and (grand_last_ts is None or ex_last > grand_last_ts):
                grand_last_ts = ex_last

    print("-" * 85)
    span_s = (grand_last_ts - grand_first_ts) / 1e9 if grand_first_ts and grand_last_ts else 0
    rate = grand_total_vol / span_s if span_s > 0 else 0
    print(f"{'TOTAL':15s}  {pair_count:4d}   {grand_total_records:>9,}  {grand_total_vol:>16,.0f}  {span_s:>8,.0f}s  {rate:>12,.0f}")

    # 校准建议
    fp_threshold = 5000  # 默认值
    print(f"\n【fp_turnover_usd 校准】")
    print(f"  数据时间 turnover rate: {rate:,.0f} $/s")
    print(f"  当前 fp_turnover_usd: {fp_threshold}")
    print(f"  期望 FP 频率 (mock 全 TopN): ~{rate * 4 / fp_threshold:.1f} 次/s (假设 4x turnover)")
    print(f"  实际 FP 频率 (回测): ~{rate / fp_threshold:.1f} 次/s (仅 turnover 驱动)")
    print(f"  ---")
    print(f"  若要保持和 mock 相同频率，建议:")

    # mock 如果有 ~80 pairs, backtest 只有 ~15-20
    # 需要知道 mock 的 turnover 总量来校准
    # 目前用数据时间 turnover rate 估算
    for ratio in [0.2, 0.25, 0.33, 0.5]:
        adj = int(fp_threshold * ratio)
        fp_freq = rate / adj if adj > 0 else 0
        print(f"    假设覆盖率 {ratio*100:.0f}%: fp_turnover_usd={adj} → FP ~{fp_freq:.1f} 次/s")

    if rate > 0:
        print(f"\n  如果期望 FP 每 50ms 触发一次 (20次/s): fp_turnover_usd = {int(rate / 20)}")
        print(f"  如果期望 FP 每 100ms 触发一次 (10次/s): fp_turnover_usd = {int(rate / 10)}")
        print(f"  如果期望 FP 每 200ms 触发一次 (5次/s): fp_turnover_usd = {int(rate / 5)}")

if __name__ == '__main__':
    main()
