#pragma once
// OB 档位 drift 表 — 离线生成, 在线查表
// 格式: drift_<symbol>.csv  (python/drift_tables/)
// 列: symbol, maker_side, level, tau_sec, avg_dist_bps, cap_mid_bps,
//     drift_mid_bps, pnl_mid_bps, fill_count, total_notional, n_days,
//     fills_per_day

#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

struct DriftEntry {
  double cap_bps;
  double drift_bps;
  double pnl_bps;
  double fills_per_day;
};

class DriftTable {
public:
  // tau_sec 固定列表 (与 Python 端一致)
  static constexpr double taus[] = {0.1, 0.5, 1.0, 5.0, 10.0, 30.0, 60.0, 300.0};
  static constexpr int n_taus = 8;

  bool load(const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open())
      return false;

    std::string line;
    // 跳表头
    std::getline(f, line);

    while (std::getline(f, line)) {
      std::stringstream ss(line);
      std::string cell;
      std::vector<std::string> cols;
      while (std::getline(ss, cell, ','))
        cols.push_back(cell);
      if (cols.size() < 10)
        continue;

      // 列: 0=symbol, 1=maker_side, 2=level, 3=tau_sec, 4=avg_dist_bps,
      //     5=cap_mid_bps, 6=drift_mid_bps, 7=pnl_mid_bps, 8=fill_count, ...
      //     11=fills_per_day
      int side = (cols[1] == "ASK") ? 1 : 0;
      int level = std::stoi(cols[2]);
      double tau = std::stod(cols[3]);
      double cap = std::stod(cols[5]);
      double drift = std::stod(cols[6]);
      double pnl = std::stod(cols[7]);
      double fills = (cols.size() > 11) ? std::stod(cols[11]) : 0.0;

      int tau_idx = nearest_tau_idx(tau);
      uint32_t key = make_key(side, level, tau_idx);
      table_[key] = {cap, drift, pnl, fills};
    }
    return !table_.empty();
  }

  // 查表: 返回指定 (side, level, tau_sec) 的 entry
  const DriftEntry *lookup(int side, int level, double tau_sec) const {
    int ti = nearest_tau_idx(tau_sec);
    auto it = table_.find(make_key(side, level, ti));
    return (it != table_.end()) ? &it->second : nullptr;
  }

  // 查表: 用最近的 level (浮点距离 → 最近的整数 level)
  const DriftEntry *lookup_nearest(int side, double dist_bps,
                                   double tau_sec) const {
    // dist_bps 是在 OB 中距离 mid 的 bps, 近似对应 level
    int level = std::clamp(static_cast<int>(dist_bps / 0.2), 0, 24);
    return lookup(side, level, tau_sec);
  }

  bool empty() const { return table_.empty(); }

private:
  std::unordered_map<uint32_t, DriftEntry> table_;

  static uint32_t make_key(int side, int level, int tau_idx) {
    return (static_cast<uint32_t>(side) << 20) |
           (static_cast<uint32_t>(level) << 8) |
           static_cast<uint32_t>(tau_idx);
  }

  static int nearest_tau_idx(double tau_sec) {
    int best = 0;
    double best_diff = std::abs(tau_sec - taus[0]);
    for (int i = 1; i < n_taus; ++i) {
      double d = std::abs(tau_sec - taus[i]);
      if (d < best_diff) {
        best_diff = d;
        best = i;
      }
    }
    return best;
  }
};
