# FP 评价函数设计

## 为什么需要专用评价函数

做市策略的 FP 不能用"距离某个 reference price 的 MAE"来评价, 因为:

1. Forex 没有唯一公允价格 — 没有 ground truth 可以比对
2. 准确 ≠ 适合做市 — 一个理论上精准但 500ms 后才到位的 FP 对做市没用
3. 平稳 ≠ 有利可图 — FP 可能与 OB 完美贴合但 spread 不够覆盖 adverse selection

**需要的是: 直接测量"用这个 FP 做市能赚钱吗", 而不是"这个 FP 准不准"。**

---

## 评价维度

### 维度 1: Markout — FP 质量的终极指标

```
定义:
  bid_markout(Δ)  = fp(t+Δ) - fill_price      (bid 成交后)
  ask_markout(Δ)  = fill_price - fp(t+Δ)       (ask 成交后)

解读:
  bid_markout > 0  → 买入后价格上涨 → 赚           (好)
  bid_markout < 0  → 买入后价格下跌 → 被 informed   (差)
  ask_markout > 0  → 卖出后价格下跌 → 赚           (好)
  ask_markout < 0  → 卖出后价格上涨 → 被 informed   (差)
```

**多窗口必要性**: 不同窗口揭示不同问题:
| 窗口 | 揭示的问题 |
|------|-----------|
| 10ms | FP 是否反映瞬时微观结构 |
| 100ms | 是否被 HFT flow 打 |
| 500ms | 短期方向判断质量 |
| 1s | 做市核心窗口 |
| 5s | 持仓风险暴露 |
| 30s | 是否可以 safely 等反向成交 |

**评价标准**:
```
合格: 所有窗口 markout 均值 > 0
良好: markout 在 100ms~1s 窗口显著为正 (t-test p<0.05)
优秀: markout 在所有窗口为正, 且 Sharpe > 1.0
危险: 任何窗口 markout 显著为负 → FP 或策略有问题
```

### 维度 2: FP 稳定性

做市的 FP 不能乱跳 — 每次 FP 更新都可能导致撤单+重挂, 产生操作成本和滑点风险。

```
定义:
  fp_rev = |fp(t) - fp(t-1)| / fp(t-1) × 10000  (bps)
  fp_rev_frac = fraction of updates where |fp_rev| > half_spread

解读:
  fp_rev > half_spread → 本次 FP 更新会导致报价穿越 OB → 必须撤单重挂
  fp_rev_frac 高 → FP 过于敏感, 操作成本高
  fp_rev_frac 低但 markout 差 → FP 过于迟钝
```

**评价标准**:
```
fp_rev 的 90th percentile 应该 < half_spread
fp_rev_frac 应该 < 10-20%
```

### 维度 3: 有效 Spread 捕获率

```
定义:
  假设 bid/ask 挂在 fp 两侧 half_spread 处

  gross_spread = ask_quote - bid_quote           (你挂的)
  effective_captured = Σ(fill_spread) / N        (实际成交赚到的)
  capture_ratio = effective_captured / gross_spread

  fill_spread 定义:
    如果双边都成交: ask_fill_price - bid_fill_price
    如果只成交一边: 用 markout 估计另一边 (即假设按 fp 平仓)

解读:
  capture_ratio ≈ 1.0  → 双边成交理想, FP 定价合理
  capture_ratio < 0.5  → 经常单边成交后价格跑掉, FP 可能偏了
  capture_ratio < 0    → 亏钱
```

### 维度 4: 成交对称性

```
定义:
  buy_ratio = N(bid_fills) / N(total_fills)

解读:
  buy_ratio ≈ 0.5  → 买卖均衡, FP 居中合理
  buy_ratio > 0.7  → 买方成交太多, FP 可能偏低 (或市场上涨)
  buy_ratio < 0.3  → 卖方成交太多, FP 可能偏高 (或市场下跌)

结合 inventory 看:
  buy_ratio > 0.5 且 inventory > 0 → FP 偏低, 需要上调
  buy_ratio < 0.5 且 inventory < 0 → FP 偏高, 需要下调
```

### 维度 5: 路径分歧度

```
定义:
  dispersion = max(mid_direct, mid_usdt, mid_usdc)
             - min(mid_direct, mid_usdt, mid_usdc)

  dispersion / direct_spread → normalized dispersion

解读:
  norm_dispersion < 1.0  → 三路径在 direct spread 内, 一致性好
  norm_dispersion 1-3    → 有一定分歧, 建议扩大 spread
  norm_dispersion > 3    → 严重分歧, 建议暂停做市
```

### 维度 6: IB Basis 行为

```
basis = fp_local - IB_mid
basis_z = (basis - EMA(basis)) / σ(basis)

解读:
  |basis_z| < 1   → 正常
  |basis_z| 1-2   → 轻度异常, 观察
  |basis_z| > 2   → 显著异常, 暂停 + 检查
```

---

## 评价函数实现

### 6.1 在线评价 (C++)

在每个 `process_order` 周期 (或专门的 eval 周期) 运行:

```cpp
struct FpEvalMetrics {
  // ——— Markout (需成交数据) ———
  struct MarkoutStats {
    double mean_10ms;
    double mean_100ms;
    double mean_500ms;
    double mean_1s;
    double mean_5s;
    double sharpe_1s;       // 1s markout / σ(1s markout)
    int sample_count;
  };
  MarkoutStats bid_markout;
  MarkoutStats ask_markout;

  // ——— 稳定性 ———
  double fp_rev_bps_p90;    // 90th percentile of |fp change|
  double fp_rev_mean_bps;
  int fp_updates;           // FP 更新次数

  // ——— Spread ———
  double effective_captured_bps;
  double gross_spread_bps;
  double capture_ratio;

  // ——— 成交对称性 ———
  double buy_ratio;         // N(bid_fills)/N(total)
  double inventory;         // 当前净持仓

  // ——— 分歧 ———
  double dispersion_bps;    // 三路径 max-min
  double dispersion_norm;   // dispersion / direct_spread

  // ——— Basis ———
  double basis_z;
  double basis_ema;

  // ——— 综合 ———
  bool fp_healthy;          // 所有检查通过
  std::string warnings;     // 警告信息
};

FpEvalMetrics evaluate_fp(
    const data::CircularBuffer<10000,2>& fp_history,
    const data::CircularBuffer<10000,1>& vp_history,
    const std::vector<FillRecord>& recent_fills,
    const BookView& direct,
    const BookView& usdt_impl,
    const BookView& usdc_impl,
    double ib_mid,
    double inventory,
    const FpEvalConfig& cfg
);
```

### 6.2 离线回测评价 (Python)

用于批量测试 FP 参数、对比不同 FP 版本:

```python
class FpEvaluator:
    """
    离线评估 FP 质量, 输出多维度报告。

    输入: 
      - fp_series: 每个时间点的 [fp_bid, fp_ask]
      - ob_series: 每个时间点的 order book snapshot
      - trade_series: 成交记录 (含 fill price, side, ts)
      - ib_series: IB mid price (可选)

    输出:
      - 各维度评分 (0-1)
      - 可视化图表
      - 参数对比报告
    """

    def evaluate(self, fp_model, data) -> FpReport:
        # 1. 模拟做市: 用 fp + half_spread 生成理论报价
        quotes = generate_quotes(fp_model, data)

        # 2. 模拟成交: 判断哪些报价会被 OB 成交
        fills = simulate_fills(quotes, data.ob_series)

        # 3. 计算 markout
        markout = compute_markout(fills, data.fp_series, windows=[0.01,0.1,0.5,1,5,30])

        # 4. 稳定性
        stability = compute_stability(data.fp_series)

        # 5. Spread 捕获
        spread_eff = compute_effective_spread(fills, quotes)

        # 6. 综合评分
        score = aggregate_score(markout, stability, spread_eff)

        return FpReport(...)

    def compare(self, fp_model_a, fp_model_b, data) -> CompareReport:
        """A/B 测试两个 FP 模型"""
        report_a = self.evaluate(fp_model_a, data)
        report_b = self.evaluate(fp_model_b, data)
        return compare(report_a, report_b)
```

### 6.3 综合评分函数

```python
def fp_health_score(metrics: FpEvalMetrics) -> float:
    """
    输出 0-1 的综合评分。
    不是简单平均 — 某些维度是硬门槛, 不通过直接 0 分。
    """
    score = 1.0

    # ——— 硬门槛 ———
    # markout 在 1s 窗口不能显著为负
    if metrics.bid_markout.mean_1s < -0.5 * metrics.gross_spread_bps:
        return 0.0  # bid 方向持续被割
    if metrics.ask_markout.mean_1s < -0.5 * metrics.gross_spread_bps:
        return 0.0  # ask 方向持续被割

    # dispersion 不能持续过大
    if metrics.dispersion_norm > 3.0:
        return 0.0  # 三路径严重分歧, 不适合做市

    # ——— 软权重 ———
    # markout positive: 0-30 分
    score *= 0.3 * min(1.0, metrics.bid_markout.sharpe_1s / 2.0 + 0.5)
    score *= 0.3 * min(1.0, metrics.ask_markout.sharpe_1s / 2.0 + 0.5)

    # 稳定性: 0-15 分
    rev_penalty = max(0.0, 1.0 - metrics.fp_rev_frac / 0.3)
    score *= 0.15 * rev_penalty + 0.85

    # 有效 spread: 0-15 分
    score *= 0.15 * metrics.capture_ratio + 0.85

    # 成交对称性: 0-10 分
    sym_score = 1.0 - abs(metrics.buy_ratio - 0.5) * 2.0
    score *= 0.10 * max(0.0, sym_score) + 0.90

    return score
```

---

## 评价的输入数据要求

### 必须的数据

| 数据 | 来源 | 用途 |
|------|------|------|
| FP 历史 | `fps_map_[currency].fps` circular buffer | 稳定性, markout reference |
| OB snapshot | `depth_map`, `bbo_map` | 模拟成交, 路径分歧 |
| 成交记录 | 回测: mock engine 输出; 实盘: `on_order_done` 记录 | markout |

### 推荐的数据

| 数据 | 来源 | 用途 |
|------|------|------|
| IB mid | `bbo_map[IB_idealpro]` | basis 评价 |
| TFI | `trades_data` | 趋势环境标记 |

---

## 评价的使用场景

### 场景 1: FP 参数调优

```
for each (ρ, a_imb, b_flow, c_edge):
  run_backtest(fp_params) → evaluate() → fp_health_score
  pick best score
```

### 场景 2: 在线监控

```
每个 process_order 周期:
  eval = evaluate_fp(current_state)
  if eval.fp_healthy == false:
    → 暂停该 currency 的做市
    → 报警
```

### 场景 3: FP 版本 A/B 对比

```
fp_v1: median(bids), median(asks)         # 当前实现
fp_v2: 0.7×robust_mid + 0.3×exec_mid     # Phase 1
fp_v3: fp_v2 + a_imb×imbalance×spread     # Phase 1 + OBI

evaluator.compare(fp_v1, fp_v2, fp_v3, data)
→ 输出: v2 比 v1 好 23%, v3 比 v2 好 8%
```

---

## 与其他模块的关系

```
                    ┌─────────────┐
                    │  FpEval     │ ← 本次新增
                    └──────┬──────┘
          markout ← ─ ─ ─ ┤ ─ ─ ─ → fp_healthy / warnings
                          │
    ┌─────────┐    ┌──────┴──────┐    ┌────────────────┐
    │  FPG    │    │ OrderProcessor│    │  Risk Monitor  │
    │ (生成FP) │    │ (读取health)  │    │  (独立报警)    │
    └─────────┘    └──────────────┘    └────────────────┘
```

- **FPG**: 生成 FP, 被 FpEval 评价
- **OrderProcessor**: 读取 `fp_healthy` flag, 不健康时暂停做市
- **Risk Monitor**: 独立验证 FpEval 的输出, 防止评价函数自身故障

---

## 实施建议

1. **先做 Python 离线版** — 用回测数据跑, 可以快速迭代评价逻辑和可视化
2. **再做 C++ 在线版** — 只做核心 markout 统计 + 分歧检测, 不需要完整评分
3. **online eval 保持轻量** — 每个 FP 更新周期只做 O(1) 更新, 不在热路径做 O(N) 扫描
4. **评价函数本身也需要被评价** — 定期检查 eval 的 false positive/negative 率
