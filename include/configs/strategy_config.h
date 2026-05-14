#pragma once
#include "common/data.h"
#include "configs/config_struct.h"
#include "configs/pair_configs.h"
#include "data_currency.h"

// === 字段定义宏（只需维护这里） ===
#define STRATEGY_CONFIG_FIELDS(X)                                              \
  X(root_path, std::string, std::string(""))                                   \
  X(prob_params_dir, std::string, std::string(""))                             \
  X(balance_params_path, std::string, std::string(""))                         \
  X(config_path, std::string, std::string(""))                                 \
  X(pair_configs_path, std::string, std::string(""))                           \
  X(data_source, std::string, std::string("dump"))                             \
  X(volatility_method_str, std::string, std::string("fp_diff"))                \
  X(forex_method, std::string, std::string("old"))                             \
  X(digital_method, std::string, std::string("old"))                           \
  X(rebalance_taker_method, std::string, std::string("pnl"))                   \
  X(aim_exchange, std::string, std::string("krk"))                             \
  X(depth_check_skip_exch, std::string, std::string(""))                       \
  X(bbo_check_skip_exch, std::string, std::string(""))                         \
  X(tfi_windows_s, std::string, std::string("0.2,5,20,60,180,600"))           \
  X(verbose_order_currency, std::string, std::string(""))                      \
  X(verbose_margin, bool, false)                                               \
  X(verbose_fp_currency, std::string, std::string(""))                         \
  X(flag_prod, bool, false)                                                    \
  X(test_mode, bool, false)                                                    \
  X(backtest, bool, false)                                                     \
  X(customer_balance, bool, false)                                             \
  X(flag_batch, bool, false)                                                   \
  X(static_hedge, bool, false)                                                 \
  X(insert, bool, false)                                                       \
  X(log_fp_only, bool, false)                                                  \
  X(log_fp, bool, false)                                                       \
  X(flag_monitor, bool, false)                                                 \
  X(cancel_negative, bool, false)                                              \
  X(amend_negative, bool, false)                                               \
  X(flag_ib, bool, false)                                                      \
  X(ib_valid, bool, true)                                                      \
  X(flag_cuscore, bool, false)                                                 \
  X(flag_slope, bool, false)                                                   \
  X(rebalance, bool, true)                                                     \
  X(rebalance_taker, bool, false)                                              \
  X(flexible_adjust, bool, false)                                              \
  X(flexible_vol_thre, bool, false)                                            \
  X(adjust_only, bool, false)                                                  \
  X(adjust_inst_fp, bool, false)                                               \
  X(flag_abnormal, bool, false)                                                \
  X(flag_save, bool, false)                                                    \
  X(neg_cancel_failed_taker, bool, false)                                      \
  X(flag_data_debug, bool, false)                                              \
  X(verbose_order, bool, false)                                                \
  X(verbose_process_orders, bool, false)                                       \
  X(verbose_fp, bool, false)                                                   \
  X(verbose_cuscore, bool, false)                                              \
  X(verbose_ob, bool, false)                                                   \
  X(verbose_delay, bool, false)                                                \
  X(verbose_module, bool, false)                                               \
  X(verbose_tfi, bool, false)                                                  \
  X(limit_usd, double, 100.0)                                                  \
  X(reconnect_threshold, double, 30.0)                                         \
  X(fp_interval, double, 1.0)                                                  \
  X(fp_wp_ema_alpha, double, 0.3)                                              \
  X(vol_interval, double, 50.0)                                                \
  X(order_interval, double, 8.0)                                               \
  X(negative_interval, double, 5.0)                                            \
  X(negative_threshold, double, 2.5)                                           \
  X(margin_threshold, double, 1e-10)                                           \
  X(EFU_threshold, double, 0.01)                                               \
  X(cuscore_threshold_slow, double, 1.0)                                       \
  X(cuscore_threshold_fast, double, 1.0)                                       \
  X(cuscore_threshold_mid, double, 1.0)                                        \
  X(vol_ts_threshold, double, 5.0)                                             \
  X(vol_ts_threshold_base, double, 5.0)                                        \
  X(diff_threshold, double, 3.0)                                               \
  X(vol_multi_base, double, 1.0)                                               \
  X(vol_multi, double, 1.0)                                                    \
  X(adjust_usd, double, -1.0)                                                  \
  X(factor_min, double, 0.25)                                                  \
  X(factor_max, double, 2.0)                                                   \
  X(bn_taker_fee, double, 0.0)                                                 \
  X(ok_taker_fee, double, 0.0)                                                 \
  X(cb_taker_fee, double, 0.0)                                                 \
  X(krk_taker_fee, double, 0.0)                                                \
  X(gt_taker_fee, double, 0.0)                                                 \
  X(digital_position_thre, double, 0.5)                                        \
  X(digital_position_thre_tmp, double, 0.5)                                    \
  X(abnormal_thre, double, 3.0)                                                \
  X(cuscore_span_slow, int, 108000)                                            \
  X(cuscore_span_fast, int, 9000)                                              \
  X(cuscore_span_mid, int, 900)                                                \
  X(trading_currencies_str, std::vector<std::string>, {"btc"})                 \
  X(digital_currencies_str, std::vector<std::string>, {"btc"})                 \
  X(instruments, picojson::array, {})                                          \
  X(instruments_swap, picojson::array, {})                                     \
  X(cores, picojson::array, {})                                                \
  X(ts_ret, std::vector<int64_t>, {})

struct StrategyConfig {
#define DECL_FIELD(name, type, def_val) type name = def_val;
  STRATEGY_CONFIG_FIELDS(DECL_FIELD)
#undef DECL_FIELD
  ServerConfig Server;
  BaseInfoConfig BaseInfo;
  TradeConfig Trade;
  QuoteConfig Quote;
  std::vector<data::currency> trading_currencies{data::currency::BTC};
  std::vector<data::currency> digital_currencies{data::currency::BTC};
  bool flag_check_bbo = false;
  double multi_order_vol = 1;
  data::VolatilityMethod volatility_method =
      data::VolatilityMethod::METHOD_UNKNOWN;
  std::unordered_map<data::currency, data::Setting> setting;
  PairConfigs pair_configs;
  data::CuscoreParams cuscore_params;
  std::vector<std::string> available_forex;
  std::unordered_map<data::currency, std::vector<double>> rets;
  int idx_ret = 0; // 当前ret数据索引
};

void LoadServerConfig(const nova::base::Config *cfg, ServerConfig &sc);
void LoadBaseInfoConfig(const nova::base::Config *cfg, BaseInfoConfig &bic);
void LoadTradeConfig(const nova::base::Config *cfg, TradeConfig &tc);
void LoadQuoteConfig(const nova::base::Config *cfg, QuoteConfig &qc);

inline void LoadStrategyConfig(const nova::base::Config *cfg,
                               StrategyConfig &sc) {
  std::set<std::string> read_keys; // 新增：记录读取到的key
#define LOAD_FIELD(name, type, def_val)                                        \
  {                                                                            \
    type tmp_val;                                                              \
    if (cfg->GetItemValue("Strategy." #name, tmp_val)) {                       \
      read_keys.insert(#name);                                                 \
    }                                                                          \
    GetItemValueWithDefault(cfg, "Strategy." #name, sc.name, def_val);         \
  }
  STRATEGY_CONFIG_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD
  // 检查所有字段是否都被读取到
#define CHECK_FIELD(name, type, def_val)                                       \
  if (read_keys.find(#name) == read_keys.end()) {                              \
    throw std::runtime_error(std::string("Config field not found: ") + #name); \
  }
  STRATEGY_CONFIG_FIELDS(CHECK_FIELD)
#undef CHECK_FIELD
  INFO_FLOG("[StrategyConfig] 所有配置字段均已成功读取！");

  // 设置配置文件路径
  std::string relative_path = cfg->config_file();
  if (!relative_path.empty()) {
    // 提取JSON文件名
    size_t last_slash = relative_path.find_last_of('/');
    size_t last_backslash = relative_path.find_last_of('\\');
    size_t last_sep = (last_slash != std::string::npos)
                          ? (last_backslash != std::string::npos
                                 ? std::max(last_slash, last_backslash)
                                 : last_slash)
                          : last_backslash;

    if (last_sep != std::string::npos) {
      std::string filename = relative_path.substr(last_sep + 1);
      sc.config_path = sum_util::JoinPath(sc.root_path, "config", filename);
      INFO_FLOG("[StrategyConfig] 配置文件路径: {}", sc.config_path);
    } else {
      // 如果没有路径分隔符，直接使用文件名
      sc.config_path =
          sum_util::JoinPath(sc.root_path, "config", relative_path);
      INFO_FLOG("[StrategyConfig] 配置文件路径: {}", sc.config_path);
    }
  }

  LoadServerConfig(cfg, sc.Server);
  LoadBaseInfoConfig(cfg, sc.BaseInfo);
  LoadTradeConfig(cfg, sc.Trade);
  LoadQuoteConfig(cfg, sc.Quote);
  // 概率表参数目录
  sc.prob_params_dir =
      sum_util::JoinPath(sc.root_path, "config/prob_params/kraken/");
  if (sum_util::IsDirectoryEmpty(sc.prob_params_dir))
    throw std::runtime_error("Invalid prob_params directory!");
  // 仓位参数文件路径
  sc.balance_params_path =
      sum_util::JoinPath(sc.root_path, "config/Setting.yml");
  if (!sum_util::FileExists(sc.balance_params_path))
    throw std::runtime_error("Invalid Setting file path!");
  // 仓位计算参数
  ryml::Tree setting_tree;
  auto setting_root = sum_util::LoadYaml(sc.balance_params_path, setting_tree);
  INFO_FLOG("[OnInit]Got Setting config from: {}", sc.balance_params_path);
  sc.setting = data::LoadSettingMap(setting_root, sc.trading_currencies);
  if (sc.test_mode) {
    sum_util::PrintYamlNode(setting_root);
  }
  // 交易货币
  sc.trading_currencies = data::str_to_currency(sc.trading_currencies_str);
  if (sc.trading_currencies.empty())
    throw std::runtime_error("Invalid trading_currencies!");
  // 加密货币
  sc.digital_currencies = data::str_to_currency(sc.digital_currencies_str);
  if (sc.digital_currencies.empty())
    throw std::runtime_error("Invalid digital_currencies!");
  // 回测数据源
  if (sc.data_source == "tardis")
    sc.flag_check_bbo = false;
  sc.multi_order_vol = sc.order_interval * 1000 / sc.vol_interval;
  // 波动率方法
  sc.volatility_method = data::get_vol_method(sc.volatility_method_str);
  if (sc.volatility_method == data::VolatilityMethod::METHOD_UNKNOWN)
    throw std::runtime_error("Invalid volatility_method!");
  // cuscore参数
  auto &cp = sc.cuscore_params;
  cp.flag_cuscore = sc.flag_cuscore;
  if (cp.flag_cuscore) {
    cp.span_slow = sc.cuscore_span_slow;
    cp.span_fast = sc.cuscore_span_fast;
    cp.span_mid = sc.cuscore_span_mid;
    cp.threshold_slow = sc.cuscore_threshold_slow;
    cp.threshold_fast = sc.cuscore_threshold_fast;
    cp.threshold_mid = sc.cuscore_threshold_mid;
    if (cp.span_slow == 0 || cp.span_fast == 0 || cp.span_mid == 0 ||
        cp.threshold_slow > 100 || cp.threshold_fast > 100 ||
        cp.threshold_mid > 100)
      throw std::runtime_error("Invalid cuscore parameters!");
    cp.coefs_t_slow = sum_util::ExpDecayCoefficients(cp.span_slow);
    cp.coefs_t_fast = sum_util::ExpDecayCoefficients(cp.span_fast);
    cp.coefs_t_mid = sum_util::ExpDecayCoefficients(cp.span_mid);
  }
  sc.digital_position_thre_tmp = sc.digital_position_thre;
  // pair 单独配置（ema_alpha 等）
  if (!sc.pair_configs_path.empty()) {
    const std::string abs_path =
        sum_util::JoinPath(sc.root_path, sc.pair_configs_path);
    // 这里只加载 pair_configs.yml（分位数/ema_alpha 等）。
    // 阈值 thr_* / depth5_thr_* 会在策略 on_init 得到“策略日期”后再从
    // config/pair_stats/<inst>/rolling7days.csv 覆盖。
    LoadPairConfigs(abs_path, sc.pair_configs);
  } else {
    sc.pair_configs.default_config.ema_alpha = sc.fp_wp_ema_alpha;
  }
}
