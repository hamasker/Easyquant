# 已知问题和约定

## Kraken v2 WebSocket 集成注意事项

### XBT/BTC 双重命名（已修复）

Kraken 对 BTC 使用两套命名：
- REST API `wsname`: `XBT/USD`
- WebSocket v2 实际推送的 symbol: `BTC/USD`

**影响**: `ProcessRawMessage` 有两层 `inst_id` 查找，必须都做 XBT↔BTC 转换：

1. **外层** (`ws_feed.cpp:480-500`)：`ExtractSymbol()` → `symbol_to_inst_` lookup → 设置 `inst_id`
   - `dispatch` lambda 用这个 `inst_id` 匹配订阅者（`state.subs[i].position->instrument.key() == inst_id.key()`）
2. **Kraken v2 handler 内** (`find_inst` 闭包)：对每个 trade/book/ticker 做 XBT↔BTC 转换后匹配

如果外层 lookup 失败 → `inst_id` 无效 → dispatch lambda 匹配失败 → 数据被静默丢弃。v2 handler 内的 `find_inst` 单独工作是不够的。

**修复** (`95594e7`, `2ef928e`)：外层 lookup 增加四路转换：
```cpp
// "BTC/USD" → "XBT/USD", "XBT/USD" → "BTC/USD"
// "BTC/EUR" → "XBT/EUR" (通过 compare(0,3,"BTC") → replace → "XBT/EUR")
// "XBT/..." → "BTC/..."
```

### Kraken v2 订阅格式

- **发送**: `{"method":"subscribe","params":{"channel":"trade","symbol":["BTC/USD"]}}`
- **响应**: `{"method":"subscribe","result":{...},"success":true}` — 在 `ProcessRawMessage` 中被过滤 (`data.contains("method")`)
- **数据推送**: `{"channel":"trade","type":"update","data":[{...trade objects...}]}` — 无 `method`/`success` 字段
- symbol 格式: `BASE/QUOTE` (如 `BTC/USD`, `ETH/EUR`)，非 v1 的 Z-prefix 格式

### v2 多条 JSON 合并

Kraken v2 可能将多条 JSON 拼在同一个 WebSocket frame 中（由换行符 `\n` 分隔）。`OnMessage` 中 parse 失败时会尝试换行分割逐条解析。

### 其他 Kraken 注意事项

- `MakeExchangeSymbol` 对 Kraken 的转换：`btc` → `xbt`，长 ticker strip X 前缀，长 currency strip X/Z 前缀
- 订阅时 `OnOpen` 将 XBT→BTC 转换（Kraken v2 用 BTC），但 `symbol_to_inst_` key 保持 XBT 格式（由 `MakeExchangeSymbol` 产生）
- 此差异是 XBT/BTC 双重命名问题的根源——两边不一致需要双向转换

## 编解码差异

| | 本地 WSL | 东京 |
|:--|:--|:--|
| protobuf | conda 7.35 | apt 3.12 |
| proto 文件 | Git 版本 (conda) | sync.sh 自动重生成 (apt) |
| CMake | CONFIG 模式 | MODULE 模式 (自动检测) |

## 配置管理

- `config/mock_config.json` 已 gitignore — 环境特定配置，两边各自维护
- `config/prod_config.json` 同样 gitignore
- 东京部署后需手动重建 mock_config.json（`Quote` 部分需包含各交易所 WS feed 配置）
