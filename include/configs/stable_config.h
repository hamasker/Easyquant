#pragma once

#include "base/base_config.h"
#include "common/data.h"
#include "constant.h"
#include "util_tools/sum_util.h"

// === Strategy.Base.* - 几乎不变的策略配置 ===
// 加新字段: 在下面 X-macro 加一行 X(name, type, default)
// JSON 没写则保留 default

#define STABLE_CONFIG_FIELDS(X)                                                \
  X(aim_exchange, std::string, std::string("krk"))                             \
  X(customer_balance, bool, false)                                             \
  X(customer_usd, double, -1.0)                                                \
  X(limit_usd, double, 100.0)                                                  \
  X(negative_interval, double, 5.0)                                            \
  X(fp_turnover_usd, double, 2000.0)                                           \
  X(order_turnover_usd, double, 35000.0)                                       \
  X(fp_interval_min_ms, double, 5.0)                                           \
  X(fp_interval_max_ms, double, 200.0)                                         \
  X(order_interval_min_ms, double, 1000.0)                                     \
  X(order_interval_max_ms, double, 3500.0)                                     \
  X(print_interval, double, 1.0)                                               \
  X(vol_interval, double, 50.0)                                                \
  X(reconnect_threshold, double, 30.0)                                         \
  X(bn_taker_fee, double, 0.000129375)                                         \
  X(cb_taker_fee, double, 0.00015)                                             \
  X(ok_taker_fee, double, 0.000175)                                            \
  X(krk_taker_fee, double, 0.0005)                                             \
  X(gt_taker_fee, double, 0.0)                                                 \
  X(trading_currencies_str, std::vector<std::string>,                          \
    constant::trading_currencies_str)                                          \
  X(digital_currencies_str, std::vector<std::string>,                          \
    constant::digital_currencies_str)                                          \
  X(instruments, std::vector<std::string>, constant::instruments)              \
  X(instruments_swap, std::vector<std::string>, constant::instruments_swap)

struct StableConfig {
  std::string root_dir; // 必传: Strategy.Base.root_dir
  std::string config_dir;
  std::string config_file_path;
  std::string pair_configs_path;
  std::string prob_params_dir;
  std::string balance_params_path;
  std::vector<data::currency> trading_currencies;
  std::vector<data::currency> digital_currencies;
#define DECL_FIELD(name, type, def_val) type name = def_val;
  STABLE_CONFIG_FIELDS(DECL_FIELD)
#undef DECL_FIELD
};

inline void LoadStableConfig(const nova::base::Config *cfg, StableConfig &sc) {
  // ******** 路径相关 ********
  cfg->GetItemValue("Strategy.Base.root_dir", sc.root_dir);
  if (sc.root_dir.empty())
    throw std::runtime_error(
        "Strategy.Base.root_dir (or Strategy.root_path) is required!");
  sc.config_dir = file_util::JoinPath(sc.root_dir, "config");
  const std::string cfg_file = cfg->config_file();
  if (!cfg_file.empty()) {
    const std::string basename = file_util::GetFileName(cfg_file);
    sc.config_file_path = file_util::JoinPath(
        sc.config_dir, basename.empty() ? cfg_file : basename);
  }
  sc.pair_configs_path = file_util::JoinPath(sc.config_dir, "pair_configs.yml");
  sc.prob_params_dir =
      file_util::JoinPath(sc.config_dir, "prob_params", "kraken");
  sc.balance_params_path = file_util::JoinPath(sc.config_dir, "Setting.yml");
  if (sum_util::IsDirectoryEmpty(sc.prob_params_dir))
    throw std::runtime_error("Prob params dir is empty: " + sc.prob_params_dir);
  if (!sum_util::FileExists(sc.balance_params_path))
    throw std::runtime_error("Balance params not found: " +
                             sc.balance_params_path);

    // ******** JSON 可选字段 (X-macro 自动展开, 必须在使用这些字段之前)
    // ********
#define LOAD_FIELD(name, type, def_val)                                        \
  cfg->GetItemValue("Strategy.Base." #name, sc.name);
  STABLE_CONFIG_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD

  // ******** 填充 vector 字段 ********
  sc.trading_currencies = data::str_to_currency(sc.trading_currencies_str);
  if (sc.trading_currencies.empty())
    throw std::runtime_error("Invalid trading currencies");
  sc.digital_currencies = data::str_to_currency(sc.digital_currencies_str);
  if (sc.digital_currencies.empty())
    throw std::runtime_error("Invalid digital currencies");
}