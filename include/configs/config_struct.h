#pragma once
#include "base/base_config.h"
#include <stdexcept>
#include <string>
#include <vector>

// ========== 工具模板 ===========
template <typename T>
void GetItemValueWithDefault(const nova::base::Config *cfg,
                             const std::string &key, T &target,
                             const T &default_val) {
  if (!cfg->GetItemValue(key, target)) {
    target = default_val;
  }
}

// Server.Log 配置
struct ServerLogConfig {
  std::string path;
  std::string file_level;
  std::string file_level_real;
  std::string screen_level;
  bool async_log = false;
  std::string fp_path;
  bool flag_save = false;
};

// Server 配置
struct ServerConfig {
  ServerLogConfig Log;
  int daemon = 0;
};

// BaseInfo 配置
struct BaseInfoConfig {
  std::string base_info_path;
};

// Trade.executor 配置
struct TradeExecutorConfig {
  std::string name;
  double priority_qty = 0.0;
  int delay_ms = 0;
  int check_position = 0;
  int depth_trade_factor = 0;
  std::string amend_mode;
};

// Trade.XXX_param 配置
struct TradeParamConfig {
  double priority_qty = 0.0;
  int delay_ms = 0;
};

// Trade 配置
struct TradeConfig {
  TradeExecutorConfig executor;
  TradeParamConfig krk_param;
  TradeParamConfig bn_param;
  TradeParamConfig ok_param;
  TradeParamConfig cb_param;
};

// Quote.backtest.input_dirs 配置
struct InputDirConfig {
  std::string path;
  std::string format;
  std::string data_type;
  bool daily = false;
  bool inst = false;
  bool gz = false;
};

// Quote.backtest 配置
struct QuoteBacktestConfig {
  std::string quote_reader;
  std::string begin_time;
  std::string end_time;
  bool just_load = false;
  bool ignore_check_quote_type = false;
  std::string output_dir;
  bool output_csv = false;
  std::vector<std::string> instruments;
  std::vector<InputDirConfig> input_dirs;
};

// Quote 配置
struct QuoteConfig {
  QuoteBacktestConfig backtest;
};

// ========== 加载函数实现 ==========
inline void LoadServerConfig(const nova::base::Config *cfg, ServerConfig &sc) {
  GetItemValueWithDefault(cfg, "Server.Log.path", sc.Log.path, std::string(""));
  GetItemValueWithDefault(cfg, "Server.Log.file_level", sc.Log.file_level,
                          std::string(""));
  GetItemValueWithDefault(cfg, "Server.Log.file_level_real",
                          sc.Log.file_level_real, std::string(""));
  GetItemValueWithDefault(cfg, "Server.Log.screen_level", sc.Log.screen_level,
                          std::string(""));
  GetItemValueWithDefault(cfg, "Server.Log.async_log", sc.Log.async_log, false);
  GetItemValueWithDefault(cfg, "Server.Log.fp_path", sc.Log.fp_path,
                          std::string(""));
  GetItemValueWithDefault(cfg, "Server.Log.flag_save", sc.Log.flag_save, false);
  GetItemValueWithDefault(cfg, "Server.daemon", sc.daemon, 0);
}

inline void LoadBaseInfoConfig(const nova::base::Config *cfg,
                               BaseInfoConfig &bic) {
  GetItemValueWithDefault(cfg, "BaseInfo.base_info_path", bic.base_info_path,
                          std::string(""));
}

inline void LoadTradeConfig(const nova::base::Config *cfg, TradeConfig &tc) {
  // executor
  GetItemValueWithDefault(cfg, "Trade.executor.name", tc.executor.name,
                          std::string(""));
  GetItemValueWithDefault(cfg, "Trade.executor.priority_qty",
                          tc.executor.priority_qty, 0.0);
  GetItemValueWithDefault(cfg, "Trade.executor.delay_ms", tc.executor.delay_ms,
                          0);
  GetItemValueWithDefault(cfg, "Trade.executor.check_position",
                          tc.executor.check_position, 0);
  GetItemValueWithDefault(cfg, "Trade.executor.depth_trade_factor",
                          tc.executor.depth_trade_factor, 0);
  GetItemValueWithDefault(cfg, "Trade.executor.amend_mode",
                          tc.executor.amend_mode, std::string(""));
  // krk_param
  GetItemValueWithDefault(cfg, "Trade.krk_param.priority_qty",
                          tc.krk_param.priority_qty, 0.0);
  GetItemValueWithDefault(cfg, "Trade.krk_param.delay_ms",
                          tc.krk_param.delay_ms, 0);
  // bn_param
  GetItemValueWithDefault(cfg, "Trade.bn_param.priority_qty",
                          tc.bn_param.priority_qty, 0.0);
  GetItemValueWithDefault(cfg, "Trade.bn_param.delay_ms", tc.bn_param.delay_ms,
                          0);
  // ok_param
  GetItemValueWithDefault(cfg, "Trade.ok_param.priority_qty",
                          tc.ok_param.priority_qty, 0.0);
  GetItemValueWithDefault(cfg, "Trade.ok_param.delay_ms", tc.ok_param.delay_ms,
                          0);
  // cb_param
  GetItemValueWithDefault(cfg, "Trade.cb_param.priority_qty",
                          tc.cb_param.priority_qty, 0.0);
  GetItemValueWithDefault(cfg, "Trade.cb_param.delay_ms", tc.cb_param.delay_ms,
                          0);
}

inline void LoadQuoteConfig(const nova::base::Config *cfg, QuoteConfig &qc) {
  // backtest
  GetItemValueWithDefault(cfg, "Quote.backtest.quote_reader",
                          qc.backtest.quote_reader, std::string(""));
  GetItemValueWithDefault(cfg, "Quote.backtest.begin_time",
                          qc.backtest.begin_time, std::string(""));
  GetItemValueWithDefault(cfg, "Quote.backtest.end_time", qc.backtest.end_time,
                          std::string(""));
  GetItemValueWithDefault(cfg, "Quote.backtest.just_load",
                          qc.backtest.just_load, false);
  GetItemValueWithDefault(cfg, "Quote.backtest.ignore_check_quote_type",
                          qc.backtest.ignore_check_quote_type, false);
  GetItemValueWithDefault(cfg, "Quote.backtest.output_dir",
                          qc.backtest.output_dir, std::string(""));
  GetItemValueWithDefault(cfg, "Quote.backtest.output_csv",
                          qc.backtest.output_csv, false);
  GetItemValueWithDefault(cfg, "Quote.backtest.instruments",
                          qc.backtest.instruments, std::vector<std::string>{});
}