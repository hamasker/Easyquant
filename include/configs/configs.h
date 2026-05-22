#pragma once

// 顶层配置入口: 聚合 JSON 5 个一级 key (Server / BaseInfo / Trade / Quote /
// Strategy) 通过 LoadConfigs 一次性把整个 JSON 灌进 C++ 端
//
// 子模块定义在:
// - base_config.h     :   ServerConfig / BaseInfoConfig / TradeConfig /
// QuoteConfig
// - strategy_config.h :   StrategyBlock + Strategy 内部 5 个子块
// - stable_config.h   :   StableConfig (被 strategy_config.h 间接拉入)
// - pair_config.h     :   PairConfig (YAML 驱动, 运行时填充到 StrategyBlock)
//

#include "configs/base_config.h"
#include "configs/strategy_config.h"

struct Configs {
  ServerConfig Server;
  BaseInfoConfig BaseInfo;
  TradeConfig Trade;
  QuoteConfig Quote;
  StrategyBlock Strategy;

  // 直通字段 (fair_price_generator 直接访问 CFG_.xxx)
  std::string aim_exchange;
  std::string forex_method{"old"};
  std::string digital_method{"old"};
  bool verbose_fp = false;
  bool verbose_tfi = false;
  bool flag_cuscore = false;
  bool flag_slope = false;
  bool flag_ib = false;
  bool amend_negative = false;
  double multi_order_vol = 0;
  double fp_kappa_m = 0;
  double fp_k_mad = 0;
  double fp_smin_bps = 0;
  double fp_smax_bps = 0;
  double fp_gcap_bps = 0;
  double fp_tau_age_ms = 0;
  bool adjust_inst_fp = false;
  std::vector<data::currency> digital_currencies;
  std::vector<data::currency> trading_currencies;
  PairConfigs pair_configs;
};

inline void LoadConfigs(const nova::base::Config *cfg, Configs &c) {
  LoadServerConfig(cfg, c.Server);
  LoadBaseInfoConfig(cfg, c.BaseInfo);
  LoadTradeConfig(cfg, c.Trade);
  LoadQuoteConfig(cfg, c.Quote);
  LoadStrategyBlock(cfg, c.Strategy);
  // 直通字段 (从 Strategy 子块映射)
  c.aim_exchange = c.Strategy.Stable.aim_exchange;
  c.trading_currencies = c.Strategy.Stable.trading_currencies;
  c.pair_configs = c.Strategy.pair_configs;
  c.digital_currencies = c.Strategy.Stable.digital_currencies;
  cfg->GetItemValue("Strategy.forex_method", c.forex_method);
  cfg->GetItemValue("Strategy.digital_method", c.digital_method);
  cfg->GetItemValue("Strategy.fp_kappa_m", c.fp_kappa_m);
  cfg->GetItemValue("Strategy.fp_k_mad", c.fp_k_mad);
  cfg->GetItemValue("Strategy.fp_smin_bps", c.fp_smin_bps);
  cfg->GetItemValue("Strategy.fp_smax_bps", c.fp_smax_bps);
  cfg->GetItemValue("Strategy.fp_gcap_bps", c.fp_gcap_bps);
  cfg->GetItemValue("Strategy.fp_tau_age_ms", c.fp_tau_age_ms);
}