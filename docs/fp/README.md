# Fair Price 模块

## 目录

| 文件 | 内容 |
|------|------|
| `current-state.md` | 当前 FP 实现状态、数据流、各层级详细说明 |
| `forex-fp-design.md` | Forex FP 盈利分析 + 完整设计 (Kraken local fair + IB basis 框架) |
| `fp-evaluation.md` | FP 评价函数: 6 维度指标、C++/Python 实现、综合评分公式 |
| `eval-backtest-guide.md` | Python 离线评价实现: 用 trade 数据模拟做市成交 + 计算 markout |

## 设计原则

- **稳定币 FP**: 只做准确定价, 锚定 1.0 保护仓位, spread capture 即可盈利
- **Forex FP**: Kraken 内部三路径 (direct + USDT implied + USDC implied) 为主定价源; IB 仅作 external basis 信号; 必须有 sellable/buyable 退出约束; 最终验证靠 markout
- **FP 不做长线预测**: 微观结构信号 (imbalance, TFI, local_edge) 用于短时调整 (< 秒级), 不在 FP 层判断大方向

## 快速定位

```
FP 入口:        strategy/taking_all_multi.cpp:352  process_fp()
FP 引擎:        strategy/fair_price_generator.cpp  FairPriceGenerator::update()
FP 头文件:      include/fair_price_generator.h
配置:           include/configs/strategy_config.h  + stable_config.h
数据结构:       include/common/data.h              fair_price_data / depths_data / vol_data
常数:           include/constant.h                 FOREX / available_forex / instruments
货币定义:       include/common/data_currency.h     forex_currencies / digital_currencies
```

## FP 层级结构

```
Layer 0: USD (硬编码 1.0)
    ↓
Layer 1: USDT/USD (三路径 equilibrium, 2bp 一致性阈值)
    ↓
Layer 2: USDC/USD (五路径 median + Circle 1:1 锚定)
    ↓
Layer 3: Forex (EUR/GBP/AUD/CHF/CAD/PAXG) — 三路径 median, 依赖 Layer1+2
    ↓
Layer 4: Digital (BTC/ETH/SOL/XRP) — digital_fp 引擎, 多交易所多信号源
    ↓
Layer 5: 合成 instrument (如 BTC/EUR) — Layer 4 ÷ Layer 3 合成
```

定价依赖链: USD(固定) → USDT → USDC → Forex → Digital → 合成inst
USDT 定价偏 1bp, 全账簿偏 1bp, 故 USDT 是核心风险链的起点。
