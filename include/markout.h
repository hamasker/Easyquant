#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

struct MarkoutTable {
  // dist bin edges, size = nbin+1
  std::vector<double> edges;
  int nbin = 0;

  // tau list in ns, size = ntau
  std::vector<int64_t> tau_ns;
  int ntau = 0;

  // drift arrays, size = 2 * ntau * nbin
  std::vector<double> drift_micro;
  std::vector<double> drift_mid;
  std::vector<double> drift_fp;

  bool load_edges_csv(const std::string &path);
  bool load_markout_csv(const std::string &path);

  bool ready() const {
    return nbin > 0 && ntau > 0 && (int)edges.size() == nbin + 1 &&
           (int)tau_ns.size() == ntau &&
           (int)drift_micro.size() == 2 * ntau * nbin &&
           (int)drift_mid.size() == 2 * ntau * nbin &&
           (int)drift_fp.size() == 2 * ntau * nbin;
  }

  // side: 0=BID, 1=ASK
  static inline double dist_from_fp(int side, double price, double fp_bid,
                                    double fp_ask) {
    return (side == 1) ? std::log(price / fp_ask) : std::log(fp_bid / price);
  }

  // 你当前用的 edge0（建议按 side 保证“赚钱为正”）
  // ASK: log(P/fp_ask) ; BID: log(fp_bid/P)
  static inline double edge0_log(int side, double price, double fp_bid,
                                 double fp_ask) {
    return (side == 1) ? std::log(price / fp_ask) : std::log(fp_bid / price);
  }

  int dist_bin(double dist) const;
  int tau_index_nearest(double tau_sec) const;

  double drift_cost_micro(int side, double dist, int tau_idx) const;
  double drift_cost_mid(int side, double dist, int tau_idx) const;
  double drift_cost_fp(int side, double dist, int tau_idx) const;

  // 便捷接口：输入 price/fp/tau_sec，内部算 dist + tau_idx 再查表
  double calculate_drift_cost_micro(int side, double price, double fp_bid,
                                    double fp_ask, double tau_sec) const;
  double calculate_drift_cost_mid(int side, double price, double fp_bid,
                                  double fp_ask, double tau_sec) const;
  double calculate_drift_cost_fp(int side, double price, double fp_bid,
                                 double fp_ask, double tau_sec) const;
};
