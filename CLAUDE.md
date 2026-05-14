# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 构建与运行

- **编译 C++（全量）：** `./compile.sh` — 清空 `build/`，执行 `cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`、`make -j32`，并把 `compile_commands.json` 复制回仓库根目录（clangd 需要）。C++20，gcc/g++，x86_64 下开启 `-mavx`。
- **产物：** `lib/libTakingDemoD.so` — 由外部的 `SimTrade` 二进制作为策略插件加载。本仓库**没有**独立可执行程序。
- **回测 / 实盘：** `./backtest.sh <N>` 会执行 `ext/coinsimulator/bin/SimTrade -f config/taking_all_multi<N>.json`，然后从日志里解析 `pnl static hedge:` / `trade usd:`，并根据 config 中 `Strategy.*` 字段拼出 tag 重命名日志文件。
- **单元测试：** `test/test_runner`（由 `test/CMakeLists.txt` 构建，`add_test(NAME my_test COMMAND test_runner)`）。跑单个用例直接带参调用 `test/test_runner` — 这里没有接入 gtest，`test.cpp` 就是一个普通可执行程序。
- **每日 pair-stats 刷新 + 同步到远端：** `python/daily_update_pair_stats_and_sync.sh [YYYYMMDD]` — 先用 `kraken/depth_lvn/<day>/` 是否存在做门禁（不传日期时还要求 mtime ≥ 当天 22:00），再 patch `config/daily_update.json` 里的 `Quote.backtest.begin_time/end_time`，跑 SimTrade，跑 `python/update_pair_configs_from_stats.py`，最后用 rsync 把 `config/pair_stats/` 同步到 `$REMOTE_DEST`。

## 代码架构

本仓库是一个 C++ 做市 / 统计套利策略（`TakingDemo`），通过 `StrategyApi` 接口挂到外部交易框架（`NovaBase` / `NovaCoin2` / `coinsimulator`）上。它**不是独立应用** — `strategy/` 编出来的 `.so` 会由 `SimTrade` 通过 `-f` 指定的 config JSON 加载。

### 模块划分

- `strategy/taking_all_multi.cpp` — 入口（`STRATEGY_API_IMPLEMENT(TakingDemo)`）。实现 `on_init`、`on_depth`、`on_trade`、`on_bbo`、订单回调、`on_reminder`。在其持有的 `InstrumentData` + `StrategyConfig` 之上构造 `FairPriceGenerator`、`OrderProcessor` 以及 `BalanceManager` map。头文件在 `include/taking_all_multi.h`。
- `strategy/fair_price_generator.cpp`（+ `include/fair_price_generator.h`、`include/digital_fp_calculator.h`）— 从多交易所 depth/BBO 生成每个币种的 fair price。按币种族分发：USDT / USDC / USD / forex / digital（每个都有 `_old` 版本，由 config 的 `forex_method` / `digital_method` 选择）。结果写入以 `data::currency` 为 key 的 `fps_map_`。
- `strategy/order_processor.cpp`（+ `include/order_processor.h`）— 取 snapshot、按币对算目标订单、对接 `TradeEngine`。读取 `fps_map`、`BlcMng_`（余额）、`InstData_.depth_map`/`order_map` 和 `CFG_`。
- `strategy/data_process.cpp`（+ `include/data_process.h`）— 在 `NovaCoin*` 行情类型之上的工具层：交易所名归一化、按交易所格式化 pair 字符串、`extract_depth_data` / `extract_bbo_data` / `extract_bar_data` / `extract_trade_data` 转成 `data::depths_data`/`bbo_data`，以及模板的 `trunc_depth_data<N>` / `weighted_price_*`。
- `src/` — 策略和测试共用的基础设施：`config_reloader.cpp`（监听 config 文件，见下面"日志级别行为"）、`config_watcher.cpp`、`markout.cpp`、`record.cpp`、`sum_util.cpp`。编成静态库 `relaxquant_src`，`test_*.cpp` 文件会被从库里排除。
- `include/common/` — 领域类型（`data::currency`、`data::InstrumentData`、`depths_data`、`fair_price_data`、`order_data`、`BalanceManager`、`VolatilityMethod`、交易所 / 合约 enum）。`data.h` 是 strategy / order / fp 模块都要 include 的公共头。
- `include/configs/strategy_config.h` — **策略参数的唯一事实源。** 字段通过 `STRATEGY_CONFIG_FIELDS(X)` X-macro 声明。加参数只需加一行 `X(name, type, default)` — 宏会同时生成结构体字段和 `cfg->GetItemValue("Strategy.<name>", ...)` 的 loader。`LoadStrategyConfig` 还会记录 JSON 里实际被读到的 key，这样没用上的 key 会以 warning 暴露出来。
- `include/configs/pair_configs.h` + `config/pair_configs.yml` — 按 instrument 粒度的覆盖参数（ema_alpha、bid/ask bps 阈值、depth / s_bps 汇总分位数、fp_momentum_scale）。每天由 `config_reloader` 重新加载。
- `ext/` — 外部依赖：`NovaBase`（base / tpl / fmt / json）、`NovaCoin2`（quote/trade/api/symbol_translator — `Depth`、`BBO`、`Trade`、`Bar`、`StrategyApi`、`OrderTp`、`SecurityPosition` 都在这里）、`coinsimulator`（提供 `SimTrade` 二进制和 `InstrumentBaseInfo.csv`）、`Hermes_Release`。

### 运行时不变量

- `TakingDemo` 持有唯一的 `StrategyConfig CFG_`、`InstrumentData InstData_`、`fps_map_`（`currency → fair_price_data`）、`BlcMng_`（`currency → BalanceManager`）。`FairPriceGenerator` 和 `OrderProcessor` 构造时只持有这些对象的引用，自己不持有状态。
- `StrategyApi` 的回调（`on_depth`/`on_trade`/`on_bbo`）是框架进入策略的唯一入口。后续一切动作都由 `StrategyConfig` 里的 `fp_interval`、`order_interval`、`vol_interval`、`negative_interval`、`rebalance_*` 这些节奏控制参数驱动。
- 日志级别：`src/config_reloader.cpp` 在 init 时强制把**最大**日志级别钉到 TRACE，以便后续通过 config reload 打开 `TRACE`/`DEBUG`；**当前**级别跟随 `Server.Log.file_level`。背景见 `README_TRACE_DEBUG_LEVEL.md` / `README_TRACE_CONFIG_LEVEL.md`。
- 策略把 `ext/coinsimulator/config/InstrumentBaseInfo.csv` 当作 instrument 目录使用 — JSON 里的 `BaseInfo.base_info_path` 必须指到它。

### Include path 约定

`strategy/CMakeLists.txt` 会 glob `include/` 下全部子目录、`ext/*/include`、`ext/*/*/include`，并额外显式加上 `ext/NovaCoin2/include/{api,quote,trade,symbol_translator}` 和 `ext/NovaBase/include/{base,tpl}`。仓库根目录的 `.clangd` 镜像了这些路径 — 新增 ext include 目录时两边都要加，否则即使编译通过，clangd 也会一片红。

## Python 侧

`python/trade_prob/` 是研究 / FP 生成 / 参数调优流水线，给 C++ 策略喂数据；C++ 构建不会 import 它。整体流程（源自 `python/trade_prob/process.md`）：

1. 在 Windows 上下载交易所原始数据（`prod_download.py`），再用 `csv_gz_to_parquet.py` 转换到 `/data/<exch>/{book_snapshot_N,trades,incremental}/` 的本地布局。
2. `FairPriceGenerator`（命令行：`python -m trade_prob.FairPriceGenerator --from_date=... --to_date=... --type={usdt|usdc|forex|digital|merge|volume|eval}`）按天生成 FP parquet，落到 `python/trade_prob/output/fps_stable/`。`digital` 通常要加 `--no_multiprocess`。
3. `optimize_fp.py` / `batch_optimize_fp.sh` 按 symbol 跑 Optuna 搜参；每个 symbol 的结果落到 `python/tune_runs/<PAIR>/results.jsonl` + `optuna_study_<PAIR>`。
4. `eval_fp.py` / `eval_fp_baseline.py` 产出日度 / 滚动汇总，供 `log_analysis.ipynb` 消费。
5. `update_pair_configs_from_stats.py` 把 `config/pair_stats/<inst>/rolling7days.csv` 转成 C++ 侧实际读取的 `config/pair_configs.yml`。

`python/adjust_balance_allocation.py` 读写 `config/BalanceAllocation.yml`，策略 init 时会把它当作各币种起始余额加载。

## 值得一提的约定

- 日志和录制的 CSV/parquet 都在 gitignore（`*.csv`、`*.parquet`、`*.log`、`python/trade_prob/output/`）。`build/` 和 `lib/` 下的构建产物也被忽略，但已签入的 `lib/libTakingDemoD.so` 例外。
- 要加一个走 JSON config 的策略参数，改 `include/configs/strategy_config.h` 里的 `STRATEGY_CONFIG_FIELDS` — 不要手写 loader。
- `taking_all_multi<N>.json` 的命名模式是 `backtest.sh` 选 config 的依据；新增 config 放在 `config/` 下，`N` 用位置参数传。
