# FP 离线评价: 用 Trade 数据模拟做市成交

## 核心原理

```
对于历史中每一个 trade:
  - 我的 bid  = fp_mid(t) - half_spread_bps
  - 我的 ask  = fp_mid(t) + half_spread_bps

  如果 trade 是卖方发起的 (aggressor=SELL) 且 trade_price ≤ 我的 bid:
    → 我买入了 (bid 被成交)
    → 记录 bid_fill, 之后观察 markout

  如果 trade 是买方发起的 (aggressor=BUY) 且 trade_price ≥ 我的 ask:
    → 我卖出了 (ask 被成交)
    → 记录 ask_fill, 之后观察 markout
```

**为什么这个假设合理**: 做市策略的 bid 挂在 fp 下方, ask 挂在上方。当有人愿意在你的价格交易时(aggressor 的方向和价格都匹配), 你的单子会被吃掉。

**忽略的因素** (后续可加): queue position 优先级、成交量不匹配、partial fill。

---

## 算法

### 输入

```
fp_series:    array of (ts_ns, fp_bid, fp_ask)    # FP 历史
trade_series: array of (ts_ns, price, qty, aggressor_side)  # 成交历史
half_spread_bps: float                               # 做市半价差
eval_windows:  [0.01, 0.1, 0.5, 1.0, 5.0, 30.0]     # markout 窗口(秒)
```

### 模拟成交

```
fills = []  # (fill_ts, fill_price, side, fp_at_fill)

for each trade in trade_series:
    fp = get_fp_at(trade.ts)   # 取 trade 时间最近的 FP

    my_bid = fp.mid - half_spread_bps
    my_ask = fp.mid + half_spread_bps

    if trade.aggressor == SELL and trade.price <= my_bid:
        fills.append(Fill(ts=trade.ts, price=trade.price, side=BID, fp=fp))

    elif trade.aggressor == BUY and trade.price >= my_ask:
        fills.append(Fill(ts=trade.ts, price=trade.price, side=ASK, fp=fp))
```

### 没有 aggressor side 时的近似

Kraken 的 public trade feed 不带 aggressor side。可以用以下规则推断:

```
if trade.price <= my_bid:
    # 成交价在 bid 区域, 推断为卖方发起
    → bid fill
elif trade.price >= my_ask:
    # 成交价在 ask 区域, 推断为买方发起
    → ask fill
elif trade.price <= fp.mid:
    # 成交价在 mid 下方, 更可能是卖方发起
    → 无判定 (信息不足)
else:
    → 无判定 (信息不足)
```

**但这样会低估 markout**: trade 在 bid 上方但仍然是卖方发起的情况被漏掉了。建议用 `trade.price <= fp.mid` 全部算 bid_fill, `trade.price >= fp.mid` 全部算 ask_fill 作为上界估计。

### 计算 markout

```
for each fill:
    for each window Δ:
        fp_future = get_fp_at(fill.ts + Δ)
        
        if fill.side == BID:
            markout = fp_future.mid - fill.price
        else:  # ASK
            markout = fill.price - fp_future.mid

        record(fill, Δ, markout)

最终统计:
    每个窗口 Δ → mean(markout), std(markout), sharpe, t_stat
```

---

## Python 实现

```python
import numpy as np
from dataclasses import dataclass
from typing import List, Dict, Optional

@dataclass
class FpPoint:
    ts_ns: int
    mid: float

@dataclass
class Trade:
    ts_ns: int
    price: float
    qty: float
    aggressor: Optional[int]  # 1=BUY, -1=SELL, None=未知

@dataclass
class Fill:
    ts_ns: int
    price: float
    side: str  # 'BID' or 'ASK'
    fp_mid: float

def simulate_fills(
    fp_series: List[FpPoint],
    trades: List[Trade],
    half_spread_bps: float,
    use_aggressor: bool = True,
) -> List[Fill]:
    """
    用历史 trade 模拟做市成交。

    Args:
        half_spread_bps: 半价差(bps), 如 3.0 表示 fp ± 0.03%
        use_aggressor: True=用已知 aggressor side 精确判定,
                       False=用价格位置推断(当 aggressor 未知时)
    """
    fills = []
    fp_idx = 0  # FP 数组的扫描指针

    for trade in trades:
        # 定位当前 trade 对应的 FP (二分或扫描)
        while (fp_idx + 1 < len(fp_series)
               and fp_series[fp_idx + 1].ts_ns <= trade.ts_ns):
            fp_idx += 1
        if fp_idx >= len(fp_series):
            break

        fp = fp_series[fp_idx]
        bid_price = fp.mid * (1 - half_spread_bps / 10000)
        ask_price = fp.mid * (1 + half_spread_bps / 10000)

        if use_aggressor and trade.aggressor is not None:
            if trade.aggressor == -1 and trade.price <= bid_price:
                fills.append(Fill(trade.ts_ns, trade.price, 'BID', fp.mid))
            elif trade.aggressor == 1 and trade.price >= ask_price:
                fills.append(Fill(trade.ts_ns, trade.price, 'ASK', fp.mid))
        else:
            # 无 aggressor side → 按价格位置推断
            if trade.price <= bid_price:
                fills.append(Fill(trade.ts_ns, trade.price, 'BID', fp.mid))
            elif trade.price >= ask_price:
                fills.append(Fill(trade.ts_ns, trade.price, 'ASK', fp.mid))

    return fills


def compute_markout(
    fills: List[Fill],
    fp_series: List[FpPoint],
    windows_sec: List[float] = [0.01, 0.1, 0.5, 1.0, 5.0, 30.0],
) -> Dict:
    """
    对每个 fill 计算多窗口 markout, 输出统计。
    """
    results = {}
    for w in windows_sec:
        delta_ns = int(w * 1e9)
        bid_markouts = []
        ask_markouts = []

        for fill in fills:
            # 找 fill_ts + delta 时刻的 FP
            future_fp = find_fp_at(fill.ts_ns + delta_ns, fp_series)
            if future_fp is None:
                continue

            if fill.side == 'BID':
                mo = (future_fp.mid - fill.price) / fill.price * 10000  # bps
                bid_markouts.append(mo)
            else:
                mo = (fill.price - future_fp.mid) / fill.price * 10000  # bps
                ask_markouts.append(mo)

        results[w] = {
            'bid': _stats(bid_markouts),
            'ask': _stats(ask_markouts),
        }
    return results


def _stats(arr: List[float]) -> Dict:
    if not arr:
        return {'mean': 0, 'std': 0, 'sharpe': 0, 'n': 0, 't_stat': 0}
    a = np.array(arr)
    mean = float(np.mean(a))
    std = float(np.std(a, ddof=1)) if len(a) > 1 else 1e-12
    return {
        'mean': mean,
        'std': std,
        'sharpe': mean / std if std > 0 else 0,
        'n': len(a),
        'pct_positive': float(np.mean(a > 0)),
        't_stat': mean / (std / np.sqrt(len(a))) if std > 0 else 0,
    }


def find_fp_at(ts_ns: int, fp_series: List[FpPoint]) -> Optional[FpPoint]:
    """二分查找 ≤ ts_ns 最近的 FP。如 ts_ns 超出范围返回 None。"""
    if not fp_series or ts_ns < fp_series[0].ts_ns:
        return None
    if ts_ns >= fp_series[-1].ts_ns:
        return fp_series[-1]  # 用最新 FP 代替

    lo, hi = 0, len(fp_series) - 1
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if fp_series[mid].ts_ns <= ts_ns:
            lo = mid
        else:
            hi = mid - 1
    return fp_series[lo]
```

### 使用示例

```python
# 加载数据
fp_data = load_fp_from_log("log/fp_eur.csv")       # ts_ns, fp_bid, fp_ask
trade_data = load_trades("data/eur_usd_trades.csv") # ts_ns, price, qty, side

fp_series = [FpPoint(ts=row[0], mid=(row[1]+row[2])/2) for row in fp_data]
trades = [Trade(ts=row[0], price=row[1], qty=row[2], aggressor=row[3])
          for row in trade_data]

# 扫描不同 spread 下的 markout
for half_spread in [2.0, 3.0, 5.0, 10.0]:
    fills = simulate_fills(fp_series, trades, half_spread)
    if len(fills) < 30:
        print(f"half_spread={half_spread}bps: only {len(fills)} fills, skip")
        continue

    markout = compute_markout(fills, fp_series)

    print(f"\n=== half_spread = {half_spread} bps, N_fills = {len(fills)} ===")
    for w, stats in markout.items():
        bid = stats['bid']
        ask = stats['ask']
        combined_mean = (bid['mean'] * bid['n'] + ask['mean'] * ask['n']) / (bid['n'] + ask['n'])
        print(f"  window={w:6.1f}s  bid_mo={bid['mean']:+.2f}bps  ask_mo={ask['mean']:+.2f}bps  "
              f"combined={combined_mean:+.2f}bps  bid_n={bid['n']}  ask_n={ask['n']}  "
              f"t_stat={bid['t_stat']:+.2f}/{ask['t_stat']:+.2f}")
```

---

## 分析的几个关键视角

### 1. Spread 扫描

```
对 [1,2,3,5,7,10,15,20] bps 分别跑模拟:
  - spread 太小 → fill 多但 markout 可能为负 (被 adverse selection 打)
  - spread 合适 → fill 适量, markout 为正
  - spread 太大 → fill 太少, 样本不足

最优 spread = argmax(fill_count × mean_markout)  或  argmax(sharpe)
```

### 2. FP 版本对比

```
fp_v1: 当前 median 三路径
fp_v2: robust_mid + exec_mid 融合
fp_v3: fp_pred (加 microstructure 信号)

对同一个 trade_series, 同一个 half_spread:
  v1 fills → markout_v1
  v2 fills → markout_v2
  v3 fills → markout_v3

结论: 哪个版本的 markout 最正且最显著 → 哪个 FP 最适合做市
```

### 3. 按时间段/市场环境分层

```
将 trade 按时段分组:
  - 亚盘 (00:00-08:00 UTC)
  - 欧盘 (08:00-16:00 UTC)
  - 美盘 (16:00-24:00 UTC)

分别跑模拟 → 看哪个时段 FP 表现好/差

或者在波动率高低环境下分别分析:
  - vol > median: 高波环境
  - vol < median: 低波环境
```

### 4. 按 FP 自身的 dispersion 分层

```
将每个 FP 时刻的三路径 dispersion 分类:
  - dispersion < 1×direct_spread:  低分歧 (FP 可信)
  - dispersion 1-3×direct_spread:  中分歧 (FP 存疑)
  - dispersion > 3×direct_spread:  高分歧 (应该停)

看不同 dispersion 下的 markout:
  → 验证"分歧大时 markout 差"这个假设是否成立
  → 如果不成立, dispersion 不是好的暂停信号
```

---

## 数据获取

### Trade 数据来源

1. **从 Kraken public API**: `https://api.kraken.com/0/public/Trades?pair=XXBTZEUR`
2. **从 WS feed 录制**: `ext/coinsimulator/feed/ws_feed` 可以 dump raw messages
3. **从已有 backtest 数据**: `python/convert_parquet_to_bin.py` 输出

### 需要注意的问题

1. **trade 数量要大**: 至少几千个 trades 才能让 markout 统计有意义
2. **FP 和 trade 的时间对齐**: 必须用同一个时间基准 (都用 UTC ns)
3. **FP 频率要够**: trade 密集时如果 FP 更新太慢, 会漏掉很多可能的 fill
4. **Kraken public trade 无 aggressor side**: 需要用价格位置推断, 这是已知的限制

---

## 输出报告示例

```
====================================
FP Evaluation Report: EUR/USD
Period: 2026-05-18 ~ 2026-05-24
FP Model: v3 (fp_pred)
====================================

Fill Simulation:
  half_spread=3bps → 1,247 fills (bid=612, ask=635)
  half_spread=5bps →   523 fills (bid=258, ask=265)
  half_spread=10bps→   104 fills (bid= 51, ask= 53)

Markout @ 5bps:
  window | bid_mean(bps) | ask_mean(bps) | combined | bid_t  | ask_t
  -------|--------------|--------------|----------|--------|-------
  0.01s  |     +0.12     |    +0.08     |  +0.10   | +1.2   | +0.9
  0.1s   |     +0.31     |    +0.25     |  +0.28   | +2.1   | +1.8
  0.5s   |     +0.45     |    +0.38     |  +0.42   | +2.8   | +2.3
  1.0s   |     +0.52     |    +0.44     |  +0.48   | +3.1   | +2.6
  5.0s   |     +0.38     |    +0.41     |  +0.40   | +1.9   | +2.0
  30.0s  |     +0.15     |    +0.18     |  +0.17   | +0.6   | +0.7

FP Health Score: 0.72 / 1.0
  markout(1s): +0.48bps ✓
  stability(p90): 0.8bps ✓ (half_spread=5bps)
  capture_ratio: 0.65 ✓
  buy_ratio: 0.49 ✓
  dispersion_norm: 1.2 ✓

Conclusion: FP suitable for market making @ 5bps half-spread.
             Recommend monitoring markout(1s) daily.
```
