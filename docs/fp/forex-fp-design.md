# Forex FP 盈利分析与设计方案

> 2026-05-25: 经多轮讨论迭代, 从"多交易所路径融合" → "Kraken local fair + IB basis" → 最终收敛为:
> **OB + volatility 做主报价, FP 做边界约束**。在高 taker fee 市场里 FP 的角色不是定价中心, 而是风控过滤器。

---

## 一、基础认知

### 1.1 Forex 不是集中撮合市场

Forex 现货是 OTC 分散报价市场, 不存在唯一公允价格。Kraken EUR/USD 是 Kraken 内部流动性池的价格, IB EUR/USD 是传统银行间 FX 聚合价, **两者长期偏离不代表 Kraken 错价**。

### 1.2 策略交易结构

```
启动时: Kraken spot 持有现货 + Binance swap 固定对冲 → delta 中性
运行时: 在 Kraken spot 挂 maker 单买卖 → 变相多空
        bid 成交 → Kraken 多出 base → 净多头
        ask 成交 → Kraken 少掉 base → 净空头
平仓:   必须通过 maker 反向成交 (taker fee ~5-10bps, 不可用)
```

### 1.3 PnL 分解与 FP 的影响路径

```
PnL = spread_capture + maker_rebate - maker_fee
    - adverse_selection - inventory_loss - hedge_slippage

FP 主要影响:
  adverse_selection:  FP 偏了 → 挂单被 informed flow 打
  inventory_loss:     FP 偏了 → 在错误方向累积仓位 → 浮动亏损
  quote skew:         FP 告诉你该偏买还是偏卖
  极端错价过滤:       FP 防止你在明显错误的价格挂单
```

**单笔成交视角**:
```
bid_fill  PnL_Δ = FP_{t+Δ} - fill_price - fee
ask_fill  PnL_Δ = fill_price - FP_{t+Δ} - fee

如果 markout 长期为负 → 被 informed flow 打单
```

---

## 二、FP 在高频做市中的角色定位 (关键)

### 2.1 高 taker fee 市场的影响

高 taker fee (如 Kraken 5-10bps) 会:

1. 降低小幅错价被快速套利的速度 → **FP 短期精度要求可以下降**
2. 盘口更钝、更慢 → spread 更宽 → maker 靠 spread 生存
3. 微小 FP 误差可能被 spread 覆盖

**判断 FP 精度是否关键**:
```
如果 |FP_error| ≪ expected_edge:
  expected_edge = half_spread - fee - adverse_buffer - inventory_buffer
  → FP 精度不是第一矛盾

如果 |FP_error| ≈ expected_edge:
  → FP 明显影响 PnL

如果 |FP_error| > expected_edge:
  → 长期亏
```

### 2.2 FP 应该是"当前 fair"而非"未来预测"

```
错误: FP_t = strong_forecast_of_future_price
正确: FP_t = current_fair_value (mark-to-market anchor)

报价拆解:
  center    = mid_OB + β(FP - mid_OB) + α - λ × inventory
  FP        → 当前真实价值 (定价锚)
  α         → 极短期预测/skew signal (小权重)
  inventory → 仓位风控
  spread    → 成本、波动、逆向选择补偿
```

**为什么 FP 不应做太强的预测**:
1. 预测噪声 → 频繁撤单
2. queue position 变差
3. post-only reject 增多
4. 成交率下降
5. 预测错了 → 整体双向挂偏

尤其在高 taker fee 市场: `|α| ≪ half_spread` 比较合理。

### 2.3 新的定价架构: OB 做主, FP 做边界

```
之前的设计:
  center = fp_pred - λ × inventory       ← FP 是报价中心
  bid/ask = center ± spread               ← 围绕 FP 挂单

修正后的设计:
  center = mid_OB + β(FP - mid_OB) + α - λ × inventory
          ──────   ─────────────────   ──
          主锚      FP 只做轻量 skew    微弱预测

  bid = min(raw_bid,  sellable - cost - profit)   ← FP/退出边界是上限
  ask = max(raw_ask,  buyable  + cost + profit)   ← FP/退出边界是下限
```

**FP 的三个角色 (按重要性排序)**:
1. **报价边界/过滤器**: `bid ≤ sellable - cost - profit` — 确保买入后能卖出
2. **弱方向信号**: `β(FP - mid_OB)` — 当 OB 明显偏离 FP 时轻微纠正
3. **风控锚点**: 当三路径严重分歧时暂停做市

**β 参数选择**:
```
β = 0       → 完全不用 FP (仅当回测证明 FP 对未来 markout 无解释力)
β = 0.1~0.3 → 轻量, 适合高 taker fee、Kraken 内部路径一致的场景
β = 0.5~0.7 → 均衡, 适合路径有分歧但可用的场景
β > 0.8     → 重 FP, 适合稳定币做市 (USDT/USDC), 不适合 forex
```

---

## 三、Kraken 内部 FP 计算 (保留, 用于边界和弱信号)

### 3.1 三路径 BookView

```
Path 1: Kraken EUR/USD 直盘 depth → BookView(bid, ask, bid_qty, ask_qty)
Path 2: USDT/EUR → 1/(USDT/EUR) × fp_usdt → 隐含 EUR/USD
Path 3: USDC/EUR → 1/(USDC/EUR) × fp_usdc → 隐含 EUR/USD

struct BookView {
  double bid, ask, bid_qty, ask_qty;
  int64_t ts_ns;
  double mid()    const { return 0.5*(bid+ask); }
  double spread() const { return ask-bid; }
  double micro()  const {
    double den = bid_qty + ask_qty;
    return den>0 ? (ask*bid_qty + bid*ask_qty)/den : mid();
  }
  BookView invert() const { return {1.0/ask, 1.0/bid, ask_qty, bid_qty, ts_ns}; }
};
```

### 3.2 核心计算

```cpp
// ——— robust mid ———
double p1 = direct.micro();
double p2 = usdt_impl.mid();
double p3 = usdc_impl.mid();
double robust_mid = median3(p1, p2, p3);

// ——— executable envelope ———
double sellable = max(direct.bid, usdt_impl.bid, usdc_impl.bid);
double buyable  = min(direct.ask, usdt_impl.ask, usdc_impl.ask);
double exec_mid = 0.5 * (sellable + buyable);
double exec_spread = buyable - sellable;

// ——— local fair ———
double fp_local = 0.7 * robust_mid + 0.3 * exec_mid;
double local_edge = fp_local - direct.mid();

// ——— 分歧检测 ———
double dispersion = max(p1,p2,p3) - min(p1,p2,p3);
bool divergence = dispersion > kThreshold * direct.spread();

// ——— weak alpha (可选, 小权重) ———
double alpha = a * imbalance * direct.spread()
             + b * trade_flow
             + d * basis_residual;   // IB basis, 权重很小

// ——— IB basis (单独维护, 不直接拉 FP) ———
double basis = fp_local - ib_mid;
double basis_z = (basis - basis_ema) / basis_std;
```

### 3.3 输出

```
直接输出 (供报价使用):
  fp_local       → 用于 β(FP - mid_OB) 弱 skew
  sellable       → 用于 bid 退出边界
  buyable        → 用于 ask 退出边界
  local_edge     → 诊断用
  divergence     → 暂停/加宽 spread 信号
  basis_z        → IB 异常检测

存储 (供后续分析):
  fps_map_[currency].fps.add({fp_bid, fp_ask})
  fps_map_[currency].vps.add((fp_bid+fp_ask)/2)
```

---

## 四、报价逻辑 (OrderProcessor 侧)

### 4.1 推荐报价公式

```
// ——— Step 1: OB 做主锚 ———
center_raw = mid_OB - λ × inventory

// ——— Step 2: FP 做弱修正 ———
edge = fp_local - mid_OB
center = center_raw + β × edge + α

// ——— Step 3: 动态 spread ———
half_spread = max(min_half,
                  base + k×σ + adverse_buffer + inventory_buffer)

// ——— Step 4: 原始报价 ———
raw_bid = center - half_spread
raw_ask = center + half_spread

// ——— Step 5: 退出边界约束 (FP 的真正作用在这里) ———
bid = min(raw_bid, sellable - cost - profit)
ask = max(raw_ask, buyable  + cost + profit)

// ——— Step 6: tick rounding ———
bid = floor_to_tick(bid)
ask = ceil_to_tick(ask)
```

### 4.2 β 参数指南

| 场景 | β | 说明 |
|------|:--:|------|
| Kraken 内部三路径高度一致, 高 taker fee | 0~0.1 | FP 几乎不参与中心, 仅做边界 |
| Kraken 内部有分歧但在 spread 内 | 0.1~0.3 | FP 轻量纠正 |
| 分歧频繁, 直盘薄 | 0.3~0.5 | FP 中度参与 |
| 稳定币做市 (USDT/USDC) | 0.7~1.0 | FP 主导 (因为锚定在 1.0) |

### 4.3 最简起步版本 (推荐)

先从这个最简版本开始, 跑通数据流后再迭代:

```
center = mid_OB - λ × inventory
half_spread = base + k × σ + adverse_buffer

raw_bid  = center - half_spread
raw_ask  = center + half_spread

bid = min(raw_bid,  sellable - cost - profit)
ask = max(raw_ask,  buyable  + cost + profit)
```

这个版本中:
- FP 完全不参与 center 计算 (β=0)
- FP 通过 sellable/buyable 提供退出边界保护
- 复杂度最低, 最容易验证

### 4.4 为什么不建议只看 OB + volatility, 完全不用 FP?

纯 OB + volatility 的风险:

1. **OB mid 可能不是 fair** — 虚假流动性、局部撤单、spoof → mid 失真
2. **你不知道是否挂在错误一侧** — 如果 direct OB 偏高而 implied FP 偏低, OB mid 会误导
3. **库存容易被错误价格带偏** — bid 一直成交、ask 不成交、库存堆积、价格继续跌

**FP 的底线作用**: 不是告诉你要挂哪里, 而是告诉你**不能挂到哪里**。

### 4.5 什么时候可以让 β ≈ 0?

回测发现以下都成立:
1. `FP - mid_OB` 对未来 markout 无解释力
2. local_edge 不预测未来收益
3. 成交后 markout 主要由 spread / queue / vol 决定
4. Kraken 内部三路径长期极度一致
5. 加 FP 反而增加撤单、降低成交率

→ 可以让 β ≈ 0, 但**仍然保留 sellable/buyable 作为边界保护**。

---

## 五、FP 暂停/减量条件

出现以下情况时减少 size、扩大 spread 或暂停:

| # | 条件 | 严重程度 | 动作 |
|:--|------|:-------:|------|
| 1 | 三路径 dispersion 突然扩大 | 高 | 扩大 spread ×2 或暂停 |
| 2 | sellable > buyable (理论交叉) 但扣除成本无利润 | 高 | 暂停该 pair |
| 3 | 某条路径 age > 5s | 中 | 扩大 spread |
| 4 | IB basis_z 急跳 (>3σ) | 中 | 观察, 不暂停 |
| 5 | direct spread 突然扩大 (>3× normal) | 高 | 暂停 |
| 6 | 最近成交 markout 持续为负 | 高 | 扩 spread, 缩量 |
| 7 | 库存接近上限 | 中 | 只挂减仓方向 |
| 8 | 稳定币路径 depth 不足 (bid_qty, ask_qty 极小) | 中 | 扩大 spread |
| 9 | local_edge 突然异常 (>5× normal) | 高 | 暂停 |

---

## 六、Markout 验证 (终极指标)

### 6.1 定义

```
bid markout_Δ  = fp_{t+Δ} - fill_price     (>0 好, <0 被割)
ask markout_Δ  = fill_price - fp_{t+Δ}      (>0 好, <0 被割)

统计窗口: 100ms, 500ms, 1s, 3s, 10s, 30s
```

### 6.2 分层分析

按以下维度分组统计 markout:
1. bid fill vs ask fill
2. 高 imbalance vs 低 imbalance
3. FP 高于 OB mid vs FP 低于 OB mid
4. spread 宽 vs spread 窄
5. 库存高 vs 库存低
6. IB basis_z 高 vs 低
7. direct 与 implied 分歧高 vs 低

某状态下 markout 长期为负 → 该状态下扩 spread / 降量 / 降方向激进度 / 必要时暂停。

### 6.3 关键回测测试

```
回归: r_{t→t+Δ} = α + β₁×local_edge + β₂×basis_z + β₃×imbalance + ε

如果 β₁ 不显著 → local_edge 不预测未来 → β 在报价中可以更小
如果 β₂ 不显著 → IB basis 不能用于报价, 只作风控
如果 β₃ 显著   → imbalance 有价值, 可入 α
```

---

## 七、实施计划

### Phase 1: 基础架构 (2-3 天)

1. `BookView` 结构体 — 加在 `data.h`
2. 重写 `calculate_fp_forex()` — 三路径 BookView + robust_mid + sellable/buyable
3. 一致性检测 — dispersion vs threshold
4. `fp_local` 写入 `fps_map_` (不改变对外接口)

### Phase 2: 报价框架 (依赖 OrderProcessor, 3-4 天)

5. 实现最简版本: `center = mid_OB - λ × inventory`
6. 动态 spread: `base + k × σ + adverse_buffer`
7. 退出边界: `bid = min(raw_bid, sellable - cost - profit)`
8. 库存 skew / tick rounding

### Phase 3: FP 弱参与 (2-3 天)

9. β 参数接入 (0~0.3)
10. α 信号 (imbalance, trade_flow, basis_residual)
11. 暂停条件实现 (10 条)

### Phase 4: 回测验证 (持续)

12. Python 离线评价 → markout 统计 + 分层分析
13. 参数扫描 (β, λ, k, base, adverse_buffer)
14. A/B 对比: β=0 vs β=0.2 vs 纯 OB+vol

---

## 八、核心原则总结

1. **高 taker fee 市场里, OB + volatility 做主报价, FP 做边界约束**
2. FP 不是定价中心 — 是告诉你在哪里**不能挂**单, 而非在哪里挂单
3. `β` 可以很小 (~0.1) 甚至接近 0, 取决于回测结果
4. sellable/buyable 退出边界是 FP 最有价值的输出
5. alpha (预测) 的权重应该远小于 half_spread
6. IB basis 只做 external residual 检测, 不直接拉主 fair
7. **markout 是最终裁判** — 不说理论, 看成交后赚不赚钱
8. 最简起步版本: `center = mid_OB - λ×inventory, bid/ask 受 sellable/buyable 约束`
