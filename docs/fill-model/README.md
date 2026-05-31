# 成交概率模型 — 总览

> 基于多轮深度讨论（2026-05-31），从第一性原理构建的做市成交概率模型。
> 当前状态：**Phase 0 完成，指数假设验证通过**。

---

## 快速导航

| 文档 | 内容 | 适合 |
|------|------|------|
| [01-framework.md](01-framework.md) | 核心公式、数学基础、三种 fill 区分 | 先读这篇 |
| [02-parameterization.md](02-parameterization.md) | c(x) 参数化：指数/分段/混合、模型选择 | 选模型时读 |
| [03-validation.md](03-validation.md) | Phase 0–4 验证路线、每步输入输出 | 执行时读 |
| [04-implementation.md](04-implementation.md) | 实施路线、代码对应、公式速查、陷阱 | 写代码时读 |

## 一句话总结

做市期望收益 = **Taker Flow Reach Rate × Conditional Edge**，在 depth 轴上积分。

```
Score(D,A,T,z) = ∫_D^{D+A} [1−e^{−c(x)T}] × Edge(x,z) dx − InventoryPenalty
```

- `c(x)` = λ × P(taker_size ≥ x)：taker flow 吃到深度 x 的到达率 → **TailFillModel**
- `Edge(x,z)` = E[PnL | fill at depth x, regime z]：成交后条件收益 → **DriftTable**

## Phase 状态

| Phase | 状态 | 产出 |
|-------|------|------|
| **0** | ✅ 完成 | 7 symbol 指数假设验证通过 |
| **1** | ⏳ 待做 | 分段指数拟合 → prob_params CSV |
| 2–4 | 📋 设计完成 | queue校正 → 联合决策 → regime |

## 关键结论 (Phase 0)

- **指数模型成立**：所有 7 个 symbol 的 log ĉ(x) vs x 近似线性
- **分侧必要**：BID/ASK base rate 差 20–40%
- **三类衰减**：稳定币 `|k|~0.00005`（极缓）、加密货币 `|k|~0.0001–0.0005`、外汇 `|k|~0.0007–0.0009`（最快）

## 相关文件

```
docs/fill-model/          ← 文档（本目录）
  README.md               ← 总览入口
  01-framework.md
  02-parameterization.md
  03-validation.md
  04-implementation.md

python/
  fill_hazard_analysis.py ← Phase 0 脚本
  drift_table.py           ← DriftTable（Edge 数据源）
  analyze_stability.py     ← Phase 0 补充（pnl 稳定性）

output/hazard/            ← Phase 0 输出
  taker_reach_events_*.csv
  hazard_curve_*.csv
  hazard_*_4panels.png

include/common/
  data.h                  ← TailFillModel C++ 实现
  drift_table.h           ← DriftTable C++ 实现
  markout.h               ← MarkoutTable（Phase 4）
```
