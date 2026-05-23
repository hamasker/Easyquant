# 数据引擎

## WSFeed — 多交易所 WebSocket

每个交易所一个独立 IO 线程, SSL_read 阻塞模型(无 sleep), min 间隔 8-36μs。

| 交易所 | 频道 | WS URL |
|--------|------|--------|
| Binance spot | bookTicker | `stream.binance.com:9443/ws` |
| Binance swap | bookTicker | `fstream.binance.com/ws` |
| Kraken | ticker+book(25档) | `ws.kraken.com` |
| OKX | bbo-tbt | `ws.okx.com:8443/ws/v5/public` |
| Coinbase | ticker | `ws-feed.exchange.coinbase.com` |

- `https_proxy` → HTTP CONNECT 隧道
- `ws_front_address` (config 透传) > 硬编码 URL
- 20s 无数据发 ping 保活; 2s 失败重试; 10s 超时强制重连
- OKX/Coinbase 文本心跳("ping")过滤; Kraken JSON 心跳("heartbeat")过滤
- 通用频道名 `bbo/depth/trade` → `MapChannels()` → 各所原生频道名

## Kraken 本地订单簿维护

```
book snapshot(bs/as) → local_book 初始化 → 合成 NovaCoinDepthLVN(100档) → dispatch qtype=8
book increment(b/a)  → apply(qty=0删, qty>0更新) → 合成全档 → dispatch qtype=8
```

`local_book` 为 `std::map<price, qty>` (bids 降序, asks 升序), 按键 `inst_id.key()` 存储。

## MmapBacktestFeed — 二进制回测

格式: `[type:1B][ts:8B][inst_id:28B][depth:170B | trade:25B]`
mmap 零拷贝, 1000条/批读取。149MB 0.17s 跑完。

Python 转换器: `python/convert_parquet_to_bin.py <exchange> <symbol> <date>`

## on_datainfo 数据处理

```
on_datainfo (WSFeed IO线程):
  fetch_data → extract_depth/bbo/trade → InstData_ 写入
  更新 global_ts
  trade 成交量 → turnover 累加到 scheduler
  返回 (μs级)
```

去重: `md.update_time <= dd.server_ts` (含相同时间戳)。

## 重连策略

- 连接失败(CLOSED): 等 2s 重试
- 卡在 CONNECTING: 等 10s 强制重连
- OnClose/OnFail → ResetConnection → Poll 自动重连
