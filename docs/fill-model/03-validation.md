# 03 — 数据验证路线图

## Phase 0：形状探索 ✅ 完成

**目标**：验证指数假设、分侧必要性、甜点区位置。

**脚本**：`python/fill_hazard_analysis.py`

### P0.1 — Taker Reach Rate 形状

**逻辑**：
1. 每笔 trade 对齐到前一个 book snapshot
2. 判定方向：`px ≥ best_ask → buy_taker`，`px ≤ best_bid → sell_taker`
3. 沿 OB 逐档累加 qty，记录吃穿的累计 USD depth `Y_j`
4. `ĉ(x) = #{Y_j ≥ x} / total_days`

**输出**：
- `output/hazard/taker_reach_events_*.csv` — 每笔 trade 的 Y_j
- `output/hazard/hazard_curve_*.csv` — ĉ(x) 各深度
- `output/hazard/hazard_*_4panels.png` — 4 张诊断图

**结论**（7 symbol，15 天）：
- ✅ 指数假设成立：所有 symbol 的 `log ĉ(x) vs x` 近似线性
- ✅ 分侧必要：BID/ASK base rate 差 20–40%
- ✅ 三类衰减：稳定币 |k|~0.00005 / 加密 |k|~0.0001-0.0005 / 外汇 |k|~0.0007-0.0009

### P0.2 — 单调性验证

Isotonic regression（PAV 算法）强制单调递减，与原始 `ĉ(x)` 对比。
所有 symbol 通过。

### P0.3 — BID vs ASK

两侧独立估计 k 和 base。大部分 symbol ASK > BID（买方 taker 更活跃）。

### P0.4 — 甜点曲线

`c(x) × Edge(x) vs x`（依赖 DriftTable 的 pnl 数据）。
分析1 已显示外汇浅档、加密中深档是甜点区。

---

## Phase 1：分段指数拟合 ⏳

**目标**：输出 `prob_params` CSV（6 列），接 `TailFillModel`。

**输入**：Phase 0 的 `hazard_curve_*.csv`（或 isotonic 平滑后的版本）

**P1.1 — 分段指数拟合**
- 分段边界：`[0, 50, 100, 200, 500, 1000, 2000, 5000, 10000]` USD
- 对每段做 WLS：`log c = log ten_b + k·x`
- 计算 `prefix_end = ∫_0^end c(x)dx`
- 输出 CSV：`slope, intercept, end, ten_b, k, prefix_end`
- 确保 BID/ASK **分开**拟合和输出

**P1.2 — 样本外校准**
- 指标：`MAPE(EFU_pred, EFU_obs)` 按 depth bin
- 校准曲线：预测 fill 率 vs 实际 fill 率
- 留 3 天做 test，12 天做 train

---

## Phase 2：Queue 校正 ⏳

**P2.1 — 一阶校正**
- `c_maker ≈ c_taker × min(1, Q / Q_level)`
- 用 mock/backtest 真实挂单 fill 验证

**二选一**（不能同时用）：
- **方案 A**（精细）：`D_eff = better depth + queue ahead`，区间积分，**不再乘 min correction**
- **方案 B**（粗略）：level 聚合 depth，用 `min(1, Q/Q_level)` 校正

Phase 0/1 用方案 B，Phase 2 升级到方案 A。

---

## Phase 3：联合决策 ⏳

**P3.1 — Score 替换**
- 严谨：`Score = ∫ P_fill(x) × Edge(x) dx − InventoryPenalty`
- 近似（Edge 平坦时）：`Score ≈ EFU(D,A,T) × Edge(D,z)`
- 替换当前 `pnl × fills_per_day`

---

## Phase 4：Regime 条件化 ⏳

**P4.1 — g_scale 接入**
- `g_scale` 随 vol / session 调节
- `TailFillModel` 已预留参数
- 与 drift 表 vol 分层（`_by_vol.csv`）对齐

**P4.2 — 混合模型（可选）**
- 指数+幂律混合：离线拟合 → 压成分段指数近似
- 或扩展 C++ `TailFillModel`

---

## 深度坐标约定

| Phase | Depth 定义 | Queue 校正 |
|-------|-----------|-----------|
| 0–1 | OB cumulative USD depth（`Σ price×qty` 到该档） | 方案 B：`min(1, Q/Q_level)` |
| 2+ | `D_eff = better depth + same-price queue ahead` | 方案 A：区间积分，不再乘 correction |
