# 已知问题 & 约定

## 已知问题

| 问题 | 影响 | 状态 |
|------|------|:--:|
| bn_swap(fstream) WSL代理间歇断联 | Binance永续数据偶尔断 | WSL网络, 纯Linux无此问题 |
| Kraken 26 instrument × 3频道 订阅慢 | 首次收数据需~15s | 建议精简到实际需要的 instrument |
| TakingDemo.so 编不过 | 头文件签名不一致 | 只维护 coinrunner |
| restart 时偶发 malloc 损坏 | 退出时 crash | nvws 库问题, 不影响运行 |

## 约定

- **加 JSON 参数**: `include/configs/strategy_config.h` → `STRATEGY_CONFIG_FIELDS(X)` 加一行。子块(Stable/Order/等) 有各自 X-macro
- **prod 模式**: 依赖 `https_proxy` 环境变量走 HTTP CONNECT 隧道
- **channels**: 统一用通用名 `bbo/depth/trade`, 引擎 `MapChannels()` 自动映射各所原生名
- **新增交易所**: 改 6 处: `MakeExchangeSymbol` + `ExtractSymbol` + `WSFeed::Initialize(URL)` + `OnOpen(订阅)` + `ProcessRawMessage(解析)` + `MapChannels`
- **编译**: 框架静态库 `coinrunner_framework`, 改 `strategy/` 只重编+链接(~8s)
- **日志**: 每个模块有自己的 TAG 前缀 (如 `[WSFeed]`, `[Runner]`, `[MockTrade]`, `[OnStrategy]`)
