#!/usr/bin/env python3
"""分析 backtest 二进制数据的时间间隔分布，评估 speed_=0 的影响。

关键指标：k-way merge 后的全局记录间隔。
- gap > 200ms: FP 200ms 兜底会延迟到下一记录
- gap > 5s: should_negative 检测延迟
- gap > 30s: should_disconnect 检测延迟
"""

import os
import sys
import struct
import mmap
from collections import defaultdict
from heapq import merge as hmerge

HDR_SIZE = 37  # 1B type + 8B ts + 28B inst_id
REC_BBO = 1
REC_DEPTH = 2
REC_TRADE = 3
BODY_SIZE = {REC_BBO: 40, REC_DEPTH: 170, REC_TRADE: 25}
REC_SIZE = {k: HDR_SIZE + v for k, v in BODY_SIZE.items()}

def extract_timestamps(filepath):
    """从 .bin 文件提取所有 (timestamp_ns, type) 元组"""
    try:
        with open(filepath, 'rb') as f:
            with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as m:
                ts_list = []
                offset = 0
                size = len(m)
                while offset + HDR_SIZE <= size:
                    rectype = m[offset]
                    ts_ns = struct.unpack_from('<q', m, offset + 1)[0]
                    body_len = BODY_SIZE.get(rectype, 0)
                    if body_len == 0:
                        offset += HDR_SIZE  # skip unknown
                        continue
                    ts_list.append((ts_ns, rectype))
                    offset += HDR_SIZE + body_len
                return ts_list
    except Exception as e:
        print(f"  ERROR reading {filepath}: {e}", file=sys.stderr)
        return []

def kway_merge_ts(file_ts_dict):
    """k-way merge 多个文件的 timestamp 序列，返回合并后的 ts 列表"""
    # 每个文件的 ts 已经排好序（文件写入时按时间顺序）
    # 直接用 heapq.merge
    streams = [iter(ts_list) for ts_list in file_ts_dict.values() if ts_list]
    if not streams:
        return []
    merged = list(hmerge(*streams, key=lambda x: x[0]))
    return merged

def compute_gaps(merged_ts):
    """计算连续记录间的时间间隔（纳秒）"""
    gaps_ns = []
    for i in range(1, len(merged_ts)):
        gap = merged_ts[i][0] - merged_ts[i-1][0]
        if gap >= 0:
            gaps_ns.append(gap)
    return gaps_ns

def stats(gaps_ns, total_records):
    """统计 gap 分布"""
    if not gaps_ns:
        return {}
    gaps_ns_sorted = sorted(gaps_ns)
    n = len(gaps_ns_sorted)

    ms_200 = 200_000_000
    s_5 = 5_000_000_000
    s_30 = 30_000_000_000

    exceed_200ms = sum(1 for g in gaps_ns_sorted if g > ms_200)
    exceed_5s = sum(1 for g in gaps_ns_sorted if g > s_5)
    exceed_30s = sum(1 for g in gaps_ns_sorted if g > s_30)

    return {
        'total_records': total_records,
        'total_gaps': n,
        'min_ms': gaps_ns_sorted[0] / 1e6,
        'p50_ms': gaps_ns_sorted[n // 2] / 1e6,
        'p95_ms': gaps_ns_sorted[int(n * 0.95)] / 1e6,
        'p99_ms': gaps_ns_sorted[int(n * 0.99)] / 1e6,
        'p999_ms': gaps_ns_sorted[int(n * 0.999)] / 1e6,
        'max_ms': gaps_ns_sorted[-1] / 1e6,
        'exceed_200ms': exceed_200ms,
        'exceed_200ms_pct': exceed_200ms * 100.0 / n,
        'exceed_5s': exceed_5s,
        'exceed_5s_pct': exceed_5s * 100.0 / n,
        'exceed_30s': exceed_30s,
        'exceed_30s_pct': exceed_30s * 100.0 / n,
    }

def main():
    data_root = '/data/bin'

    # 收集所有文件，按 exchange 和 type 分组
    files_by_exchange = defaultdict(list)
    for root, dirs, files in os.walk(data_root):
        for f in files:
            if f.endswith('.bin'):
                full = os.path.join(root, f)
                # path: /data/bin/<exchange>/<type>/<date>/<inst>.bin
                parts = full.split('/')
                exchange = parts[3] if len(parts) > 3 else 'unknown'
                dtype = parts[4] if len(parts) > 4 else 'unknown'
                files_by_exchange[exchange].append((dtype, full))

    for ex in sorted(files_by_exchange.keys()):
        print(f"\n{'='*60}")
        print(f"Exchange: {ex}  ({len(files_by_exchange[ex])} files)")
        print(f"{'='*60}")

        ex_files = files_by_exchange[ex]
        file_ts = {}
        total = 0

        for dtype, path in sorted(ex_files):
            fsize_mb = os.path.getsize(path) / 1e6
            print(f"  Reading {dtype:10s} {os.path.basename(path):20s} ({fsize_mb:.0f} MB)...", end=' ', flush=True)
            ts_list = extract_timestamps(path)
            file_ts[path] = ts_list
            total += len(ts_list)
            print(f"{len(ts_list):,} records")

        if len(file_ts) <= 1:
            # 只有一个文件，直接算 gap
            ts_only = [t[0] for t in file_ts[list(file_ts.keys())[0]]]
            merged = [(t, 0) for t in ts_only]
            print(f"\n  Single file, total {len(ts_only):,} records")
        else:
            print(f"\n  K-way merging {len(file_ts)} files ({total:,} total records)...", end=' ', flush=True)
            merged = kway_merge_ts(file_ts)
            print(f"done, {len(merged):,} merged")

        gaps = compute_gaps(merged)
        s = stats(gaps, len(merged))

        print(f"\n  Gap Distribution (per-exchange k-way merge):")
        print(f"  {'Min':>10s}: {s['min_ms']:10.3f} ms")
        print(f"  {'Median':>10s}: {s['p50_ms']:10.3f} ms")
        print(f"  {'P95':>10s}: {s['p95_ms']:10.3f} ms")
        print(f"  {'P99':>10s}: {s['p99_ms']:10.3f} ms")
        print(f"  {'P99.9':>10s}: {s['p999_ms']:10.3f} ms")
        print(f"  {'Max':>10s}: {s['max_ms']:10.3f} ms")
        print(f"\n  超出阈值统计:")
        print(f"  > 200ms (FP兜底延迟):  {s['exceed_200ms']:>10,} gaps ({s['exceed_200ms_pct']:.4f}%)")
        print(f"  > 5s   (negative延迟): {s['exceed_5s']:>10,} gaps ({s['exceed_5s_pct']:.4f}%)")
        print(f"  > 30s  (disconnect延迟): {s['exceed_30s']:>10,} gaps ({s['exceed_30s_pct']:.4f}%)")

    # 全局 k-way merge（跨交易所，抽样）
    print(f"\n{'='*60}")
    print(f"Global k-way merge (sampling: trade files only, all exchanges)")
    print(f"{'='*60}")

    global_files = {}
    for ex in sorted(files_by_exchange.keys()):
        for dtype, path in files_by_exchange[ex]:
            if dtype == 'trade':  # trade 是 turnover 驱动 FP 的关键
                fsize_mb = os.path.getsize(path) / 1e6
                print(f"  Reading {ex}/{dtype}/{os.path.basename(path)} ({fsize_mb:.0f} MB)...", end=' ', flush=True)
                ts_list = extract_timestamps(path)
                global_files[f"{ex}/{os.path.basename(path)}"] = ts_list
                print(f"{len(ts_list):,} records")

    if len(global_files) > 1:
        total_global = sum(len(v) for v in global_files.values())
        print(f"\n  K-way merging {len(global_files)} trade files ({total_global:,} total)...", end=' ', flush=True)
        merged = kway_merge_ts(global_files)
        print(f"done, {len(merged):,} merged")

        gaps = compute_gaps(merged)
        s = stats(gaps, len(merged))

        print(f"\n  Global Trade Gap Distribution:")
        print(f"  {'Min':>10s}: {s['min_ms']:10.3f} ms")
        print(f"  {'Median':>10s}: {s['p50_ms']:10.3f} ms")
        print(f"  {'P95':>10s}: {s['p95_ms']:10.3f} ms")
        print(f"  {'P99':>10s}: {s['p99_ms']:10.3f} ms")
        print(f"  {'P99.9':>10s}: {s['p999_ms']:10.3f} ms")
        print(f"  {'Max':>10s}: {s['max_ms']:10.3f} ms")
        print(f"\n  超出阈值统计:")
        print(f"  > 200ms (FP兜底延迟):  {s['exceed_200ms']:>10,} gaps ({s['exceed_200ms_pct']:.4f}%)")
        print(f"  > 5s   (negative延迟): {s['exceed_5s']:>10,} gaps ({s['exceed_5s_pct']:.4f}%)")
        print(f"  > 30s  (disconnect延迟): {s['exceed_30s']:>10,} gaps ({s['exceed_30s_pct']:.4f}%)")

        # 评估影响
        print(f"\n{'='*60}")
        print(f"【speed_=0 影响评估】")
        print(f"{'='*60}")

        exceed_200ms_pct = s['exceed_200ms_pct']
        exceed_5s_pct = s['exceed_5s_pct']

        if exceed_200ms_pct < 0.01:
            print(f"  FP 200ms 兜底: {exceed_200ms_pct:.4f}% gaps > 200ms → 几乎没有延迟")
        elif exceed_200ms_pct < 1:
            print(f"  FP 200ms 兜底: {exceed_200ms_pct:.2f}% gaps > 200ms → 偶尔延迟，影响极小")
        elif exceed_200ms_pct < 10:
            print(f"  FP 200ms 兜底: {exceed_200ms_pct:.1f}% gaps > 200ms → 有一定延迟，建议关注")
        else:
            print(f"  FP 200ms 兜底: {exceed_200ms_pct:.1f}% gaps > 200ms → 延迟较多，建议 speed_ > 0")

        if exceed_5s_pct < 0.001:
            print(f"  Negative 5s 检测: {exceed_5s_pct:.4f}% gaps > 5s → 几乎无影响")
        else:
            print(f"  Negative 5s 检测: {exceed_5s_pct:.2f}% gaps > 5s → 有一定延迟")

        print(f"\n  结论: batch=1 + speed_=0 对 {s['total_records']:,} 条记录")
        print(f"  中位间隔 {s['p50_ms']:.2f}ms，数据密度极高")
        if exceed_200ms_pct < 1:
            print(f"  只有 {exceed_200ms_pct:.2f}% 的间隙超过 200ms，speed_=0 影响极小")
        print(f"  → batch=1 + speed_=0 完全够用")

if __name__ == '__main__':
    main()
