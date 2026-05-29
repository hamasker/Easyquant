# 已知问题和约定

## USDT FP stale depth (已修复)

`calculate_fp_usdt` 需 5 个 Kraken depth pair 全部新鲜（5s 内）。Kraken v2 book handler 原要求 `!bids.empty() && !asks.empty()` 才 dispatch DepthLVN，交叉对（usdt_eur, usdc_usdt 等）orderbook 薄，某侧长期为空 → DepthLVN 永不 dispatch → `depth_map.local_ts` 永久为 0 → 5 对全 stale。

**修复** (`ws_feed.cpp:656`): `&&` → `||`，单侧有数据即 dispatch。

## FP 启动时空 buffer 崩溃 (已修复)

`calculate_fp_usdt` 遇到 stale 提前返回时未写 `fps_map_[USDT]`，后续 `calculate_fp_usdc/forex` 调用 `get_latest()` 抛异常。

**修复**: `calculate_fp_usdt` 返回 bool；`update()` 检查结果和 buffer 非空后才执行后续计算。

## O(1) symbol 查找 & dispatch (已优化)

`ws_feed.cpp:PrefillSymbolVariants()` 在 init 时预计算 XBT↔BTC 转换 + 小写变体到 `symbol_to_inst_`，热路径只需一次 `find()`（原 4 次 XBT↔BTC + O(n) 大小写遍历）。

`ws_feed.cpp:BuildDispatchIndex()` 预建 `(symbol,qtype)→sub_index` 映射，dispatch 从 O(n) 遍历降为 O(1)。

## XBT/BTC 双重命名（已通过 PrefillSymbolVariants 简化）

Kraken 使用两套命名：REST `XBT/USD`，WS v2 推送 `BTC/USD`。外层 lookup 和 v2 handler 内 `find_inst` 都需要双向转换。修复通过 `PrefillSymbolVariants` 预计算所有变体，热路径只需单次 `find()`。

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
