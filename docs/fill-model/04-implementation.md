# 04 — 实施路线与代码对应

## 实施路线图

```
Phase 0 ✅ → Phase 1 ⏳ → Phase 2 ⏳ → Phase 3 ⏳ → Phase 4 ⏳
 (形状探索)   (分段指数)    (queue)     (联合决策)   (regime)
```

## Phase 1 产出（第一版目标）

分段指数 CSV，每个 pair 两个文件：

```
config/prob_params/kraken/<PAIR>_buy.csv   ← BID fill 的 c(x)
config/prob_params/kraken/<PAIR>_sell.csv  ← ASK fill 的 c(x)
```

每行 6 列（无表头）：
```
slope, intercept, end, ten_b, k, prefix_end
```

C++ `InstrumentComponent` 自动从 `prob_params_dir` 加载（已实现）。

## 与现有代码的对应

| 组件 | 当前实现 | Phase 1 目标 | 备注 |
|------|---------|-------------|------|
| 成交概率 | `fills_per_day`（离散 level） | `TailFillModel.expected_fill_usd()` | C++ 当前线性近似 |
| 收益估计 | `pnl_mid_bps` from DriftTable | `Edge_s(x,z)` conditional | 需加 shrinkage |
| 决策排序 | `pnl × fills` | `EFU × Edge − InvPenalty` | 优先积分形式 |
| 风险调整 | 无 | `g_scale` per regime | 已预留参数 |
| 队列校正 | 无 | `min(1, Q/Q_level)` (Phase 2) | 注意不与 D_eff 重复 |

## 量纲约定

| 量纲 | 用途 |
|------|------|
| `c_per_day(x)` | 拟合、存储、CSV |
| `c_per_sec(x) = c_per_day/86400` | P_fill 公式 |
| `T = 60s` | 挂单等待时间（与 drift tau 一致） |
| `x` = USD cumulative depth | 深度坐标 |

## 关键公式速查

```
c(x)       = ten_b × exp(k × x)           ← reach rate (events/day)
P_fill     = 1 − exp(−c(x)T/86400)         ← 成交概率
EFU        = ∫[1 − exp(−c(x)T/86400)]dx    ← 期望成交 USD
Edge       = E[PnL | fill at depth x]       ← 条件收益
Score      = ∫ P_fill(x) × Edge(x) dx       ← 挂单评分（严谨）
Score      ≈ EFU × Edge                     ← 快速近似
```

## 常见陷阱

1. **把 taker 穿透当 maker fill** → 浅档 over-estimate
2. **PnL 过拟合** → 深档样本少，`c × pnl` 方差极大 → 做 shrinkage
3. **非平稳** → Poisson 假设在 news/vol spike 下失效 → `g_scale` 或分 regime
4. **竞争均衡** → 若某 depth 的 `c × pnl` 长期为正，其他 maker 进入 → 定期 recalibrate
5. **线性 EFU 近似** → 浅档高频时高估 → 用 `1 − exp(−cT)`
6. **坐标不统一** → BID/ASK 两侧 depth 必须统一到 USD cumulative
7. **queue 重复校正** → `D_eff` + `min correction` 不能同时用
8. **T 与 tau 不对齐** → fill 模型 `T=60s` 必须与 drift `tau=60s` 一致
9. **c_per_day vs c_per_sec 混用** → 代码中必须明确命名

## 相关文件

```
docs/fill-model/              ← 文档
  README.md
  01-framework.md
  02-parameterization.md
  03-validation.md
  04-implementation.md

python/
  fill_hazard_analysis.py     ← Phase 0: taker reach rate 分析
  drift_table.py              ← DriftTable 生成（Edge 数据源）
  analyze_stability.py        ← PnL 日间稳定性分析
  markout_eval.py             ← Markout 评估（load_book/load_trades 工具）

include/common/
  data.h:1804                 ← TailFillModel C++ 实现
  data.h:771                  ← prob_params_buy/sell 加载
  drift_table.h               ← DriftTable C++ 实现
  markout.h                   ← MarkoutTable（Phase 4）

strategy/
  taking_all_multi.cpp:421    ← process_order（当前 pnl×fills 排序）
  fair_price_generator.cpp    ← FP 引擎
  order_processor.cpp         ← OrderProcessor（Relaxquant 版参考）

output/hazard/                ← Phase 0 输出
```
