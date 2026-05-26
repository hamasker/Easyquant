# FP 当前实现状态

## 一、整体调用链

```
data feed (WS/mmap) → on_datainfo → fetch_data (写入 InstData_)
                                      ↓
                              accumulate_turnover (全市场成交额累加)
                                      ↓
                        scheduler.acc_usd_fp >= fp_turnover_usd? (5000 USD 默认)
                                      ↓ 是 + 距上次 > 500μs
on_poll / on_reminder(10ms) → do_calculations → process_fp
                                      ↓
                          DataProcess::fetch_data_all (拉取所有 instrument 最新行情)
                                      ↓
                          fpg_->update(ts)
                            ├── calculate_fp_usdt()      # 核心: 三路径 equilibrium
                            ├── calculate_fp_usdc()      # 五路径 median + 锚定
                            ├── calculate_fp_usd()       # 硬编码 [1.0, 1.0]
                            ├── calculate_fp_forex(c)    # 三路径 median (每个 forex)
                            ├── calculate_fp_digital(c)  # digital_fp 引擎 (每个币)
                            └── update_fp_insts()        # 写入所有 IC->fp_bid/fp_ask
```

## 二、各层实现细节

### 2.1 USD

```cpp
void calculate_fp_usd() {
  fps_map_[USD].fps.add({1.0, 1.0});
  fps_map_[USD].vps.add(1.0);
}
```

USD 为定价锚，固定 1.0。所有其他货币的 FP 最终都折算为 USD。

### 2.2 USDT — 核心定价基准

**三条路径**:
1. **Direct**: USDT/USD Kraken order book (直接盘口)
2. **EUR path**: USDT/EUR × EUR/USD (合成)
3. **USDC path**: USDC/USD ÷ USDC/USDT (合成)

**处理流程**:
1. 提取 5 个 depth pair (5 个 instrument 的 order book)
2. 每条路径独立计算 mid-price (BBO level)
3. **一致性检测**: 三路径 mid 取 median, 偏离 > 2bp 的路径被丢弃
   - 3 条都通过 → 全部参与 equilibrium
   - 1 条异常 → 丢弃, 2 条参与
   - 2+ 异常 → `AIM_EXCH_INVALID`
4. 通过检测的路径用 `append_path_levels` 构造 Level (price, qty)
   - Direct: 用 depth 前 10 档
   - EUR/UDSC 路径: 双腿合成, depth_T=10, depth_E/C=5
5. `solve_equilibrium_price`: 在合成后的 bids/asks 上找 `|B(P)-A(P)|` 最小的价格
   - 有重叠区间 → 扫描候选价格, 选供需量差最小者
   - 无重叠 → fallback 用 mid price + half spread
6. 写入 `fps_map_[USDT]`

**关键设计决策**:
- USDT 不做预测, 只做定价 (准确 > 速度)
- 三路径一致性检测用 2bp 阈值, median 抗单路径偏离
- 价格重叠时 equilibrium 选供需最均衡的价格, 无重叠时退化为 mid price

### 2.3 USDC

**五条信号源**:
1. AIM USDC/USD (主交易所直接路径, depth wp)
2. AIM USDC/EUR × EUR/USD (通过 EUR 合成)
3. Binance USDC/USDT × USDT/USD (通过 USDT 合成)
4. Coinbase USDT/USD 取倒数 × USDT/USD (通过 USDT 合成)
5. AIM raw BBO (不做 wp 的原始 BBO)

**处理流程**:
1. 检查 AIM 数据有效性 (3 个 depth pair, 5s stale 阈值)
2. 检查外部数据有效性 (Binance + Coinbase bbo/depth)
3. 根据外部数据是否有效, 选择 5 条或 3 条 (仅 AIM) 信号
4. 分别计算 bid 和 ask 的 median
5. **USDC 锚定检测**: 偏离 1.0 超过 2bp → warning, 超过 10bp → hard cap

### 2.4 Forex (EUR/GBP/AUD/CHF/CAD/PAXG)

**三条路径 (均在 AIM exchange)**:
1. C/USD: direct (weighted_price, 用 BBO price 做基准价)
2. USDT/C: `1 / wp(USDT/C) × fp_usdt` (通过 USDT)
3. USDC/C: `1 / wp(USDC/C) × fp_usdc` (通过 USDC)

**当前实现** (`calculate_fp_forex`):
```
for each relative_trading_id:
  wps_aim_Cusd  = calculate_weighted_price(depth_aim_Cusd, ..., ptq=true)
  wps_aim_usdtC = calculate_weighted_price(depth_aim_usdtC, ..., fp_usdt)
  wps_aim_usdcC = calculate_weighted_price(depth_aim_usdcC, ..., fp_usdc)

  bids = [wps_aim_Cusd.bid,  1/wps_aim_usdtC.ask × fp_usdt.bid,  1/wps_aim_usdcC.ask × fp_usdc.bid]
  asks = [wps_aim_Cusd.ask,  1/wps_aim_usdtC.bid × fp_usdt.ask,  1/wps_aim_usdcC.bid × fp_usdc.ask]

  fp_bid = median(bids)
  fp_ask = median(asks)
```

**关键问题**:
1. 三条路径全在同一交易所 (Kraken), 没有跨所验证
2. 直接用 median, 没有一致性检测 (与 USDT 不同)
3. 没有溢价模型 (代码中有 `// todo: 增加溢价计算模块`)
4. 没有预测性 — 纯定价, 不判断方向
5. `ptq=true` 意味着 C/USD 路径用 BBO 价格作为 quantity 权重转换 USD, 这是把 order book 价格当汇率用的 hack
6. `fp_forex_IB` (Interactive Brokers) stubbed, 没有接入

### 2.5 Digital (BTC/ETH/SOL/XRP)

**信号源**: 每个币 3 个外部交易所 + 1 个 AIM
- Binance: BBO + depth wp
- OKX: BBO + depth wp
- Coinbase: BBO + depth wp
- Kraken (AIM): depth wp

每对 (ex, src) 产生 2 个 Quote2 (depth + bbo), 共 7 个信号源 (aim 1 + 3×2)

**digital_fp 引擎**: 用 Bayesian 方法融合多信号源, 输出 [fp_bid, fp_ask], 包含:
- TFI (Trade Flow Imbalance) 修正
- FP momentum (用前一拍的 fp_mid 变化做平移)

### 2.6 update_fp_insts

将 FP 写入所有 trading instrument 的 `IC->fp_bid/fp_ask`:
- 同 base 不同 quote 的合成 inst: 用 base/USD 和 quote/USD 的 FP 交叉计算
- 计算每个 inst 的 `s_bps` (FP 偏离 OB 的 bps 值) 存入 `vol_map`
- 同一 base 的所有 pair 统一预期成交方向 (避免 BTC/USD 看涨但 BTC/EUR 看跌的矛盾)

## 三、FP 触发机制

**成交额驱动** (非定时):
```
每个 trade → accumulate_turnover(dollar_amount)
  acc_usd_fp += price × qty (仅 USD/USDT/USDC quote 的交易)
  
当 acc_usd_fp >= fp_turnover_usd (默认 5000 USD):
  AND ts - last_fp > 500μs (CPU 保护):
    → process_fp()
    → acc_usd_fp 归零
```

**为什么成交额驱动**: 市场越活跃 → FP 更新越频繁 (自适应节奏)。成交量 5000 USD → 中位 ~50ms/次, 冷市 ~1s/次。

**动态 Top Pairs**: 启动时从 Binance/OKX/Gate.io 拉取 24h 成交量 top 20 的 USDT 交易对, 自动订阅 trade 频道用于 turnover 累加。

## 四、关键数据结构

### fps_map_ (输出)
```cpp
unordered_map<currency, fair_price_data>
  fair_price_data {
    CircularBuffer<10000,1> timestamps;  // 最近 10000 个 FP 时间戳
    CircularBuffer<10000,2> fps;         // [bid, ask] × 10000
    CircularBuffer<10000,1> vps;         // mid price × 10000
    DigitalFpState digital_state;        // digital 引擎状态 (仅 BTC/ETH 等)
  }
```

### InstData_ (输入/中间)
```cpp
InstrumentData {
  InstrumentManager IM;                   // instrument 注册表
  unordered_map<UniInstID, depths_data> depth_map;    // 所有 depth
  unordered_map<UniInstID, bbo_data>    bbo_map;      // 非 AIM 的 BBO
  unordered_map<UniInstID, vol_data>    vol_map;      // 波动率/偏差
  unordered_map<UniInstID, trades_data> trade_map;    // 成交数据(TFI)
}
```

## 五、目前已知问题

| 问题 | 影响 | 优先级 |
|------|------|:-----:|
| Forex FP 无一致性检测 | 单路径异常直接污染 median | 高 |
| Forex FP 无跨所验证 | 所有路径同所(Kraken), 所级故障无法发现 | 高 |
| Forex FP 无溢价模块 | 外汇远期溢价(forward points)未建模 | 中 |
| IB 数据 stubbed | 缺少银行间市场定价参考 | 低 |
| Forex FP `ptq=true` hack | C/USD path 用 BBO 价格做 USD 转换, 精度受限 | 中 |
| Digital FP 依赖 forex FP 质量 | forex FP 误差传导至 digital 合成 pair | 中 |
