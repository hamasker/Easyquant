# 成交概率模型综述

> ⚠️ 本文档已拆分为分层结构，请从 **[docs/fill-model/README.md](fill-model/README.md)** 进入。
> 本文档保留作为完整参考，新内容优先更新到 `fill-model/` 下。

> 基于多轮深度讨论（2026-05-31），整合数学框架、经济直觉、模型选择、数据验证的完整方案。

---

## 一、核心框架：Flow Reach Rate × Conditional Edge

做市挂单的期望收益由两个独立维度决定：

```
Score_s(D, A, T, z) = ∫_D^{D+A} P_fill(x, T, z) × Edge_s(x, z) dx
```

| 维度 | 含义 | 负责模块 |
|------|------|---------|
| `P_fill(x, T, z)` | 挂在深度 x、等 T 秒的成交概率 | TailFillModel（待构建） |
| `Edge_s(x, z)` | 成交后的条件期望净收益 | DriftTable（已有，需条件化） |
| `s` | BID fill / ASK fill 分侧 | 两侧独立建模 |
| `z` | 市场状态：vol, spread, imbalance, session... | regime 分层 |

**核心洞察**：成交 ≠ 赚钱。`P(fill)` 高但 `Edge` 负的位置是毒流陷阱——模型必须同时优化两个维度，而不是分开估计再乘。

**"联合"的具体含义**：`c(x|z)` 和 `Edge(x|z)` 可以分开估计（不同模块负责），但必须在**同一 regime `z`** 下进行，且决策层做积分/点乘时两者都是条件于 fill 的条件期望。即：不能用"无条件 Edge × 无条件 fill"。

---

## 二、数学基础

### 2.1 Taker Flow 的尾部分布

定义累计深度 `x` = 从 best price 起算的 USD 总量。每一笔 taker order 有一个"吃穿深度"`S`（消耗多少累计 USD depth）。

若 taker 到达率 `λ`（泊松过程），单笔大小 `S` 的尾部分布为 `P(S ≥ x)`，则：

```
c(x) = λ × P(S ≥ x)        ← 吃到深度 x 的到达率（reach rate）
```

**关键区分**：`c(x)` 是 taker flow 的 **reach rate**，不是 maker 的 fill rate。Maker fill rate 还需考虑 queue position：

```
c_maker(x, Q) ≈ c_taker(x) × min(1, Q / Q_level(x))
```

### 2.2 成交概率的正确形式

订单挂在深度 `D`，大小 `A = QP` USD，等待 `T` 秒：

```
P_fill(x, T) = 1 − exp(−c(x) × T / 86400)
```

**注意**：不是线性近似 `c(x)T/86400`。线性近似只在 `cT ≪ 1` 时成立，浅档高频成交时会系统性高估。指数形式体现了"同一片订单成交一次后不能再成交"的约束。

**⚠️ 当前 C++ 实现是线性近似**：`TailFillModel::expected_fill_usd()` 实际使用 `day_fill × (H_sec/86400)`（即 `(T/86400)∫c(x)dx`），而非 `∫[1−e^{−cT/86400}]dx`。Phase 1 需决定：改 C++ 实现，还是先校准 `c(x)` 再单独升级 EFU。

### 2.3 期望成交 USD (EFU)

```
EFU(D, A, T) = ∫_D^{D+A} [1 − exp(−c(x)T/86400)] dx
```

离散化后用梯形积分即可。

### 2.4 期望收益 (Expected PnL)

```
ExpectedPnL(D, A, T, z) = ∫_D^{D+A} P_fill(x,T,z) × Edge_s(x,z) dx − InventoryPenalty
```

---

## 三、三种 fill 概念的区分（极易混淆）

| 概念 | 符号 | 含义 | 数据来源 |
|------|------|------|---------|
| **A. Taker 穿透率** | fill_count / fills_per_day | taker 吃到 OB 第 i 档的次数 | `drift_table.py` |
| **B. Maker Reach Rate** | `c(x)` | 挂在深度 x 的 maker 订单被吃到的到达率 | TailFillModel 建模对象 |
| **C. 至少成交一次概率** | `1 − e^{−cT}` | T 秒内至少有 1 笔成交 | `prob_at_least_once()` |

关系：`c_maker ≈ c_taker × min(1, Q/Q_level)`。**用 A 代 B 会系统性高估**，尤其浅档 queue 竞争激烈时。

---

## 四、`c(x)` 的参数化形式

### 4.1 指数模型（微观基础基线）

```
c(x) = ten_b × exp(k × x),  k < 0
```

**微观基础**：taker 大小服从指数分布 `P(S ≥ x) = exp(−λ_s·x)`，到达率泊松 `λ`，则 `c(x) = λ × exp(−λ_s·x)`。

- `ten_b ≈ λ`：成交活跃度
- `|k| ≈ λ_s`：深度衰减速度

**适用场景**：taker size 无重尾时。单段指数下 `log c(x) vs x` 是线性的。

### 4.2 分段指数

```
c(x) = ten_b_j × exp(k_j × x),  x ∈ [b_j, b_{j+1})
```

每段独立 `(ten_b, k, end, prefix_end)`，即 `TailFillModel` 的 `pp` 格式。浅/深档可用不同衰减率。

### 4.3 指数+幂律混合（Phase 4 目标，非第一版）

```
log c_s(x, z) = a_s + k_s·x + β_s·log(x + x₀) + θ_s^⊤·z + η_s^⊤·(z·x)
```

| 项 | 含义 | 约束 |
|----|------|------|
| `a_s` | 基线活跃度 | — |
| `k_s·x` | 指数衰减 | `k_s ≤ 0` |
| `β_s·log(x+x₀)` | 重尾修正 | 若 `β_s < 0` 则深档 fill 比指数预测的多 |
| `θ_s·z` | 市场状态平移 | vol 高 → 成交更活跃 |
| `η_s·(z·x)` | 状态×深度交互 | vol 高 → 衰减可能变快或变慢 |

**优势**：同时覆盖指数衰减（微观基础）和重尾（经验事实），一个公式统一。

**⚠️ 注意**：`TailFillModel` 的 C++ 实现只支持分段指数 `(ten_b, k, end, prefix_end)`，不支持 `β·log(x+x₀)` 和 regime 交互项 `η·(z·x)`。因此：

| 路径 | 做法 | 阶段 |
|------|------|------|
| **A. 纯分段指数**（推荐第一版） | Phase 1 直接输出分段指数 CSV，接现有 C++ | Phase 1 |
| **B. 混合模型→分段近似** | 离线拟合混合模型，再压成分段指数 | Phase 4 |
| **C. 扩展 TailFillModel** | C++ 支持混合形式 + regime 参数 | Phase 4 |

**第一版走路径 A**：Phase 1 用分段指数，§4.3 混合模型作为 Phase 4 的统计升级。

### 4.4 模型选择标准

不预设形状。用样本外误差比较：

- `log c(x) vs x` **线性** → 纯指数够
- `log c(x) vs x` **弯曲** → 分段指数或指数+幂律
- `log c(x) vs log x` **线性** → 纯幂律

---

## 五、Conditional Edge（DriftTable 的条件化）

### 5.1 核心问题

DriftTable 的 `pnl_mid_bps` = `E[pnl | fill at level i]`。但 fill 本身是信息事件：

- **被买方 taker 吃掉（ASK fill）** → 说明有主动买流 → fair price 可能已偏上
- **被卖方 taker 吃掉（BID fill）** → 说明有主动卖流 → fair price 可能已偏下

成交深度越深，信息含量越大（更深 = 更大的 taker order = 更可能 informed）。

### 5.2 必须验证的假设

对每个深度 x，计算：`E[ΔF_τ | taker reaches depth x]`，τ = 100ms, 500ms, 1s, 3s, 10s。

验证项：
- 深度越深 → adverse selection 是否越强？
- buy taker 和 sell taker 是否对称？
- 高 vol vs 低 vol → drift 结构是否不同？
- 不同 session → drift 结构是否不同？

### 5.3 与 fill model 的联合条件

Edge 和 fill 必须在**同一 regime z** 下估计，否则会错配。例如：
- 高 vol 时 fill 多、但 drift 也大 → net edge 可能反而更低
- 低 vol 时 fill 少、但 drift 小 → net edge 可能更稳定

---

## 六、深度坐标与量纲的统一

### 6.1 有效深度 `D_eff`（Phase 2）

`D_eff` 不只是 OB 档位的价格深度，而是 queue depth：

```
D_eff = Σ(更好价格档位的 qty) + 同价格档位中排在前面的 qty
```

- 挂在 best bid 队尾 → `D_eff = 当前 best bid 上排在前面的 size`
- 挂在 bid2 → `D_eff = bid1_size + bid2 前方 size`
- 改善报价挂到新的 best bid → `D_eff ≈ 0`（但 adverse selection 变大）

### 6.2 USD 统一

BID 和 ASK 两侧的深度单位必须统一到 **USD cumulative depth**，不能一侧用 base qty 一侧用 USD notional，否则同一 level 两侧不可比。

### 6.3 多订单不重叠 + Queue 校正二选一

同一侧多个挂单映射到累计深度轴上时，区间不能重叠——同一笔 taker flow 不能重复计算：

```
bid1: D=[100, 200], bid2: D=[300, 500], bid3: D=[700, 1000]
```

**Queue 校正二选一**（不能同时用）：

| 方案 | 做法 | 适用场景 |
|------|------|---------|
| **A. D_eff 精细版** | 使用 `D_eff = better depth + queue ahead`，订单区间 `[D_eff, D_eff+A]`，**不再乘 min correction** | 有 queue position 数据时 |
| **B. min correction 粗略版** | 使用 level 聚合 depth，`c_maker ≈ c_taker × min(1, Q/Q_level)` | 只有 level 级别数据、无 queue position 时 |

若已使用方案 A（D_eff 含 queue ahead 做积分区间），则**不再额外乘 min(1, Q/Q_level)**，否则会重复惩罚 queue 效应。Phase 0/1 用方案 B，Phase 2 升级到方案 A。

### 6.5 `c(x)` 的量纲统一

`x` 为 USD cumulative depth 时，`c(x)` 必须固定量纲。推荐：

| 量纲 | 符号 | 用途 |
|------|------|------|
| `c_per_day(x)` | events/day at depth x | 拟合、存储、CSV 输出 |
| `c_per_sec(x) = c_per_day(x) / 86400` | events/sec | P_fill 公式中与 T(秒) 相乘 |

**代码中必须明确命名 `c_per_day` / `c_per_sec`，不能混用。**

`P_fill(x, T)` 的两种等价写法：
```
P_fill = 1 − exp(−c_per_sec(x) × T)       ← c(x) 为 per_sec
P_fill = 1 − exp(−c_per_day(x) × T/86400)  ← c(x) 为 per_day
```

### 6.6 `T` 与策略 horizon 对齐

fill 模型的 `T`（挂单等待时间）必须与 DriftTable 的 `tau`（markout 窗口）一致。当前 DriftTable 主用 `tau=60s`，因此 fill 模型默认 `T=60s`。若不一致，`Score` 量纲会错配。

### 6.7 Phase 0/1 的简化 depth

Phase 0/1 使用**OB cumulative USD depth**（不含 queue position），即 `D = Σ_{j<level} price_j × qty_j`。Phase 2 升级到含 queue ahead 的 `D_eff`。文档中写清楚当前阶段用的是哪个 depth 定义。

---

## 七、数据验证路线图

### Phase 0：形状探索（不改模型，只画图）

**⚠️ P0.1 需要新代码**：`drift_table.py` 的 `fill_count` 是按 OB level 聚合的，不是 P0.1 要的"每笔 taker 吃穿深度 Y_j"。需新写逻辑：对每笔 trade 沿 OB 累加被吃 USD → Y_j → `ĉ(x) = #{Y_j ≥ x} / total_time`。

| 任务 | 能否复用 drift_table | 说明 |
|------|---------------------|------|
| P0.1 形状 | ❌ 需新逻辑 | 对每笔 trade 算 cumulative USD penetration |
| P0.2 单调性 | ⚠️ 近似 | level→USD depth 后可做 isotonic |
| P0.3 分侧 | ✅ | 已有 BID/ASK 分开 |
| P0.4 甜点曲线 | ✅ | `fills_per_day × pnl` 现成 |

**P0.1 — taker reach rate 的形状**
- 从 `book_snapshot_25` + `trades` 构造每笔 taker event 的吃穿深度 `Y_j`
- 估计 `ĉ(x) = #{Y_j ≥ x} / total_time`
- 画 `log ĉ(x) vs x` → 线性 → 指数够；弯曲 → 分段/混合
- 画 `log ĉ(x) vs log x` → 同时看幂律拟合度

**P0.2 — 单调性验证**
- `ĉ(x)` 是否严格递减？
- 用 isotonic regression 平滑，不要信 raw 散点

**P0.3 — 分侧验证**
- `ĉ_bid(x)` vs `ĉ_ask(x)` 是否显著不同？

**P0.4 — 甜点曲线**
- 画 `ĉ(x) × pnl(x)` vs `x`，看最大值是否和 `pnl(x)` 单独最大值不同
- 若甜点区在深档（level 15+），Phase 1 需对 `pnl` 做 Bayesian shrinkage（向全局均值或浅档回归），因为深档样本太少

### Phase 1：拟合 `c(x)`（输出分段指数 CSV，接 TailFillModel）

**路径 A（推荐）**：纯分段指数，直接输出 `(ten_b, k, end, prefix_end)` 6 列 CSV。

**P1.1 — 分段指数拟合**
- 深度分段：`[0, 50, 100, 200, 500, 1000, 2000, 5000, 10000]` USD
- 对 isotonic 平滑后的 `(x_i, ĉ_i)` 做分段 WLS：`log c = log ten_b + k·x`
- 每段计算 `prefix_end = ∫ c(x)dx` 到段尾
- 输出与 `TailFillModel` 一致的 6 列 CSV（`slope, intercept, end, ten_b, k, prefix_end`）

**P1.2 — 样本外校准**
- 指标：`MAPE(EFU_pred, EFU_obs)` 或 fill count calibration curve
- 按 depth bin 画校准曲线：预测 fill 率 vs 实际 fill 率

**⚠️ 路径 B/C（混合模型）延后到 Phase 4**。

### Phase 2：Queue 校正

**P2.1 — 一阶校正**
- `c_maker ≈ c_taker × min(1, Q / Q_level)`
- 用 mock/backtest 真实挂单 fill 验证

### Phase 3：联合决策

**P3.1 — 决策函数**
- **严谨版本**：`Score = ∫_D^{D+A} P_fill(x,T,z) × Edge_s(x,z) dx − InventoryPenalty`
- **实盘快速近似**（当 A 较小、Edge 在区间内平坦时）：`Score ≈ EFU(D,A,T) × Edge(D,z)`
- 替换当前 `pnl × fills_per_day`，按 depth 连续选档，不固定 25 level

### Phase 4：Regime 条件化

**P4.1 — g_scale 接入**
- vol / session 调节 `g_scale`（`TailFillModel` 已预留参数）
- 与 drift 表 vol 分层对齐

---

## 八、五个模型条件（修订版）

| # | 条件 | 性质 | 验证方法 |
|---|------|------|---------|
| ① | `c'(x) ≤ 0`（单调递减） | **硬约束**（无套利条件） | isotonic regression |
| ② | `log c(x)` 形状不做预设 | **模型选择条件** | 线性→指数；弯曲→混合 |
| ③ | 样本外可校准 | **关键条件** | MAPE / calibration curve |
| ④ | 参数逐日稳定 | **必要条件** | `ten_b`, `k` 的 CV（Phase 4 加 `β` 后同步监控） |
| ⑤ | 规模敏感性（`Q` 越大 EFU 越大） | **必须满足** | 同 depth 不同 Q 的 fill 对比 |

---

## 九、常见陷阱

1. **把 taker 穿透当 maker fill** → 浅档 over-estimate，挂太 aggressive
2. **PnL 过拟合** → 深档 fill 少（level 20+ 样本几十次），`c × pnl` 乘积方差极大；应对 pnl 做 shrinkage
3. **非平稳** → Poisson 假设在 news/vol spike 下失效；`g_scale` 或分 regime 比单参数更稳
4. **竞争均衡** → 若某 depth 的 `c × pnl` 长期显著为正，其他 maker 会挤进来，`c(x)` 内生化下降 → 需要 periodic recalibration
5. **线性 EFU 近似** → `cT/86400` 在浅档高频时高估；必须用 `1 − exp(−cT)`
6. **坐标不统一** → BID/ASK 两侧 depth 必须统一到 USD cumulative

---

## 十、实施路线

```
Phase 0 (数据探索)
  ├── P0.1: log c(x) vs x 形状
  ├── P0.2: 单调性
  ├── P0.3: BID vs ASK
  └── P0.4: pnl × fills 甜点曲线
        ↓
Phase 1 (模型拟合)
  ├── P1.1: 分段指数拟合 → prob_params CSV（6列）
  └── P1.2: 样本外校准
        ↓
Phase 2 (Queue 校正)
  └── P2.1: c_maker = c_taker × f_queue
        ↓
Phase 3 (联合决策)
  └── P3.1: Score = EFU × Edge 替换 pnl × fills
        ↓
Phase 4 (条件化)
  └── P4.1: g_scale(vol, session)
```

---

## 十一、与现有代码的对应关系

| 组件 | 当前实现 | 目标 | 备注 |
|------|---------|------|------|
| 成交概率 | `fills_per_day`（离散 level，taker 穿透） | `TailFillModel.expected_fill_usd()`（连续 depth，maker hazard） | C++ 当前为线性近似，Phase 1 决定是否改 |
| 收益估计 | `pnl_mid_bps` from DriftTable | `Edge_s(x, z)` conditional on fill | 需加 shrinkage（深档小样本） |
| 决策排序 | `rank = pnl × fills` | `Score = EFU(D,A,T) × Edge(D,z) − InventoryPenalty` | 优先保留积分形式 |
| 风险调整 | 无 | `g_scale` per regime | `TailFillModel` 已预留参数 |
| 队列校正 | 无 | `c_maker ≈ c_taker × min(1, Q/Q_level)` | Phase 2；注意不与 D_eff 重复 |

---

## 十二、关键公式速查

```
c(x)      = λ × P(S ≥ x)                    ← reach rate (1/s 或 1/day)
P_fill    = 1 − exp(−c(x)T/86400)            ← 成交概率
EFU       = ∫[1 − exp(−c(x)T/86400)]dx       ← 期望成交 USD
Edge      = E[PnL | fill at depth x]          ← 条件收益
Score     = ∫ P_fill(x) × Edge(x) dx − InvPen ← 挂单评分
```

> **最重要的公式**：`Score = P(fill) × E[PnL | fill]`  
> 不是分开估计再乘，而是联合条件化。成交事件本身携带信息。
