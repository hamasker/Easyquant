# 交易引擎

## MockTradeEngine (本地撮合)

### 撮合流程

```
DoSendOrder → 拒单检查(qty≤0/price≤0/无效inst) → reject
           → 自成交检查(本地簿同inst反向单+价格交叉) → reject
           → 外部BBO撮合
             ├─ 对价: fill_qty = min(qty_left, BBO_size × match_fill_ratio/100)
             ├─ 跨价: 全吃 BBO 量
             ├─ 全成交 → on_order_done
             ├─ 部分成交 → on_order_update(等后续BBO补足)
             └─ 未成交 → 入簿 → 下次 BBO 更新 MatchAll
```

### 撮合参数

| 参数 | 作用 |
|------|------|
| `match_delay_ms` | 订单接受后延迟 N ms 才撮合, 0=即时 |
| `match_fill_ratio` | 对价成交比例(%), 0=必须跨价才成交, 100=全吃 |
| `record_dir` | 设值后订单事件写 `orders.csv` (ACCEPTED/FILLED/REJECTED/CANCELLED) |

### 订单回调

| 回调 | 触发条件 |
|------|---------|
| `on_order_accepted` | 通过检查 |
| `on_order_rejected` | qty≤0 / price≤0 / 无效inst / 自成交 |
| `on_order_update` | 部分成交 |
| `on_order_done` | 完全成交 |
| `on_order_cancelled` | 撤单成功 |
| `on_order_cancel_failed` | 撤单失败(不在簿中) |

### BBO 更新

WSFeed dispatch 路径中, BBO 数据同步更新撮合引擎: `UpdateBBO(inst, bid, ask, qty)` → 新行情到达触发 `MatchAll(inst)`。

## REST 实盘引擎

基类 `RestTradeEngine` (继承 MockTradeEngine)。提供 HTTPS POST + HMAC Sign + Base64 + Token bucket 限速。

### 各交易所

| 引擎 | API | 签名 | 费率 |
|------|-----|------|------|
| KrakenTradeEngine | /0/private/AddOrder | HMAC-SHA512 b64+nonce | — |
| BinanceTradeEngine | /api/v3/order | HMAC-SHA256 hex | spot |
| BinanceSwapTradeEngine | /fapi/v1/order | HMAC-SHA256 hex | swap |
| OKXTradeEngine | /api/v5/trade/order | HMAC-SHA256 b64+ts | spot |
| CoinbaseTradeEngine | /api/v3/brokerage/orders | HMAC-SHA256 hex | spot |

行为: api_key 为空 → 退回本地撮合。prod 模式 + 空 → throw。
`front_address` config 透传; Token bucket: `max_limit_rate` + `decay_rate_per_sec`。
