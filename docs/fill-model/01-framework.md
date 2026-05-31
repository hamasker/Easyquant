# 01 — 核心框架与数学基础

## 1. 核心方程

做市挂单的期望收益由两个维度决定：

```
Score_s(D, A, T, z) = ∫_D^{D+A} P_fill(x, T, z) × Edge_s(x, z) dx − InventoryPenalty
```

| 维度 | 含义 | 负责模块 |
|------|------|---------|
| `P_fill(x, T, z)` | 挂在深度 x、等 T 秒的成交概率 | TailFillModel（待构建） |
| `Edge_s(x, z)` | 成交后的条件期望净收益 | DriftTable（已有，需条件化） |
| `s` | BID fill / ASK fill | 两侧独立建模 |
| `z` | 市场状态：vol, spread, imbalance, session... | regime 分层 |

**核心洞察**：成交 ≠ 赚钱。`P(fill)` 高但 `Edge` 负的位置是毒流陷阱。

**"联合"的含义**：`c(x|z)` 和 `Edge(x|z)` 可以分开估计（不同模块负责），但必须在**同一 regime `z`** 下进行。不能用"无条件 Edge × 无条件 fill"。

## 2. Taker Flow 的尾部分布

定义累计深度 `x` = 从 best price 起算的 USD 总量。每一笔 taker order 有一个"吃穿深度"`S`。

若 taker 到达率 `λ`（泊松过程），单笔大小 `S` 的尾部分布为 `P(S ≥ x)`，则：

```
c(x) = λ × P(S ≥ x)        ← 吃到深度 x 的到达率（taker reach rate）
```

**关键区分**：`c(x)` 是 taker flow 的 **reach rate**，不是 maker 的 fill rate。Maker fill rate 还需 queue correction：

```
c_maker(x, Q) ≈ c_taker(x) × min(1, Q / Q_level(x))
```

## 3. 成交概率

订单挂在深度 `D`，大小 `A = QP` USD，等待 `T` 秒：

```
P_fill(x, T) = 1 − exp(−c(x) × T / 86400)     ← c(x) 为 per_day
P_fill(x, T) = 1 − exp(−c(x) × T)              ← c(x) 为 per_sec
```

**不是线性近似** `cT/86400`。线性近似只在 `cT ≪ 1` 时成立。

⚠️ 当前 C++ `expected_fill_usd()` 是线性近似，Phase 1 需决定是否改。

## 4. 期望成交 USD (EFU)

```
EFU(D, A, T) = ∫_D^{D+A} [1 − exp(−c(x)T/86400)] dx     (严谨版)
EFU(D, A, T) ≈ (T/86400) × ∫_D^{D+A} c(x) dx             (线性近似)
```

## 5. 三种 fill 概念

| 概念 | 符号 | 含义 | 数据来源 |
|------|------|------|---------|
| **A. Taker 穿透率** | fill_count | taker 吃到 OB 第 i 档的次数 | `drift_table.py` |
| **B. Maker Reach Rate** | `c(x)` | 挂在深度 x 的 maker 订单被吃到的到达率 | TailFillModel |
| **C. 至少成交一次概率** | `1 − e^{−cT}` | T 秒内 ≥1 笔成交 | `prob_at_least_once()` |

关系：`c_maker ≈ c_taker × min(1, Q/Q_level)`。**用 A 代 B 会系统性高估**。

## 6. 微观基础

指数形式 `c(x) = ten_b × exp(k × x)`（`k < 0`）的微观基础：

```
taker 到达: Poisson(λ)
taker 大小 S: Exponential(λ_s) → P(S ≥ x) = exp(−λ_s·x)
→ c(x) = λ × exp(−λ_s·x)
→ ten_b = λ, k = −λ_s
```

经济直觉：
- `ten_b`（= λ）：taker 到达频率 → 成交活跃度
- `|k|`（= λ_s）：taker 大小衰减率 → |k| 大 = taker 订单普遍小 = fills 集中在浅档
