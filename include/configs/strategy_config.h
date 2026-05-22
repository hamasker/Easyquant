#pragma once

#include "configs/base_config.h"
#include "configs/pair_configs.h"
#include "configs/stable_config.h"

// =============== Strategy.Verbose.* ================================
// 新加字段: 在下面 X-macro 加一行 X(name, type, default)
#define VERBOSE_CONFIG_FIELDS(X)                                               \
  X(order, bool, false)                                                        \
  X(margin, bool, false)                                                       \
  X(fp, bool, false)                                                           \
  X(cuscore, bool, false)                                                      \
  X(ob, bool, false)                                                           \
  X(delay, bool, false)                                                        \
  X(process_orders, bool, false)                                               \
  X(module, bool, false)                                                       \
  X(tfi, bool, false)                                                          \
  X(order_currency, std::string, std::string(""))                              \
  X(fp_currency, std::string, std::string(""))

struct VerboseConfig {
#define DECL_FIELD(name, type, def_val) type name = def_val;
  VERBOSE_CONFIG_FIELDS(DECL_FIELD)
#undef DECL_FIELD
};

inline void LoadVerboseConfig(const nova::base::Config *cfg,
                              VerboseConfig &vc) {
#define LOAD_FIELD(name, type, def_val)                                        \
  cfg->GetItemValue("Strategy.Verbose." #name, vc.name);
  VERBOSE_CONFIG_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD
}

// =============== Strategy.Cuscore.* ================================

#define CUSCORE_CONFIG_FIELDS(X)                                               \
  X(enable, bool, false)                                                       \
  X(threshold_slow, double, 1.0)                                               \
  X(threshold_fast, double, 1.0)                                               \
  X(threshold_mid, double, 1.0)                                                \
  X(span_slow, int, 54000)                                                     \
  X(span_fast, int, 9000)                                                      \
  X(span_mid, int, 900)

struct CuscoreConfig {
#define DECL_FIELD(name, type, def_val) type name = def_val;
  CUSCORE_CONFIG_FIELDS(DECL_FIELD)
#undef DECL_FIELD
};

inline void LoadCuscoreConfig(const nova::base::Config *cfg,
                              CuscoreConfig &cc) {
#define LOAD_FIELD(name, type, def_val)                                        \
  cfg->GetItemValue("Strategy.Cuscore." #name, cc.name);
  CUSCORE_CONFIG_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD
}

// =============== Strategy.Volatility.* ================================

#define VOLATILITY_CONFIG_FIELDS(X)                                            \
  X(method_str, std::string, "fp_diff_shared")                                 \
  X(multi, double, 0.8)                                                        \
  X(multi_base, double, 0.0)                                                   \
  X(multi_shrink_ratio, double, 0.0)                                           \
  X(ts_threshold, double, -1.0)                                                \
  X(ts_threshold_base, double, 10000.0)                                        \
  X(flexible_threshold, bool, false)

struct VolatilityConfig {
#define DECL_FIELD(name, type, def_val) type name = def_val;
  VOLATILITY_CONFIG_FIELDS(DECL_FIELD)
#undef DECL_FIELD
};

inline void LoadVolatilityConfig(const nova::base::Config *cfg,
                                 VolatilityConfig &vc) {
#define LOAD_FIELD(name, type, def_val)                                        \
  cfg->GetItemValue("Strategy.Volatility." #name, vc.name);
  VOLATILITY_CONFIG_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD
}

// =============== Strategy.Taker.* ================================
#define TAKER_CONFIG_FIELDS(X)                                                 \
  X(enable, bool, false)                                                       \
  X(verbose_mode, bool, false)                                                 \
  X(deviation_trigger_pct, double, 50.0)                                       \
  X(deviation_target_pct, double, 30.0)                                        \
  X(min_action_qty_pct, double, 5.0)                                           \
  X(topbook_take_ratio, double, 0.3)                                           \
  X(slip_vol_k, double, 2.0)                                                   \
  X(slip_min_bps, double, 5.0)                                                 \
  X(same_side_cooldown_ms, int, 1000)                                          \
  X(pending_timeout_ms, int, 2000)                                             \
  X(profit_take_bps, double, 0.0)

struct TakerConfig {
#define DECL_FIELD(name, type, def_val) type name = def_val;
  TAKER_CONFIG_FIELDS(DECL_FIELD)
#undef DECL_FIELD
};

inline void LoadTakerConfig(const nova::base::Config *cfg, TakerConfig &tc) {
#define LOAD_FIELD(name, type, def_val)                                        \
  cfg->GetItemValue("Strategy.Taker." #name, tc.name);
  TAKER_CONFIG_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD
}

// =============== Strategy.Order.* ================================
#define ORDER_CONFIG_FIELDS(X)                                                 \
  X(holding_time_s, double, 60.0)                                              \
  X(threshold_margin, double, 0.0)

struct OrderConfig {
#define DECL_FIELD(name, type, def_val) type name = def_val;
  ORDER_CONFIG_FIELDS(DECL_FIELD)
#undef DECL_FIELD
};

inline void LoadOrderConfig(const nova::base::Config *cfg, OrderConfig &oc) {
#define LOAD_FIELD(name, type, def_val)                                        \
  cfg->GetItemValue("Strategy.Order." #name, oc.name);
  ORDER_CONFIG_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD
}

// =============== Strategy.* - 顶层旋钮 ================================
#define STRATEGY_CONFIG_FIELDS(X)                                              \
  X(flag_ib, bool, false)                                                      \
  X(flag_prod, bool, false)                                                    \
  X(backtest, bool, false)                                                     \
  X(customer_balance, bool, false)                                             \
  X(recover_usd, double, -1.0)                                                 \
  X(digital_position_thre, double, 0.0)                                        \
  X(flexible_adjust, bool, false)

struct StrategyBlock {
  StableConfig Stable;
  OrderConfig Order;
  TakerConfig Taker;
  VolatilityConfig Volatility;
  CuscoreConfig Cuscore;
  VerboseConfig Verbose;

  // 顶层旋钮 (X-marco, 平铺在 Strategy.* 下)
#define DECL_FIELD(name, type, def_val) type name = def_val;
  STRATEGY_CONFIG_FIELDS(DECL_FIELD)
#undef DECL_FIELD

  // 派生 / 运行时 (不来自 JSON)
  std::unordered_map<data::currency, data::Setting> setting;
  PairConfigs pair_configs;
  data::CuscoreParams cuscore_params;
  std::vector<std::string> available_forex;
  std::unordered_map<data::currency, std::vector<double>> rets;
  std::vector<double> ts_ret; // 时序收益率缓存
};

inline void LoadStrategyBlock(const nova::base::Config *cfg,
                              StrategyBlock &sb) {
  // ******** 顶层旋钮 (X-macro) ********
  std::set<std::string> read_keys;
#define LOAD_FIELD(name, type, def_val)                                        \
  {                                                                            \
    type tmp_val;                                                              \
    if (cfg->GetItemValue("Strategy." #name, tmp_val)) {                       \
      read_keys.insert(#name);                                                 \
    }                                                                          \
    GetItemValueWithDefault(cfg, "Strategy." #name, sb.name, def_val);         \
  }
  STRATEGY_CONFIG_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD
  // 检查所有字段是否都被读取到 (缺失则用默认值并警告)
#define CHECK_FIELD(name, type, def_val)                                       \
  if (read_keys.find(#name) == read_keys.end()) {                              \
    WARNING_FLOG("[StrategyConfig] Config field not found: Strategy.{}, "      \
                 "using default",                                              \
                 #name);                                                       \
  }
  STRATEGY_CONFIG_FIELDS(CHECK_FIELD)
#undef CHECK_FIELD
  INFO_FLOG("[StrategyConfig] 所有配置字段均已成功读取！");

  // ******** 5 个子块 *********
  LoadStableConfig(cfg, sb.Stable);
  LoadOrderConfig(cfg, sb.Order);
  LoadVolatilityConfig(cfg, sb.Volatility);
  LoadCuscoreConfig(cfg, sb.Cuscore);
  LoadVerboseConfig(cfg, sb.Verbose);
  LoadTakerConfig(cfg, sb.Taker);

  LoadPairConfigs(sb.Stable.pair_configs_path, sb.pair_configs);

  // sb.multi_order_vol = sb.Stable.order_interval * 1000 /
  // sb.Stable.vol_interval;

  ryml::Tree setting_tree;
  auto setting_root =
      sum_util::LoadYaml(sb.Stable.balance_params_path, setting_tree);
  INFO_FLOG("[OnInit]Got Setting config from: {}",
            sb.Stable.balance_params_path);
  sb.setting = data::LoadSettingMap(setting_root, sb.Stable.trading_currencies);
  // if (sb.test_mode) {
  //   sum_util::PrintYamlNode(setting_root);
  // }
  // cuscore参数
  // auto &cp = sb.Cuscore.cuscore_params;
  // cp.flag_cuscore = sc.flag_cuscore;
  // if (cp.flag_cuscore) {
  //   cp.span_slow = sc.cuscore_span_slow;
  //   cp.span_fast = sc.cuscore_span_fast;
  //   cp.span_mid = sc.cuscore_span_mid;
  //   cp.threshold_slow = sc.cuscore_threshold_slow;
  //   cp.threshold_fast = sc.cuscore_threshold_fast;
  //   cp.threshold_mid = sc.cuscore_threshold_mid;
  //   if (cp.span_slow == 0 || cp.span_fast == 0 || cp.span_mid == 0 ||
  //       cp.threshold_slow > 100 || cp.threshold_fast > 100 ||
  //       cp.threshold_mid > 100)
  //     throw std::runtime_error("Invalid cuscore parameters!");
  //   cp.coefs_t_slow = sum_util::ExpDecayCoefficients(cp.span_slow);
  //   cp.coefs_t_fast = sum_util::ExpDecayCoefficients(cp.span_fast);
  //   cp.coefs_t_mid = sum_util::ExpDecayCoefficients(cp.span_mid);
  // }
}
