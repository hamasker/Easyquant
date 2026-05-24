# USDT Fair Price 设计与优化

## 架构

```
行情数据 → on_datainfo → fetch_data → InstData_ (depth/bbo/trade)
                                ↓
                          accumulate_turnover (全市场成交额累加)
                                ↓
                    scheduler.acc_usd_fp >= fp_turnover_usd?
                                ↓ 是 (且距上次 ≥500μs)
                         process_fp()
                            ├── fetch_data_all   (拉取最新行情)
                            └── fpg_->update(ts)
                                  ├── calculate_fp_usdt()   ← 基准
                                  ├── calculate_fp_usdc()
                                  ├── calculate_fp_usd()
                                  ├── calculate_fp_forex()  (EUR/AUD/GBP...)
                                  ├── calculate_fp_digital() (BTC/ETH/SOL...)
                                  └── update_fp_insts()
```

## USDT 定价逻辑 (`calculate_fp_usdt`)

```
USDT/USD 直接盘口 ──┐
USDT/EUR × EUR/USD ─┼──→ 三路径一致性检测 (median, 2bp阈值)
USDC/USD ÷ USDC/USDT─┘       ↓ 异常路径被丢弃
                        仅正常路径 Level 合并 → solve_equilibrium_price
                                                      ↓
                                              [fp_bid, fp_ask]
```

**关键设计决策：**

1. **三路径一致性检测** — 每条路径独立算 mid-price，用 median 检测异常
   - 三条都正常: 全部参与 equilibrium
   - 一条异常: 丢弃，只用两条
   - 两条及以上异常: 触发 AIM_EXCH_INVALID

2. **USDT 是纯定价基准，不做预测** — 多交易所取中位数，加权按成交量，不预测方向

## FP 触发策略

```
trigger: acc_usd_fp >= fp_turnover_usd  AND  ts - last_fp > 500μs
         ─────────────────────────────       ────────────────────────
              成交额驱动 (做市逻辑)              CPU 保护 (最多 2000/s)
```

**成交额来源：** 全市场所有订阅 pair 的 trade 累加（不限交易所、不限币种）

**动态 Top Pair：** 启动时从 Binance/OKX/Gate.io API 拉取 24h 成交量前 20 的 USDT 对，自动订阅 trade 频道

| 配置项 | 推荐值 | 说明 |
|--------|--------|------|
| `fp_turnover_usd` | 2000 | 中位 ~50ms/次, 冷市 ~1s/次 |
| `fp_interval_min_ns` | 500μs | 下限, 保护 CPU |
| 一致性偏离阈值 | 2bp | 路径异常判定 |

## 为什么成交额驱动而不是定时驱动

做市策略的核心逻辑: 市场活跃 → 价格在变 → 需要刷新 FP → 但触发信号应该是成交量而不是时间

- 成交额触发: 市场越活跃 → FP 刷新越频繁 (自适应节奏)
- 定时触发: 死市无意义消耗 CPU, 活跃时反而可能滞后

## 为什么 USDT 不预测

所有币种的 FP 都从 USDT 派生:
```
btc_fp = btc/usdt_wp × usdt_fp
eth_fp = eth/usdt_wp × usdt_fp
```

USDT 定价偏 1bp → 全账簿偏 1bp。预测错误会传染。准确性 > 预测性。

预测性放在下单层（判断短期方向决定挂单时机），不在定价层。
