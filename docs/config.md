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
    "backtest": {"begin_time": "20251202.00:00:00", "input_dirs": [...]}
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
