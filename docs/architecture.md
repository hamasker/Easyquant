# 架构

## 初始化流程

```
main() → StrategyRunner::Initialize()
  InitMockFramework()     // 框架初始化
  InitConfig()            // 加载 JSON + 设置日志
  InitEngines()           // 解析 Trade.TradeEngine → 创建引擎 + 模式自检
  InitStrategy()          // 创建 TakingDemo → on_init → subscribe instruments
  InitFeed()              // 按模式创建数据源
    mock/prod: InitProdFeeds() → WSFeed × N
    backtest:  MmapBacktestFeed
  Run()                   // 主循环
```

## 模式自检

```
InitEngines() 解析 Trade.TradeEngine:
  有 BinanceSpotEngine/KrakenSpotEngine/... → prod
  全 MockEngine + Quote.backtest 有数据       → backtest
  全 MockEngine + Quote.<exch>.enabled=true   → mock
  混合                                        → throw
  全 MockEngine + 两者都没有                   → throw
```

## 数据流

```
WSFeed IO线程 (每所独立线程):
  SSL_read(阻塞) → 收到帧 → OnMessage → ProcessRawMessage → dispatch → on_datainfo
    on_datainfo: fetch_data(μs) → 更新 global_ts → 累加 turnover → 返回

主循环 (yield 模式):
  Poll feeds → on_poll → do_calculations → ProcessReminders → yield

on_reminder (10ms 定时):
  do_calculations 兜底
```

## do_calculations 调度

```
do_calculations(ts):
  flag_data_ready? → false → return
  should_disconnect → process_disconnect
  should_negative   → process_negative
  should_fp         → process_fp (FPG.update)
  should_order      → process_order (OP.fetch_orders → SendOrder)
```

## 编译

框架(`ext/coinsimulator/`) 编为静态库 `coinrunner_framework`。改 `strategy/` 只重编译 + 链接(~8s)。
