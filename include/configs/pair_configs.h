#pragma once

#include "util_tools/file_util.h"
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>

// 单个 pair 的配置（可扩展，后续可加更多 key）
struct PairConfig {
  // log_fp_only 回测汇总分位数（0~100），s_bps 与 depth5 的 bid/ask
  double s_bps_summary_percentile_bid = 30.0;
  double s_bps_summary_percentile_ask = 30.0;
  // s_bps 下单闸门阈值（bps），从 pair_configs.yml 每日重载
  double thr_bid_bps = 0.0;
  double thr_ask_bps = 0.0;
  double s_abs_min_bps = 0.0;
  double s_abs_max_bps = 30.0;
  double depth5_summary_percentile_bid = 30.0;
  double depth5_summary_percentile_ask = 30.0;
  double depth5_thr_bid = 0.0;
  double depth5_thr_ask = 0.0;
  double ema_alpha = 1.0;
  double wp_invalid_ticks = 0;
  // FP 动量缩放，0=关闭；>0 时用 fp_mid 的 bar 间变化平移 bid/ask
  double fp_momentum_scale = 0.0;
  // TFI 多尺度系数，长度与 Strategy.tfi_windows_s 对齐
  std::vector<double> fp_tfi_scales = {};
};

// 所有 pair 的配置集合
struct PairConfigs {
  // pair 名称 -> 该 pair 的配置
  std::unordered_map<std::string, PairConfig> pair_config_map;
  // 未配置 pair 时使用的默认配置
  PairConfig default_config;
};

// 获取指定 pair 的配置（未配置时返回 default_config）
inline const PairConfig &GetPairConfig(const PairConfigs &pc,
                                       const std::string &inst_str) {
  auto it = pc.pair_config_map.find(inst_str);
  if (it != pc.pair_config_map.end())
    return it->second;
  return pc.default_config;
}

// 从 YAML 文件加载 pair 配置
// 文件格式示例：
//   default:
//     ema_alpha: 0.3
//   btc_usdt_spot.krk:
//     ema_alpha: 0.2
//   eur_usd_spot.krk:
//     ema_alpha: 0.5
//     其他key: 值  # 后续扩展
inline void LoadPairConfigs(const std::string &path, PairConfigs &pc) {
  if (!file_util::FileExists(path))
    return;
  ryml::Tree tree;
  auto root = file_util::LoadYaml(path, tree);
  if (!root.is_map())
    return;
  for (const auto &pair_node : root.children()) {
    if (!pair_node.has_key())
      continue;
    std::string pair_key(pair_node.key().str, pair_node.key().len);
    PairConfig cfg;
    if (pair_node.is_map()) {
      for (const auto &kv : pair_node.children()) {
        if (!kv.has_key())
          continue;
        std::string k(kv.key().str, kv.key().len);
        if (kv.has_val()) {
          std::string v_str(kv.val().str, kv.val().len);
          double v = std::strtod(v_str.c_str(), nullptr);
          if (k == "s_bps_summary_percentile_bid")
            cfg.s_bps_summary_percentile_bid = v;
          else if (k == "s_bps_summary_percentile_ask")
            cfg.s_bps_summary_percentile_ask = v;
          else if (k == "s_abs_min_bps")
            cfg.s_abs_min_bps = v;
          else if (k == "s_abs_max_bps")
            cfg.s_abs_max_bps = v;
          else if (k == "depth5_summary_percentile_bid")
            cfg.depth5_summary_percentile_bid = v;
          else if (k == "depth5_summary_percentile_ask")
            cfg.depth5_summary_percentile_ask = v;
          else if (k == "ema_alpha")
            cfg.ema_alpha = v;
          else if (k == "wp_invalid_ticks")
            cfg.wp_invalid_ticks = v;
          else if (k == "fp_momentum_scale")
            cfg.fp_momentum_scale = v;
          else if (k == "fp_tfi_scales") {
            std::vector<double> arr;
            std::stringstream ss(v_str);
            std::string token;
            while (std::getline(ss, token, ',')) {
              if (!token.empty()) {
                arr.push_back(std::strtod(token.c_str(), nullptr));
              }
            }
            cfg.fp_tfi_scales = arr;
          }
          // 后续扩展: else if (k == "xxx") cfg.xxx = v;
        }
      }
    }
    if (pair_key == "default") {
      pc.default_config = cfg;
    } else {
      pc.pair_config_map[pair_key] = cfg;
    }
  }
}

// 从 rolling7days.csv 中加载某个 inst 在指定 date 的阈值，覆盖 cfg
// 里的阈值字段。 rolling7days.csv 由 python 脚本生成，列至少包含： date,
// thr_bid_bps, thr_ask_bps, depth5_thr_bid, depth5_thr_ask
inline bool
LoadPairThresholdsFromRolling7Days(const std::string &rolling_csv_path,
                                   const std::string &yyyymmdd,
                                   PairConfig &cfg) {
  if (!file_util::FileExists(rolling_csv_path))
    return false;
  try {
    // rapidcsv 在 file_util.h 中已 include（common/rapidcsv.h）
    rapidcsv::Document doc(rolling_csv_path, rapidcsv::LabelParams(0, -1));
    const auto dates = doc.GetColumn<std::string>("date");
    auto it = std::find(dates.begin(), dates.end(), yyyymmdd);
    if (it == dates.end())
      return false;
    const size_t row = static_cast<size_t>(std::distance(dates.begin(), it));
    cfg.thr_bid_bps = doc.GetCell<double>("thr_bid_bps", row);
    cfg.thr_ask_bps = doc.GetCell<double>("thr_ask_bps", row);
    cfg.depth5_thr_bid = doc.GetCell<double>("depth5_thr_bid", row);
    cfg.depth5_thr_ask = doc.GetCell<double>("depth5_thr_ask", row);
    return true;
  } catch (...) {
    return false;
  }
}

// 加载 pair_configs.yml，并用 rolling7days.csv 覆盖各 inst 的阈值字段
// - 阈值字段：thr_bid_bps/thr_ask_bps/depth5_thr_bid/depth5_thr_ask
// - 其余字段仍来自 pair_configs.yml
inline void LoadPairConfigs(const std::string &pair_configs_yml_path,
                            const std::string &pair_stats_root,
                            const std::string &yyyymmdd, PairConfigs &pc) {
  LoadPairConfigs(pair_configs_yml_path, pc);
  if (pair_stats_root.empty() || yyyymmdd.empty())
    return;

  for (auto &kv : pc.pair_config_map) {
    const std::string &inst = kv.first;
    auto &cfg = kv.second;
    const std::string rolling_path =
        file_util::JoinPath(pair_stats_root, inst, "rolling7days.csv");
    LoadPairThresholdsFromRolling7Days(rolling_path, yyyymmdd, cfg);
  }
}
