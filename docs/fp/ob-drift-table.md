# OB 档位 Drift 表 — 做市下单决策引擎

## 一句话

不用预设 spread。对 OB 每一档查表: `net_edge = cap - drift`。正的就挂, 负的就不挂。挂多少 = `net_edge × 预期成交量 × 风险预算`。

---

## 一、表是怎么算出来的

```
对每一天的数据:
  for each book snapshot:
    取该时刻的 25 档 OB (bid/ask price + qty)
    对每个 snapshot 之间的 taker trade:
      买方量 → 沿 ask book 逐层填充 → 每层被吃了多少
      卖方量 → 沿 bid book 逐层填充 → 每层被吃了多少

    对每个档位 level i:
      如果这一层有 fill:
        cap  = (fill_price - mid) / fill_price       # 吃到的 spread
        等 tau 秒后:
        drift = (mid_{t+tau} - mid_t) / fill_price   # 价格往不利方向动了多少
        pnl   = cap - drift                          # 这个 fill 的净收益

输出: 按 (maker_side, level, tau) 加权平均 → 一张 drift 表
```

**关键**: 不需要 FP。`dist = log(price / mid_OB)` 是纯 OB 结构, 与任何外部定价无关。

---

## 二、输出文件

| 文件 | 内容 | 用途 |
|------|------|------|
| `drift_<symbol>.csv` | 全量聚合 (所有天加权平均) | 下单时查表 |
| `drift_<symbol>_daily.csv` | 逐日明细 | **手动调试**: 看哪天表变形了 |
| `drift_<symbol>_by_vol.csv` | 按 vol 分三层 (low/med/high) | 高 vol 天换表 |

### 列定义

| 列 | 含义 | 正负解读 |
|----|------|---------|
| `maker_side` | BID / ASK | — |
| `level` | OB 档位, 0=best, 24=最深 | — |
| `tau_sec` | markout 窗口 (秒) | — |
| `avg_dist_bps` | 该档位到 mid 的平均 log 距离 | — |
| `cap_mid_bps` | 成交时吃到的 spread (bps) | 始终 ≥0 |
| `drift_mid_bps` | 成交后 OB mid 往不利方向移动的幅度 (bps) | **正=不利**, 负=有利 |
| `pnl_mid_bps` | `cap - drift` | **正=赚钱** |
| `fill_count` | 该档位的 fill 总次数 | — |
| `fills_per_day` | 日均 fill 次数 | — |
| `n_days` | 统计天数 | — |

---

## 三、如何读这张表 — 手动调试指南

### 3.1 快速看某个币种

```bash
# 跑完 drift_table.py --all 之后
python drift_table.py --print-levels XBTUSD
```

输出类似:

```
Level  ASK dist  ASK cap ASK drift  ASK pnl  |  BID dist  BID cap BID drift  BID pnl   fills/d
    0       0.1    +0.05     +1.68    -1.63  |       0.0    +0.03     +1.71    -1.67     16812
    1       0.5    +0.50     +0.39    +0.11  |       0.4    +0.41     +2.13    -1.72      4212
    ...
    8       2.4    +2.43     +2.93    -0.51  |       2.4    +2.39     +1.99    +0.40       267
    9       2.6    +2.59     +3.43    -0.83  |       2.8    +2.76     +1.22    +1.53       197
   10       3.0    +2.98     +2.73    +0.25  |       2.7    +2.68     +3.65    -0.97       150
```

**读法**: Level 0 (best bid/ask) PnL 为负 — 因为 cap 极小 (OB spread ~0.1bps) 而 drift 很大 (成交后价格快速跑掉)。大约 level 8-10 开始 PnL 转正 — cap 涨到了 2-3bps, drift 没有同比例增长。

**决策**: 在 BTC 上做市, level < 8 的档位不该挂单。level 8-15 是甜点区, PnL 正且 fill 量适中。

### 3.2 对比不同 tau

```
tau=1s:    level 8 ASK pnl=-0.49, BID pnl=+0.30
tau=60s:   level 8 ASK pnl=-0.51, BID pnl=+0.40
tau=300s:  level 8 ASK pnl=?,    BID pnl=?
```

如果 PnL 从 1s→60s 递减, 说明这个档位的成交后会被趋势性 flow 吃掉 — 你应该用短 tau (快速 turnover)。如果 PnL 随 tau 增大而增大, 说明 reversal 占主导 — 可以持仓更久。

### 3.3 对比逐日表

```bash
# 打开 drift_<symbol>_daily.csv, 按 level=8, tau=60, maker_side=ASK 过滤
# 看每天的 pnl_mid_bps 是否稳定
```

某天 PnL 突然转负 → 那天的市场环境不适合这个档位做市 → 要么是趋势市 (drift 暴增), 要么是 vol spike。

### 3.4 对比 vol 分层表

```bash
# drift_<symbol>_by_vol.csv
# 同一个 level 在 low_vol / med_vol / high_vol 下的 pnl
```

如果 high_vol 下 PnL 显著变差 → **高 vol 天应该换到更深的档位, 或直接暂停该方向**。

---

## 四、如何用这张表做下单决策

### 4.1 最简单版本: 固定选正 PnL 档位

```python
# 加载 drift 表
table = pd.read_csv("drift_tables/drift_xbtusd.csv")

# 选 BTC ASK 方向, tau=60s, PnL > 0 的档位
ask_60 = table[(table["maker_side"] == "ASK") & (table["tau_sec"] == 60)]
good_levels = ask_60[ask_60["pnl_mid_bps"] > 0]["level"].tolist()
# → [1, 6, 10, 11, 13, 15, 16, 19, 23]   (仅 3 天数据)

# 下单: 在 good_levels 里选 fills_per_day 最多的几个档位
best = ask_60[ask_60["level"].isin(good_levels)].nlargest(5, "fills_per_day")
```

### 4.2 进阶: 按 vol 选表

```python
vol_table_low  = pd.read_csv("drift_tables/drift_xbtusd_by_vol.csv")
vol_table_med  = ...
vol_table_high = ...

current_vol = compute_recent_volatility()  # 实时计算

if current_vol < vol_p33:
    table = vol_table_low
elif current_vol > vol_p67:
    table = vol_table_high
else:
    table = vol_table_med
```

### 4.3 最终: 每档计算期望收益, 选最优

```python
def pick_best_level(table, side, tau, balance_usd, risk_per_fill):
    """从所有 PnL>0 的档位中, 选期望收益最大的"""
    sub = table[(table["maker_side"] == side) & (table["tau_sec"] == tau)]
    sub = sub[sub["pnl_mid_bps"] > 0].copy()
    if sub.empty:
        return None  # 没有档位值得挂

    # 期望收益 = pnl(bps) × fill_prob (用 fills/day 近似) × size
    sub["expected_profit"] = (
        sub["pnl_mid_bps"] / 10000  # bps → fraction
        * sub["fills_per_day"]      # fill 概率 proxy
        * risk_per_fill              # USD
    )
    return sub.nlargest(3, "expected_profit")[["level", "pnl_mid_bps", "fills_per_day", "expected_profit"]]
```

---

## 五、与固定 spread 方案的对比

| | 固定 spread | Drift 表 |
|:--|:-----------|:---------|
| 参数 | 1 个 (spread bps) | 0 个 (纯查表) |
| 适应性 | 高 vol 天可能不适用 | 天然反映不同 vol 下的 drift |
| 优化空间 | 只能调一个 spread | 每个档位独立决策, 可以做多档组合 |
| 做市粒度 | 双边同 spread | BID/ASK 可以用不同档位 |
| 数据需求 | 少 | 需要多天数据生成可靠的表 |
| 过拟合风险 | 低 | 需要足够样本, 需要 shrinkage |

---

## 六、调试清单

手动优化一个币种时, 按这个顺序检查:

1. **打开逐日表, 看 `pnl_mid_bps` 的日间稳定性**
   - 如果某些天 PnL 剧烈为负 → 识别这些天的特征 (vol? trend? news?)
   - 决定是要在那些天换更深的档位, 还是直接暂停

2. **对比不同 tau 的 PnL**
   - 选 PnL 最大且稳定的 tau 作为下单决策的参考窗口
   - 如果 tau=1s 和 tau=60s 的 PnL 差不多 → 选短 tau (减少持仓风险)

3. **对比 ASK 和 BID 是否对称**
   - 如果 ASK PnL >> BID PnL → 市场偏向卖方有利 → 可能需要调整

4. **看 fills_per_day 的衰减**
   - 从 best 档 (level 0) 到最深档 (level 24), fill 量呈指数衰减
   - 找到 PnL × fills 最大的"甜点档位"

5. **对比 vol 分层表**
   - 高 vol 下是否所有档位的 PnL 都变差?
   - 如果是 → vol 高时直接暂停做市
   - 如果只是浅档变差, 深档还好 → 高 vol 时自动移到深档

6. **样本量检查**
   - 最深档位 (level 20+) fill_count 可能只有几十次
   - 小样本均值不可靠 → 对这些档位加 safety margin

---

## 七、后续: 把表接进 OrderProcessor

C++ 端下单时的调用流程 (伪代码):

```cpp
void OrderProcessor::process_order() {
  auto &table = markout_table_;  // 加载 drift_<symbol>.csv

  for (auto &inst : trading_instruments) {
    auto &ob = depth_map[inst.id];
    double mid = 0.5 * (ob.bids[0].price + ob.asks[0].price);

    // 选当前波动率对应的 regime
    int regime = get_vol_regime();  // 0=low, 1=med, 2=high

    // ASK 方向: 遍历有正 PnL 的档位, 选最优
    for (int level : table.positive_levels(ASK, regime, tau=60)) {
      double price = ob.asks[level].price;
      double cap = table.cap(ASK, level, regime, tau=60);
      double drift = table.drift(ASK, level, regime, tau=60);
      double net_bps = cap - drift;

      if (net_bps <= 0) continue;

      double size = calc_order_size(net_bps, ob.asks[level].qty, balance);
      if (size > 0)
        SendOrder(ASK, price, size);
    }
  }
}
```

---

## 八、生成和更新

```bash
# 全量生成
python drift_table.py --all

# 单个币种
python drift_table.py --symbol XBTUSD --date 2026-03-09,2026-03-10

# 打印档位表 (不重算)
python drift_table.py --print-levels XBTUSD
```

建议每月更新一次表, 或在策略表现退化时重新生成。
