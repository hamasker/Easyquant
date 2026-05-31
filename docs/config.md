# 配置参考

三个配置文件: `config/mock_config.json`, `prod_config.json`, `backtest_config.json`。

## Trade.TradeEngine

```json
{"name": "MockEngine", "match_delay_ms": 0, "match_fill_ratio": 100, "record_dir": ""}
{"name": "KrakenSpotEngine", "front_address": "api.kraken.com", "ws_front_address": "wss://...", "api_key": "", "api_secret": "", "ws_core": -1}
```

| name | 引擎类 | 用途 |
|------|--------|------|
| MockEngine | MockTradeEngine | 本地撮合 |
| KrakenSpotEngine | KrakenTradeEngine | Kraken REST |
| BinanceSpotEngine | BinanceTradeEngine | Binance REST |
| BinanceSwapEngine | BinanceSwapTradeEngine | Binance 永续 |
| OKXSpotEngine | OKXTradeEngine | OKX REST |
| CoinbaseSpotEngine | CoinbaseTradeEngine | Coinbase REST |

| 字段 | 说明 |
|------|------|
| `front_address` | REST API host |
| `ws_front_address` | WS URL (覆盖硬编码) |
| `ws_core` | WS 线程绑核 |
| `match_delay_ms` | 撮合延迟(ms) |
| `match_fill_ratio` | 成交比例(%), 0=必须跨价, 100=全吃 |
| `max_limit_rate` | API 限速(次/s) |
| `decay_rate_per_sec` | 限速恢复速率 |
| `record_dir` | 订单录制目录 |
| `api_key/api_secret/api_passphrase` | 交易所 API 密钥 |

## Quote — 数据源

channels 用通用名, 引擎自动映射各所最快频道:

| 通用名 | Binance | Kraken | OKX | Coinbase |
|--------|---------|--------|-----|----------|
| `bbo` | bookTicker | ticker | bbo-tbt | ticker |
| `depth` | depth@100ms | book(25档) | books5 | level2 |
| `trade` | trade | trade | — | matches |

```json
"Quote": {
    "kraken":  {"enabled": true, "channels": ["bbo", "depth", "trade"], "core": -1},
    "backtest": {"begin_time": "20251202.00:00:00", "end_time": "20251203.00:00:00", "input_dirs": [...]}
}
```

## Server.Log

```json
"Server": {"Log": {
    "path": "log/backtest.log",     // 自动加时间戳后缀, 自动建父目录
    "file_level": "TRACE",          // TRACE/DEBUG/INFO/WARNING/ERROR/FATAL
    "screen_level": "ERROR",
    "async_log": false
}}
```

日志格式: `[HH:MM:SS.us] [L] [PID] message`

## Strategy — X-macro 参数

`include/configs/strategy_config.h`: `STRATEGY_CONFIG_FIELDS(X)` 加一行 `X(name, type, default)` 即可。子块: Stable/Order/Taker/Volatility/Cuscore/Verbose — 各自有独立 X-macro。

### Strategy.Base (StableConfig) — 调度器参数

`include/configs/stable_config.h` X-macro，可在 JSON `Strategy.Base` 下覆盖：

| 字段 | 默认 | 说明 |
|------|------|------|
| `fp_turnover_usd` | 2000 | FP 触发成交额阈值 ($) |
| `order_turnover_usd` | 35000 | Order 触发成交额阈值 ($) |
| `fp_interval_min_ms` | 5 | FP 最小间隔 (ms)，防 flood |
| `fp_interval_max_ms` | 200 | FP 最大间隔兜底 (ms)，冷清时强制触发 |
| `order_interval_min_ms` | 1000 | Order 最小间隔 (ms)，防 rate limit |
| `order_interval_max_ms` | 3500 | Order 最大间隔兜底 (ms) |
| `negative_interval` | 5.0 | negative 检测间隔 (ms) |
| `print_interval` | 1.0 | should_print 打印间隔 (s)，0=禁用 |

调度器谓词 (`include/common/scheduler.h`):
- `should_fp`: `acc_usd_fp >= fp_turnover_usd AND ts - last_fp_ts > fp_interval_min`
- `should_order`: `acc_usd_order >= order_turnover_usd AND ts - last_order_ts > order_interval_min OR ts - last_order_ts > order_interval_max`
- `should_disconnect`: 断连标记 OR aim exchange 30s 无数据 → 撤单
- `should_print`: `print_interval > 0 AND ts - last_print_ts > print_interval`（每秒打印 global_ts 北京时间）

### Strategy 顶层字段

| 字段 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `backtest` | bool | false | 回测模式标识, 设为 true 后跳过 TopN turnover 拉取 |
| `flag_prod` | bool | false | 实盘模式标识 |

### Quote.backtest 时间格式

`begin_time` / `end_time` 格式: `"YYYYMMDD.HH:mm:ss"`，按 **UTC** 解析（`timegm`）。
例: `"20260320.00:00:00"` → UTC 零点 → 北京时间 08:00:00。

内部转换为 UTC epoch 纳秒后与数据记录时间戳比对。
