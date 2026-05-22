# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 启动

```bash
cd /home/baser/Working/MX/Easyquant
./compile.sh

# 自动检测模式（从 config JSON 判断）
./build/ext/coinsimulator/coinrunner -f config/mock_config.json      # mock
./build/ext/coinsimulator/coinrunner -f config/prod_config.json       # prod
./build/ext/coinsimulator/coinrunner -f config/backtest_config.json -d data.bin  # backtest

# 已有数据
/data/bin/backtest_binance_btcusdt_2025-12-02.bin   # 149MB, 756849条, 0.17s跑完
```

## 整体架构

```
配置 JSON (三选一: mock/prod/backtest)
  → StrategyRunner::Initialize()
    → InitMockFramework() → InitConfig() → InitEngines() → InitStrategy() → InitFeed()
  → Run() 主循环: Poll feeds → on_poll → ProcessReminders → yield

三种模式:
  mock:    WSFeed(真实行情) + MockTradeEngine(本地撮合)
  backtest: MmapFeed(二进制回放) + MockTradeEngine(本地撮合)
  prod:    WSFeed(真实行情) + 实盘引擎(KrakenTradeEngine等) + 必须配api_key
```

## 配置结构

三个配置文件: `config/mock_config.json`, `config/prod_config.json`, `config/backtest_config.json`

### Trade.TradeEngine (驱动模式选择)

```json
"Trade": {
    "TradeEngine": [
        {"name": "MockEngine", "match_delay_ms": 0, "match_fill_ratio": 100, "record_dir": ""},
        // 或实盘引擎:
        {"name": "KrakenSpotEngine", "front_address": "api.kraken.com", "ws_front_address": "wss://...",
         "api_key": "", "api_secret": "", "ws_core": -1, "match_delay_ms": 50, "match_fill_ratio": 0}
    ]
}
```

| name | 引擎类 | 用途 |
|------|--------|------|
| `MockEngine` | MockTradeEngine | 本地撮合 |
| `KrakenSpotEngine` | KrakenTradeEngine | Kraken REST 实盘 |
| `BinanceSpotEngine` | BinanceTradeEngine | Binance REST 实盘 |
| `BinanceSwapEngine` | BinanceSwapTradeEngine | Binance 永续 |
| `OKXSpotEngine` | OKXTradeEngine | OKX REST 实盘 |
| `CoinbaseSpotEngine` | CoinbaseTradeEngine | Coinbase REST 实盘 |

模式自检规则: 全 MockEngine + Quote.backtest → backtest; 全 MockEngine + Quote.<exch>.enabled → mock; 有真实引擎 → prod; 混合 → 报错。

### Quote (数据源配置)

```json
"Quote": {
    "binance":   {"enabled": true, "channels": ["bookTicker"], "core": -1},
    "binance_u": {"enabled": true, "channels": ["bookTicker"], "core": -1},
    "kraken":    {"enabled": true, "channels": ["book","ticker"], "core": -1, "api_key":"", "api_secret":""},
    "okex":      {"enabled": true, "channels": ["bbo-tbt"], "core": -1, "api_key":"", "api_secret":"", "api_passphrase":""},
    "coinbase":  {"enabled": true, "channels": ["ticker"], "core": -1, "api_key":"", "api_secret":"", "api_passphrase":""},
    "backtest":  {"begin_time": "...", "input_dirs": [...]}
}
```

ws_front_address 从 TradeEngine 配置透传，覆盖 WSFeed 硬编码 URL。

## 数据引擎

### WSFeed — 多交易所实时 WebSocket

| 交易所 | config section | WS URL | 频道 | min间隔 |
|--------|---------------|--------|------|:---:|
| Binance spot | `binance` | `stream.binance.com:9443/ws` | bookTicker | **17μs** |
| Binance swap | `binance_u` | `fstream.binance.com/ws` | bookTicker | **17μs** |
| Kraken | `kraken` | `ws.kraken.com` | book depth=25 + ticker | **8μs** |
| OKX | `okex` | `ws.okx.com:8443/ws/v5/public` | bbo-tbt | **11μs** |
| Coinbase | `coinbase` | `ws-feed.exchange.coinbase.com` | ticker | **36μs** |

**关键实现**: nvws IO 线程去掉 sleep → SSL_read 阻塞模型, min 到 8-36μs。
每个 WSFeed 独立线程, 通过 `https_proxy` 走 HTTP CONNECT 隧道。

### MmapBacktestFeed — 二进制回测

Python 转换器: `python/convert_parquet_to_bin.py <exchange> <symbol> <YYYY-MM-DD>`
格式: `[type:1B][ts:8B][inst_id:28B][depth:170B 或 trade:25B]`
零拷贝 mmap, 1000条/批, 149MB 0.17s 跑完。

### 数据处理

- `on_datainfo` 在 WSFeed IO 线程 → 只做 `fetch_data` (μs级) → 立即返回
- `on_poll` 在主循环 → 调 `do_calculations`
- `md.update_time <= dd.server_ts` 去重
- 20s 无数据自动发 ping 保活
- 重连: 2s 失败重试, 10s 超时强制重连

### Symbol 映射

| 交易所 | MakeExchangeSymbol (订阅用) | ExtractSymbol (反向匹配) |
|--------|---------------------------|-------------------------|
| Binance | `ticker+currency` 小写, `btcusdt` | `data["s"]` |
| Kraken | `TICKER/CURRENCY` 大写, `XBT/USD` | `data[3]` (pair名) |
| OKX | `TICKER-CURRENCY` 大写, `BTC-USDT` | `data["arg"]["instId"]` |
| Coinbase | `TICKER-CURRENCY` 大写, `BTC-USD` | `data["product_id"]` |

## 交易引擎

### MockTradeEngine (本地撮合)

- **撮合规则**: 新单→先检查自成交(有则 reject)→外部BBO撮合
- **对价成交**: `fill_qty = min(qty_left, BBO_size × match_fill_ratio/100)`, ratio=0 必须跨价
- **跨价成交**: 全吃 BBO 量
- **延迟**: `match_delay_ms` 控制, TryMatch 中检查 `now - accept_time < delay`
- **手续费**: 成交额×0.1%
- **仓位更新**: 买→long+, 卖→short+
- **拒绝**: qty≤0, price≤0, 无效instrument, 自成交
- **录制**: `record_dir` 设值 → 订单事件写 `orders.csv`

### REST 实盘引擎

基类 `RestTradeEngine` 提供 HTTPS POST + HMAC Sign + Base64。各交易所引擎:

| 引擎 | API | 签名 |
|------|-----|------|
| KrakenTradeEngine | POST /0/private/AddOrder | HMAC-SHA512 b64 + nonce |
| BinanceTradeEngine | POST /api/v3/order | HMAC-SHA256 hex |
| BinanceSwapTradeEngine | POST /fapi/v1/order | HMAC-SHA256 hex |
| OKXTradeEngine | POST /api/v5/trade/order | HMAC-SHA256 b64 + timestamp |
| CoinbaseTradeEngine | POST /api/v3/brokerage/orders | HMAC-SHA256 hex |

api_key 为空 → 退回 MockTradeEngine 本地撮合。prod 模式 + 空 → throw 拒绝启动。
`front_address` 从 config 透传覆盖硬编码 host。Token bucket 限速 (`max_limit_rate` + `decay_rate_per_sec`)。

### 订单回调

| 回调 | 触发 |
|------|------|
| `on_order_accepted` | 通过拒单检查 |
| `on_order_rejected` | qty≤0/price≤0/自成交 |
| `on_order_update` | 部分成交 |
| `on_order_done` | 完全成交 |
| `on_order_cancelled` | 撤单成功 |
| `on_order_cancel_failed` | 撤单失败(不在簿中) |

## 策略核心 (待补)

### 已有框架

- `ModuleScheduler` — 4 模块独立节拍调度 (FP/Order/Negative/Disconnect)
- `FairPriceGenerator` (header only) — 多币种 FP 计算接口
- `OrderProcessor` (header only) — 订单生成接口
- `data_process.cpp` — 辅助函数已齐全 (extract depth/bbo/trade, weighted price, prob, PnL)

### 参考实现 (Relaxquant)

`/home/baser/Working/MX/Relaxquant/strategy/`:
- `fair_price_generator.cpp` (2237行) — 跨交易所 USDT/USDC/USD/forex/digital FP
- `order_processor.cpp` (1124行) — 概率化量优化 + 保证金计算 + 订单生成

### 需要补的

1. **FairPriceGenerator.cpp** — 从 Relaxquant 移植, 修掉: 全局变量改成员、60s→5s stale、throw→return false、加 BBO fallback
2. **OrderProcessor.cpp** — 移植, 修掉: 全局 bool 改成员、三遍 sift 合并
3. **`do_calculations`** — 骨架 (调 scheduler 四个谓词)
4. **`process_fp`** — `FPG.update(ts)`
5. **`process_order`** — `OP.fetch_orders()` → SendOrder
6. **`process_negative`** — 检查 `BlcMng_` 余额 + 敞口上限硬止损
7. **`accumulate_turnover`** — 成交回调累加到 scheduler

### 做市策略设计要点 (已讨论)

- **Kraken 纯 maker**: spread 1-3bp + maker rebate 2bp → 毛利润 4-5bp, 被吃后不敢 taker 平(10bp 费差)
- **FP 要精确不要预测**: 做市利润来自挂单被对手 maker 平仓的概率, 加方向偏移会降低成交率
- **但需方向过滤器**: 盘口失衡 + taker buy/sell 比 + 多所价差 → 分类"现在多头是否安全", 不是预测价格
- **敞口控制**: 偏移定价自动调节 + 硬止损上限 + 大行情前缩量
- **下单规则**: FP 移动 > spread/2 全撤重挂, 成交后等 1-2s 再补, 单量按盘口深度成比例

## 关键文件地图

| 文件 | 职责 |
|------|------|
| `ext/coinsimulator/main.cpp` | 入口 |
| `ext/coinsimulator/strategy_runner.h/cpp` | 策略生命周期, InitEngines/InitProdFeeds |
| `ext/coinsimulator/feed/ws_feed.h/cpp` | 多交易所 WS, symbol映射, dispatch |
| `ext/coinsimulator/feed/mmap_backtest_feed.h/cpp` | mmap 二进制回测 |
| `ext/coinsimulator/mock/mock_trade_engine.h/cpp` | 本地撮合引擎 |
| `ext/coinsimulator/mock/rest_trade_engine.h/cpp` | REST 引擎基类 |
| `ext/coinsimulator/mock/kraken_trade_engine.h/cpp` | Kraken 实盘 |
| `ext/coinsimulator/mock/exch_trade_engines.h/cpp` | Binance/OKX/Coinbase 实盘 |
| `ext/coinsimulator/mock/strategy_api_impl.cpp` | CreateOrder/SendOrder/CancelOrder |
| `ext/coinsimulator/coinrunner_log.h` | 日志宏重定向 |
| `ext/NovaBase/src/nvws/ws_websocket_client.cpp` | 底层WS(独立线程/代理/绑核) |
| `strategy/taking_all_multi.cpp` | 策略入口, on_datainfo/on_poll/subscribe |
| `include/common/scheduler.h` | 模块调度器 |
| `include/common/data.h` | 领域类型 |
| `include/fair_price_generator.h` | FP 接口 (缺 cpp) |
| `include/order_processor.h` | Order 接口 (缺 cpp) |
| `include/taking_all_multi.h` | 策略类声明 |
| `include/configs/strategy_config.h` | 策略参数 X-macro |
| `python/convert_parquet_to_bin.py` | 数据格式转换器 |
| `config/mock_config.json` / `prod_config.json` / `backtest_config.json` | 配置文件 |

## 约定

- 日志/CSV/parquet 均 gitignore, `lib/libTakingDemoD.so` 例外
- 加 JSON 参数 → 改 `STRATEGY_CONFIG_FIELDS`
- prod 模式依赖 `https_proxy` 环境变量
- 加新交易所需改 6 处: MakeExchangeSymbol, ExtractSymbol, WSFeed::Initialize(URL), OnOpen(订阅), ProcessRawMessage(解析), DefaultChannelsForExchange
