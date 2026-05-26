# CLAUDE.md

## 启动

```bash
cd /home/baser/Working/MX/Easyquant && ./compile.sh
./build/ext/coinsimulator/coinrunner -f config/mock_config.json
./build/ext/coinsimulator/coinrunner -f config/prod_config.json
./build/ext/coinsimulator/coinrunner -f config/backtest_config.json -d data.bin
```

## 架构概览

```
配置 JSON → InitMockFramework → InitConfig → InitEngines → InitStrategy → InitFeed → Run
  mock:    WSFeed(真实行情) + MockTradeEngine(本地撮合)
  backtest: MmapFeed(二进制回放) + MockTradeEngine
  prod:    WSFeed(真实行情) + 实盘引擎 + api_key 必须
```

模式由 `Trade.TradeEngine` 配置自检。详见 `docs/architecture.md`。

## 关键文件

| 文件 | 职责 |
|------|------|
| `ext/coinsimulator/main.cpp` | 入口 |
| `ext/coinsimulator/strategy_runner.h/cpp` | 生命周期, InitEngines/InitProdFeeds |
| `ext/coinsimulator/feed/ws_feed.h/cpp` | 多所WS,通用频道,Kraken本地簿 |
| `ext/coinsimulator/feed/mmap_backtest_feed.h/cpp` | mmap回测 |
| `ext/coinsimulator/mock/mock_trade_engine.h/cpp` | 撮合引擎 |
| `ext/coinsimulator/mock/rest_trade_engine.h/cpp` | REST基类 |
| `ext/coinsimulator/mock/*_trade_engine.*` | 各所实盘 |
| `ext/coinsimulator/coinrunner_log.h` | 日志宏 |
| `ext/NovaBase/src/nvws/ws_websocket_client.cpp` | 底层WS |
| `strategy/taking_all_multi.cpp` | 策略入口 |
| `strategy/fair_price_generator.cpp` | FP计算(已移植) |
| `include/common/scheduler.h` | 模块调度器 |
| `config/*.json` | 三套配置文件 |

## 详细文档

- `docs/architecture.md` — 架构、数据流、模式检测
- `docs/config.md` — 配置字段全量参考
- `docs/data-engine.md` — WSFeed/MmapFeed/数据处理
- `docs/trading-engine.md` — 撮合引擎 + REST 实盘
- `docs/fp-design.md` — USDT Fair Price 设计和触发机制
- `docs/fp/README.md` — FP 模块总览, 层级结构, 快速定位
- `docs/fp/current-state.md` — FP 当前实现详情, 各层逐解析
- `docs/fp/forex-fp-design.md` — Forex FP 盈利分析, 问题诊断, 改进方案
- `docs/known-issues.md` — 已知问题和约定

## 策略现状

- `FairPriceGenerator.cpp` 已移植(2237行)
- `do_calculations` 框架就位(scheduler 四谓词)
- 待补: `OrderProcessor.cpp` 移植, `process_fp/order/negative` 实现

## 约定

- 加 JSON 参数 → `STRATEGY_CONFIG_FIELDS` X-macro
- prod 依赖 `https_proxy` 环境变量
- 框架(coinsimulator) 静态库, 改 strategy/ ~8s 增量编译
- channels 统一用通用名: `bbo/depth/trade`, 引擎自动映射
