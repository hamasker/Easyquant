# CLAUDE.md

## 启动

```bash
# 本地编译 (WSL, conda protobuf 7.35)
cd /home/baser/Working/MX/Easyquant && ./compile.sh
./build/ext/coinsimulator/coinrunner -f config/mock_config.json

# 东京服务器部署
ssh tokyo /opt/Easyquant/sync.sh   # git pull + proto重生成 + 编译
```

## 架构概览

```
配置 JSON → InitMockFramework → InitConfig → InitEngines → InitStrategy → InitFeed → Run
  mock:    WSFeed(真实行情) + MockTradeEngine(本地撮合)
  backtest: MmapFeed(二进制回放) + MockTradeEngine
  prod:    WSFeed(真实行情) + 实盘引擎 + api_key 必须
```

## 关键文件

| 文件 | 职责 |
|------|------|
| `strategy/taking_all_multi.cpp` | 策略入口: on_init, on_datainfo, do_calculations |
| `strategy/volume_pairs.cpp` | 各所Top成交量pair拉取+去重订阅 |
| `strategy/fair_price_generator.cpp` | FP计算: USDT/USDC/USD/Forex/Digital |
| `strategy/data_process.cpp` | 行情提取: fetch_data, weighted_price |
| `ext/coinsimulator/feed/ws_feed.cpp` | 多所WS连接+消息解析(v1/v2/Exchange) |
| `ext/NovaBase/src/nvws/ws_websocket_client.cpp` | 底层SSL WS客户端 |
| `include/common/data.h` | InstrumentData, depths_data, TailFillModel等 |
| `include/common/scheduler.h` | ModuleScheduler(四谓词调度) |
| `include/common/drift_table.h` | OB档位drift表加载+查表 |
| `include/common/symbol_mapping.h` | 交易所symbol统一映射(XBT↔BTC等) |
| `include/common/turnover_pairs.h` | Turnover订阅管理(O(1)去重) |
| `include/common/volume_pairs.h` | Top成交量pair获取接口 |
| `include/taking_all_multi.h` | TakingDemo类声明 |
| `config/mock_config.json` | mock模式配置(WSL路径) |

## 详细文档

- `docs/architecture.md` — 架构、数据流、模式检测
- `docs/config.md` — 配置字段全量参考
- `docs/data-engine.md` — WSFeed/MmapFeed/数据处理
- `docs/trading-engine.md` — 撮合引擎 + REST 实盘
- `docs/fp/README.md` — FP 模块总览
- `docs/fp/current-state.md` — FP 当前实现
- `docs/fp/forex-fp-design.md` — Forex FP 设计方案
- `docs/fp/fp-evaluation.md` — FP 评价函数
- `docs/fp/eval-backtest-guide.md` — Python离线评价
- `docs/fp/ob-drift-table.md` — OB档位Drift表使用指南
- `docs/known-issues.md` — 已知问题和约定

## 数据流

```
WSFeed IO线程 (每所独立):
  SSL_read → OnMessage → JSON parse → ProcessRawMessage
    ├─ Binance: {"e":"trade"/"bookTicker",...}
    ├─ Kraken v2: {"channel":"trade"/"ticker"/"book","type":"snapshot/update","data":[...]}
    ├─ OKX: {"arg":{"channel":"trades"/"bbo-tbt"}, "data":[...]}
    ├─ Coinbase(Exchange): {"type":"match"/"ticker"/"snapshot"/"l2update",...}
    └─ Gate.io: {"method":"trades.update"/"depth.update","params":[...]}
         ↓ dispatch() → on_datainfo → fetch_data → InstData_ 写入

主循环:
  Poll feeds → on_poll → do_calculations → ProcessReminders → yield
  on_reminder(10ms): do_calculations 兜底
```

## do_calculations 调度

```
do_calculations(ts):
  flag_data_ready? → false → return
  should_disconnect → process_disconnect (撤所有单)
  should_negative   → process_negative   (FP异常→撤单)
  should_fp         → process_fp         (FPG.update)
  should_order      → process_order      (查drift表→选档位→下单)
```

## 策略现状

- **FP**: 五层定价(USD→USDT→USDC→Forex→Digital), 成交额驱动
- **下单**: OB档位drift表查表, 每侧最多3档, 撤单时只撤偏离>1tick的订单
- **风控**: FP s_bps>50bps 撤单, dispersion过大跳过, OB过期撤单
- **turnover**: 5所Top20 USDT pair的trade数据驱动FP触发

## WebSocket 交易所一览

| 交易所 | WS版本 | URL | trade频道 |
|--------|:-----:|-----|:---------:|
| Binance | v1 | stream.binance.com:9443 | `@trade` |
| Binance swap | v1 | fstream.binance.com | `@trade` |
| Kraken | **v2** | ws.kraken.com/v2 | `trade` (每笔推送) |
| OKX | v5 | ws.okx.com:8443/ws/v5/public | `trades` |
| Coinbase | Exchange | ws-feed.exchange.coinbase.com | `matches` |

## 编解码差异 (本地 vs 东京)

| | 本地 WSL | 东京 |
|:--|:--|:--|
| protobuf | conda 7.35 | apt 3.12 |
| proto文件 | Git版本(conda) | sync.sh自动重生成(apt) |
| CMake | CONFIG模式 | MODULE模式(自动检测) |
| compile.sh | -j32 | sync.sh用-j2 |

## 约定

- 加 JSON 参数 → `STRATEGY_CONFIG_FIELDS` X-macro (strategy_config.h)
- prod 依赖 `https_proxy` 环境变量
- channels 统一用通用名: `bbo/depth/trade`, 引擎 `MapChannels()` 自动映射各所原生名
- 新增交易所: 改MapChannels + ExtractSymbol + OnOpen(订阅) + ProcessRawMessage(解析)
- 新增symbol映射: 改 `symbol_mapping.h` 的 kraken_x_map()
- 东京部署: `ssh tokyo /opt/Easyquant/sync.sh`
- 本地WSL路径: `/home/baser/Working/MX/Easyquant/`, 东京路径: `/opt/Easyquant/`
