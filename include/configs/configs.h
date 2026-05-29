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
};

inline void LoadConfigs(const nova::base::Config *cfg, Configs &c) {
  LoadServerConfig(cfg, c.Server);
  LoadBaseInfoConfig(cfg, c.BaseInfo);
  LoadTradeConfig(cfg, c.Trade);
  LoadQuoteConfig(cfg, c.Quote);
  LoadStrategyBlock(cfg, c.Strategy);
}