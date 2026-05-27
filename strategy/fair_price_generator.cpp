#include "fair_price_generator.h"
#include "data.h"
#include "data_process.h"
#include "digital_fp_calculator.h"

using Dtype = data::depth_type;
using DataProcess::trunc_depth_data;
using DataProcess::weighted_price_bbo;
using DataProcess::weighted_price_depth;
constexpr int DepNum5 = 5;
constexpr int DepNum10 = 10;
constexpr int64_t NS_5S = 5LL * 1000000000LL; // 5 * 1e9 ns

// 定义无效数组常量
constexpr std::array<double, 2> invalid_array = {-1.0, -1.0};

namespace {

// 总是以 USDT 为 base，以 USD 为 quote
struct Level {
  double price; // USDT/USD
  double qty;   // USDT 数量
};

// 通用的路径处理函数，支持单腿和双腿路径
// 对于单腿路径：leg2 为 nullptr，depth2 为 0
// 对于双腿路径：使用 price_func 和 qty_func 来计算合成价格和数量
// 模板参数避免 std::function 堆分配和虚函数调用开销
template <typename PriceFn, typename QtyFn>
inline void
append_path_levels(const data::depths_data &leg1, std::size_t depth1,
                   const data::depths_data *leg2, std::size_t depth2,
                   std::vector<Level> &bids, std::vector<Level> &asks,
                   PriceFn price_func, QtyFn qty_func, bool leg1_bid_use_bids,
                   bool leg1_ask_use_bids, bool leg2_bid_use_bids,
                   bool leg2_ask_use_bids) {

  // bid 方向（市场买 USDT，我们卖 USDT）
  const auto &leg1_bid_side = leg1_bid_use_bids ? leg1.bids : leg1.asks;
  const auto &leg2_bid_side = (leg2 && leg2_bid_use_bids)
                                  ? leg2->bids
                                  : (leg2 ? leg2->asks : leg1.bids);

  for (std::size_t i = 0; i < depth1 && i < leg1_bid_side.size(); ++i) {
    double p1 = leg1_bid_side[i][0];
    double q1 = leg1_bid_side[i][1];
    if (p1 <= 0.0 || q1 <= 0.0 || !std::isfinite(p1) || !std::isfinite(q1))
      continue;

    if (leg2 == nullptr) {
      // 单腿路径：直接使用
      bids.push_back(Level{p1, q1});
    } else {
      // 双腿路径：遍历 leg2
      for (std::size_t j = 0; j < depth2 && j < leg2_bid_side.size(); ++j) {
        double p2 = leg2_bid_side[j][0];
        double q2 = leg2_bid_side[j][1];
        if (p2 <= 0.0 || q2 <= 0.0 || !std::isfinite(p2) || !std::isfinite(q2))
          continue;

        double price = price_func(p1, p2);
        double qty = qty_func(p1, q1, p2, q2);
        if (price > 0.0 && qty > 0.0 && std::isfinite(price) &&
            std::isfinite(qty)) {
          bids.push_back(Level{price, qty});
        }
      }
    }
  }

  // ask 方向（市场卖 USDT，我们买 USDT）
  const auto &leg1_ask_side = leg1_ask_use_bids ? leg1.bids : leg1.asks;
  const auto &leg2_ask_side = (leg2 && leg2_ask_use_bids)
                                  ? leg2->bids
                                  : (leg2 ? leg2->asks : leg1.asks);

  for (std::size_t i = 0; i < depth1 && i < leg1_ask_side.size(); ++i) {
    double p1 = leg1_ask_side[i][0];
    double q1 = leg1_ask_side[i][1];
    if (p1 <= 0.0 || q1 <= 0.0 || !std::isfinite(p1) || !std::isfinite(q1))
      continue;

    if (leg2 == nullptr) {
      // 单腿路径：直接使用
      asks.push_back(Level{p1, q1});
    } else {
      // 双腿路径：遍历 leg2
      for (std::size_t j = 0; j < depth2 && j < leg2_ask_side.size(); ++j) {
        double p2 = leg2_ask_side[j][0];
        double q2 = leg2_ask_side[j][1];
        if (p2 <= 0.0 || q2 <= 0.0 || !std::isfinite(p2) || !std::isfinite(q2))
          continue;

        double price = price_func(p1, p2);
        double qty = qty_func(p1, q1, p2, q2);
        if (price > 0.0 && qty > 0.0 && std::isfinite(price) &&
            std::isfinite(qty)) {
          asks.push_back(Level{price, qty});
        }
      }
    }
  }
}

// 便捷函数：直接路径
inline void append_direct_usdtusd_levels(const data::depths_data &d,
                                         std::size_t use_depth,
                                         std::vector<Level> &bids,
                                         std::vector<Level> &asks) {
  append_path_levels(
      d, use_depth, nullptr, 0, bids, asks,
      [](double p1, double) { return p1; }, // 价格直接使用
      [](double, double q1, double, double) { return q1; }, // 数量直接使用
      true, false, // bid 用 bids，ask 用 asks
      true, false);
}

// 便捷函数：EUR 路径
inline void append_eur_path_levels(const data::depths_data &usdt_eur,
                                   const data::depths_data &eur_usd,
                                   std::size_t depth_T, std::size_t depth_E,
                                   std::vector<Level> &bids,
                                   std::vector<Level> &asks) {
  append_path_levels(
      usdt_eur, depth_T, &eur_usd, depth_E, bids, asks,
      [](double p_TE, double p_EU) { return p_TE * p_EU; }, // 价格相乘
      [](double p_TE, double q_TE, double, double q_EU) {
        return std::min(q_TE, q_EU / p_TE); // 数量取 min
      },
      true, false,  // leg1 (usdt_eur): bid 用 bids，ask 用 asks
      true, false); // leg2 (eur_usd): bid 用 bids，ask 用 asks
}

// 便捷函数：USDC 路径
inline void append_usdc_path_levels(const data::depths_data &usdc_usdt,
                                    const data::depths_data &usdc_usd,
                                    std::size_t depth_CT, std::size_t depth_CU,
                                    std::vector<Level> &bids,
                                    std::vector<Level> &asks) {
  append_path_levels(
      usdc_usdt, depth_CT, &usdc_usd, depth_CU, bids, asks,
      [](double p_CT, double p_CU) { return p_CU / p_CT; }, // 价格相除
      [](double p_CT, double q_CT, double, double q_CU) {
        double max_q_c = std::min(q_CT, q_CU); // USDC 数量
        return max_q_c * p_CT;                 // 转成 USDT
      },
      false, true,  // leg1 (usdc_usdt): bid 用 asks，ask 用 bids
      true, false); // leg2 (usdc_usd): bid 用 bids，ask 用 asks
}

// 在合成好的 Levels 上，找"供需量差 |B(P)-A(P)| 最小"的价格，作为 equilibrium
// mid
inline bool solve_equilibrium_price(const std::vector<Level> &bids,
                                    const std::vector<Level> &asks,
                                    double &fp_buy, double &fp_sell) {
  if (bids.empty() || asks.empty()) {
    return false;
  }

  // 先拷贝一份用于排序
  std::vector<Level> bids_sorted = bids;
  std::vector<Level> asks_sorted = asks;

  std::sort(bids_sorted.begin(), bids_sorted.end(),
            [](const Level &a, const Level &b) { return a.price > b.price; });
  std::sort(asks_sorted.begin(), asks_sorted.end(),
            [](const Level &a, const Level &b) { return a.price < b.price; });

  // 检查排序后的数据有效性
  if (bids_sorted.empty() || asks_sorted.empty()) {
    return false;
  }

  // 检查价格有效性
  if (!std::isfinite(bids_sorted.front().price) ||
      !std::isfinite(asks_sorted.front().price) ||
      bids_sorted.front().price <= 0.0 || asks_sorted.front().price <= 0.0) {
    return false;
  }

  // 检查是否有价格重叠（最高bid应该 >= 最低ask才有意义）
  double max_bid = bids_sorted.front().price;
  double min_ask = asks_sorted.front().price;
  bool has_overlap = (max_bid >= min_ask);

  // 如果没有重叠，直接使用mid price作为fallback
  if (!has_overlap) {
    double mid_price = 0.5 * (max_bid + min_ask);
    if (!std::isfinite(mid_price) || mid_price <= 0.0) {
      return false;
    }
    // 使用mid price，spread为价差的一半
    double gap = min_ask - max_bid;
    constexpr double ALPHA = 0.5;
    double spread = ALPHA * gap;
    fp_buy = mid_price - 0.5 * spread;
    fp_sell = mid_price + 0.5 * spread;

    // 确保fp_buy不超过max_bid，fp_sell不低于min_ask
    if (fp_buy > max_bid)
      fp_buy = max_bid;
    if (fp_sell < min_ask)
      fp_sell = min_ask;

    if (fp_buy <= 0.0 || fp_sell <= 0.0 || !std::isfinite(fp_buy) ||
        !std::isfinite(fp_sell)) {
      return false;
    }
    if (fp_buy > fp_sell) {
      std::swap(fp_buy, fp_sell);
    }
    return true;
  }

  // 收集所有候选价格
  std::vector<double> cand;
  cand.reserve(bids_sorted.size() + asks_sorted.size());
  for (const auto &l : bids_sorted) {
    if (std::isfinite(l.price) && l.price > 0.0) {
      cand.push_back(l.price);
    }
  }
  for (const auto &l : asks_sorted) {
    if (std::isfinite(l.price) && l.price > 0.0) {
      cand.push_back(l.price);
    }
  }

  if (cand.empty()) {
    return false;
  }

  std::sort(cand.begin(), cand.end());
  cand.erase(std::unique(cand.begin(), cand.end()), cand.end());

  double best_p = std::numeric_limits<double>::quiet_NaN();
  double best_diff = std::numeric_limits<double>::infinity();

  // 暴力 O(N^2) 计算：N 一般几十~一百，完全可接受
  // 只考虑在 [min_ask, max_bid] 范围内的价格
  for (double p : cand) {
    if (!std::isfinite(p) || p <= 0.0) {
      continue;
    }

    // 只考虑在重叠区域内的价格
    if (p < min_ask || p > max_bid) {
      continue;
    }

    double B = 0.0;
    double A = 0.0;
    for (const auto &l : bids_sorted) {
      if (std::isfinite(l.price) && l.price >= p && std::isfinite(l.qty) &&
          l.qty > 0.0) {
        B += l.qty;
      }
    }
    for (const auto &l : asks_sorted) {
      if (std::isfinite(l.price) && l.price <= p && std::isfinite(l.qty) &&
          l.qty > 0.0) {
        A += l.qty;
      }
    }
    if (B <= 0.0 || A <= 0.0) {
      continue; // 任意一边没量就忽略
    }

    double diff = std::fabs(B - A);
    if (diff < best_diff) {
      best_diff = diff;
      best_p = p;
    }
  }

  if (!std::isfinite(best_p)) {
    // 如果找不到有效的equilibrium价格，使用简单的mid price作为fallback
    // 这种情况可能发生在所有价格都没有重叠的供需量，或者价差太小
    if (has_overlap) {
      // 有价格重叠，使用mid price作为fallback
      best_p = 0.5 * (max_bid + min_ask);
      if (!std::isfinite(best_p) || best_p <= 0.0) {
        return false;
      }
    } else {
      // 这种情况不应该发生，因为已经在前面处理了
      return false;
    }
  }

  // 以 best_p 为 mid，再用全局 best bid/ask 的 spread 来生成 bid/ask
  // bids_sorted 已按价格降序排序，front() 就是最高的 bid
  // asks_sorted 已按价格升序排序，front() 就是最低的 ask
  double best_bid = bids_sorted.front().price;
  double best_ask = asks_sorted.front().price;

  // 确保 best_bid 和 best_ask 有效
  if (!std::isfinite(best_bid) || !std::isfinite(best_ask) || best_bid <= 0.0 ||
      best_ask <= 0.0) {
    return false;
  }

  double raw_spread = best_ask - best_bid;
  if (raw_spread < 0.0) {
    raw_spread = 0.0;
  }

  // 这里可以调 α，比如 0.5 表示 spread 全加在 fp 上
  constexpr double ALPHA = 0.5;
  double spread = ALPHA * raw_spread;

  fp_buy = best_p - 0.5 * spread;
  fp_sell = best_p + 0.5 * spread;

  // 确保 fp_buy 和 fp_sell 有效且合理
  if (!std::isfinite(fp_buy) || !std::isfinite(fp_sell) || fp_buy <= 0.0 ||
      fp_sell <= 0.0) {
    return false;
  }

  if (fp_buy > fp_sell) {
    std::swap(fp_buy, fp_sell);
  }

  return true;
}

// FP 动量: 用上一拍的 fp_mid 变化对当前 bid/ask 做平移
inline void apply_fp_momentum(double &fp_bid, double &fp_ask, double prev_bid,
                              double prev_ask, double scale) {
  if (scale == 0.0 || !std::isfinite(prev_bid) || !std::isfinite(prev_ask) ||
      prev_bid <= 0.0 || prev_ask <= 0.0)
    return;
  double mid = std::sqrt(std::max(fp_bid * fp_ask, 1e-20));
  double prev_mid = std::sqrt(prev_bid * prev_ask);
  if (!std::isfinite(mid) || mid <= 0.0 || prev_mid < 1e-20)
    return;
  double mom = (mid - prev_mid) / std::max(prev_mid, 1e-20);
  double shift = mid * scale * mom;
  fp_bid += shift;
  fp_ask += shift;
}

inline bool has_tfi_enabled(const PairConfig &pc) {
  for (double v : pc.fp_tfi_scales) {
    if (v != 0.0)
      return true;
  }
  return false;
}

inline std::string vec_to_str(const std::vector<double> &v, int precision = 4) {
  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss << std::setprecision(precision) << "[";
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i > 0)
      oss << ",";
    oss << v[i];
  }
  oss << "]";
  return oss.str();
}

inline std::vector<double> get_tfi_from_map(
    const std::unordered_map<data::UniInstID, data::trades_data> &trade_map,
    data::UniInstID id, int64_t now_ts_ns) {
  constexpr int64_t kStaleNs = 5LL * 1000LL * 1000LL * 1000LL;
  auto it = trade_map.find(id);
  if (it == trade_map.end())
    return std::vector<double>(data::trades_data::window_steps().size(), 0.0);
  return it->second.get_tfi(now_ts_ns, kStaleNs);
}

inline double compute_tfi_bps_from_trades(
    const std::unordered_map<data::UniInstID, data::trades_data> &trade_map,
    data::UniInstID id1, data::UniInstID id2, data::UniInstID id3,
    data::UniInstID id4, const PairConfig &pc, int64_t now_ts_ns) {
  constexpr int64_t kStaleNs = 5LL * 1000LL * 1000LL * 1000LL;
  const std::size_t n_scales = data::trades_data::window_steps().size();
  std::vector<double> tfi_mean(n_scales, 0.0);
  int n_valid = 0;

  for (const auto id : {id1, id2, id3, id4}) {
    auto it = trade_map.find(id);
    if (it == trade_map.end())
      continue;
    const auto tfi = it->second.get_tfi(now_ts_ns, kStaleNs);
    bool has_non_zero = false;
    for (double v : tfi) {
      if (v != 0.0) {
        has_non_zero = true;
        break;
      }
    }
    if (!has_non_zero)
      continue;
    for (std::size_t i = 0; i < std::min(tfi.size(), tfi_mean.size()); ++i) {
      tfi_mean[i] += tfi[i];
    }
    n_valid++;
  }

  if (n_valid <= 0)
    return 0.0;
  for (std::size_t i = 0; i < tfi_mean.size(); ++i) {
    tfi_mean[i] /= static_cast<double>(n_valid);
  }

  double s_bps = 0.0;
  for (std::size_t i = 0;
       i < std::min(pc.fp_tfi_scales.size(), tfi_mean.size()); ++i) {
    s_bps += pc.fp_tfi_scales[i] * tfi_mean[i];
  }
  return std::clamp(s_bps, -200.0, 200.0);
}

inline void apply_tfi_scale(double &fp_bid, double &fp_ask, double s_bps) {
  if (!std::isfinite(s_bps) || !std::isfinite(fp_bid) || !std::isfinite(fp_ask))
    return;
  const double factor = 1.0 + s_bps * 1e-4;
  if (!std::isfinite(factor) || factor <= 0.0)
    return;
  fp_bid *= factor;
  fp_ask *= factor;
}

} // namespace

// 性能统计: USDT FP 计算方法耗时
namespace {
struct UsdtFpPerfStats {
  std::atomic<uint64_t> old_method_count{0};
  std::atomic<uint64_t> new_method_count{0};
  std::atomic<uint64_t> old_method_total_ns{0};
  std::atomic<uint64_t> new_method_total_ns{0};
  std::atomic<uint64_t> old_method_max_ns{0};
  std::atomic<uint64_t> new_method_max_ns{0};
  std::atomic<uint64_t> last_report_count{0};

  void report() {
    constexpr uint64_t REPORT_INTERVAL = 1000; // 每1000次报告一次
    uint64_t old_cnt = old_method_count.load();
    uint64_t new_cnt = new_method_count.load();
    uint64_t last_cnt = last_report_count.load();

    if (old_cnt + new_cnt >= last_cnt + REPORT_INTERVAL) {
      uint64_t old_total = old_method_total_ns.load();
      uint64_t new_total = new_method_total_ns.load();
      uint64_t old_max = old_method_max_ns.load();
      uint64_t new_max = new_method_max_ns.load();

      if (old_cnt > 0 && new_cnt > 0) {
        double old_avg_us = old_total / static_cast<double>(old_cnt) / 1000.0;
        double new_avg_us = new_total / static_cast<double>(new_cnt) / 1000.0;
        double old_max_us = old_max / 1000.0;
        double new_max_us = new_max / 1000.0;

        TRACE_FLOG("USDT FP Performance: old_method: count={}, avg={:.3f}us, "
                   "max={:.3f}us | "
                   "new_method: count={}, avg={:.3f}us, max={:.3f}us | "
                   "speedup={:.2f}x",
                   old_cnt, old_avg_us, old_max_us, new_cnt, new_avg_us,
                   new_max_us, old_avg_us > 0 ? old_avg_us / new_avg_us : 0.0);
      }

      last_report_count.store(old_cnt + new_cnt);
    }
  }
};

// static UsdtFpPerfStats usdt_fp_perf_stats;
} // namespace

bool FairPriceGenerator::update(const int64_t ts) {
  this->ts_tmp = ts;
  calculate_fp_usdt();
  calculate_fp_usdc();
  calculate_fp_usd();
  for (const auto &currency : CFG_.trading_currencies) {
    if (currency != data::currency::USD && currency != data::currency::USDT &&
        currency != data::currency::USDC) {
      const auto &currency_str = data::get_currency_name(currency);
      if (sum_util::Find(constant::available_forex, currency_str)) {
        if (CFG_.forex_method == "new")
          calculate_fp_forex(currency);
      } else if (sum_util::Find(constant::available_digital, currency_str)) {
        calculate_fp_digital(currency);
      }
    }
  }
  update_fp_insts();
  if (this->fp_update_times < 2999) {
    this->fp_update_times++;
    this->first_cal = false;
    if (CFG_.verbose_fp)
      DEBUG_FLOG("waiting for fp enough {}", this->fp_update_times);
    return false;
  } else
    return true;
}

// ─── 路径一致性检测 ───
static constexpr double kConsistencyBps = 2.0;
static int check_path_consistency(double d_mid, double e_mid, double u_mid) {
  double sorted[] = {d_mid, e_mid, u_mid};
  std::sort(sorted, sorted + 3);
  double med = sorted[1]; // median 抗单路径偏离
  int mask = 0;
  for (int i = 0; i < 3; ++i) {
    double mids[] = {d_mid, e_mid, u_mid};
    if (std::isfinite(mids[i]) && mids[i] > 0 &&
        std::abs(mids[i] - med) / med * 10000.0 <= kConsistencyBps)
      mask |= (1 << i);
  }
  return mask;
}

void FairPriceGenerator::calculate_fp_usdt() {
  // 1. 提取 5 个 depth pair
  const auto &id_d = id_map.at(
      DataProcess::format_main_exchange_usd_pair("usdt", CFG_.aim_exchange));
  const auto &id_te = id_map.at(DataProcess::format_main_exchange_cross_pair(
      "usdt", "eur", CFG_.aim_exchange));
  const auto &id_eu = id_map.at(
      DataProcess::format_main_exchange_usd_pair("eur", CFG_.aim_exchange));
  const auto &id_ct = id_map.at(DataProcess::format_main_exchange_cross_pair(
      "usdc", "usdt", CFG_.aim_exchange));
  const auto &id_cu = id_map.at(
      DataProcess::format_main_exchange_usd_pair("usdc", CFG_.aim_exchange));

  const auto &dd = depth_map.at(id_d);
  const auto &dte = depth_map.at(id_te);
  const auto &deu = depth_map.at(id_eu);
  const auto &dct = depth_map.at(id_ct);
  const auto &dcu = depth_map.at(id_cu);

  // 数据时效检查
  InstData_.abnormal_status = data::abnormal_status::NORMAL;
  auto stale = [&](const auto &d) {
    return !d.valid || ts_tmp - d.local_ts > NS_5S;
  };
  if (stale(dd) || stale(dte) || stale(deu) || stale(dct) || stale(dcu))
      [[unlikely]] {
    ERROR_FLOG("abnormal usdt fp: stale depth");
    InstData_.abnormal_status = data::abnormal_status::AIM_EXCH_INVALID;
    return;
  }

  // 2. 三路径独立 mid → 一致性检测
  auto mid = [](double b1, double a1, double b2, double a2, auto fn) {
    return (fn(b1, b2) + fn(a1, a2)) * 0.5;
  };
  double d_mid = mid(dd.bids[0][0], dd.asks[0][0], 0, 0,
                     [](double p, double) { return p; });
  double e_mid = mid(dte.bids[0][0], dte.asks[0][0], deu.bids[0][0],
                     deu.asks[0][0], [](double a, double b) { return a * b; });
  double u_mid = mid(dct.bids[0][0], dct.asks[0][0], dcu.bids[0][0],
                     dcu.asks[0][0], [](double a, double b) { return b / a; });

  int mask = check_path_consistency(d_mid, e_mid, u_mid);
  if (mask == 0) [[unlikely]] {
    ERROR_FLOG(
        "abnormal usdt fp: all 3 paths diverged d={:.6f} e={:.6f} u={:.6f}",
        d_mid, e_mid, u_mid);
    InstData_.abnormal_status = data::abnormal_status::AIM_EXCH_INVALID;
    return;
  }

  // 3. 只构造通过检测的路径 Levels
  std::vector<Level> bids, asks;
  bids.reserve(128);
  asks.reserve(128);
  if (mask & 1)
    append_direct_usdtusd_levels(dd, DepNum10, bids, asks);
  if (mask & 2)
    append_eur_path_levels(dte, deu, DepNum10, DepNum5, bids, asks);
  if (mask & 4)
    append_usdc_path_levels(dct, dcu, DepNum10, DepNum5, bids, asks);

  if (bids.empty() || asks.empty()) [[unlikely]] {
    ERROR_FLOG("abnormal usdt fp: empty levels mask={}", mask);
    InstData_.abnormal_status = data::abnormal_status::AIM_EXCH_INVALID;
    return;
  }

  // 4. solve equilibrium
  double fp_buy = NAN, fp_sell = NAN;
  if (!solve_equilibrium_price(bids, asks, fp_buy, fp_sell) ||
      !std::isfinite(fp_buy) || !std::isfinite(fp_sell)) [[unlikely]] {
    ERROR_FLOG("abnormal usdt fp: equilibrium failed mask={}", mask);
    InstData_.abnormal_status = data::abnormal_status::AIM_EXCH_INVALID;
    return;
  }

  // 5. 存储
  std::array<double, 2> fp = {fp_buy, fp_sell};
  fps_map_[data::currency::USDT].timestamps.add(ts_tmp);
  fps_map_[data::currency::USDT].fps.add(fp);
  fps_map_[data::currency::USDT].vps.add((fp_buy + fp_sell) * 0.5);
  InstData_.IM.FindByUniId(id_d)->fp_bid = fp[0];
  InstData_.IM.FindByUniId(id_d)->fp_ask = fp[1];

  if (CFG_.verbose_fp) [[unlikely]]
    DEBUG_FLOG("usdt fp: [{:.6f}, {:.6f}] mask={} d={:.6f} e={:.6f} u={:.6f}",
               fp_buy, fp_sell, mask, d_mid, e_mid, u_mid);
}

namespace {
// 辅助结构：从 bbo 或 depth 中提取的数据
struct BboDepthData {
  double ask0, bid0, askv0, bidv0;
  double tick_size, taker_fee;
  double ema_bidv, ema_askv;
};

// 从 bbo 或 depth 中提取数据的辅助函数
inline BboDepthData extract_bbo_or_depth_data(const data::bbo_data &bbo,
                                              const data::depths_data &depth,
                                              int64_t current_ts,
                                              int64_t invalid_thre) {
  BboDepthData result;
  if (bbo.valid && current_ts - bbo.local_ts < invalid_thre) {
    result.ask0 = bbo.ask[0];
    result.bid0 = bbo.bid[0];
    result.askv0 = bbo.ask[1];
    result.bidv0 = bbo.bid[1];
    result.tick_size = bbo.tick_size;
    result.taker_fee = bbo.taker_fee;
    result.ema_bidv = bbo.ema_bidv;
    result.ema_askv = bbo.ema_askv;
  } else {
    result.ask0 = depth.asks[0][0];
    result.bid0 = depth.bids[0][0];
    result.askv0 = depth.asks[0][1];
    result.bidv0 = depth.bids[0][1];
    result.tick_size = depth.tick_size;
    result.taker_fee = depth.taker_fee;
    result.ema_bidv = depth.ema_bidv;
    result.ema_askv = depth.ema_askv;
  }
  return result;
}

// 检查深度数据有效性的辅助函数
inline bool is_depth_valid(const data::depths_data &depth, int64_t current_ts,
                           int64_t invalid_thre) {
  return depth.valid && (current_ts - depth.local_ts <= invalid_thre);
}

// 检查外部数据有效性的辅助函数
inline bool is_external_data_valid(const data::bbo_data &bbo,
                                   const data::depths_data &depth,
                                   int64_t current_ts, int64_t invalid_thre) {
  return (bbo.valid || depth.valid) &&
         (current_ts - bbo.local_ts < invalid_thre ||
          current_ts - depth.local_ts < invalid_thre);
}

// 计算加权价格，如果交易对以 usdt 结尾则转换成 usd（使用 bbo 数据）
// 参数：
//   - bbo: bbo 数据
//   - inst_str: 交易对字符串，用于判断是否以 usdt 结尾
//   - usdt_usd_bid: USDT/USD 的 bid 价格（用于转换）
//   - usdt_usd_ask: USDT/USD 的 ask 价格（用于转换）
// 返回：转换后的加权价格 [bid, ask]（如果以 usdt 结尾，已转换为 usd）
inline std::array<double, 2>
calculate_weighted_price(data::bbo_data &bbo,
                         const std::array<double, 2> *fp_usdt = nullptr) {
  const double ema_alpha = bbo.PC.ema_alpha;
  const double ema_bidv = bbo.ema_bidv * 0.6;
  const double ema_askv = bbo.ema_askv * 0.7;
  // const double ema_bidv = bbo.PC.depth5_thr_bid;
  // const double ema_askv = bbo.PC.depth5_thr_ask;

  double bid = bbo.bid[0];
  double ask = bbo.ask[0];
  double bidv = bbo.bid[1] * bbo.bid[0];
  double askv = bbo.ask[1] * bbo.ask[0];

  if (fp_usdt != nullptr) {
    bidv *= (*fp_usdt)[1];
    askv *= (*fp_usdt)[0];
  }
  std::array<double, 2> wp = weighted_price_bbo(
      ask, askv, bid, bidv, bbo.tick_size, bbo.taker_fee, ema_bidv, ema_askv);
  wp[0] = (wp[0] > 0) ? wp[0] : bid - 30 * bbo.tick_size;
  wp[1] = (wp[1] > 0) ? wp[1] : ask + 30 * bbo.tick_size;
  const bool has_prev = bbo.wp_prev[0] > 0 && bbo.wp_prev[1] > 0;
  if (has_prev) {
    wp[0] = ema_alpha * wp[0] + (1.0 - ema_alpha) * bbo.wp_prev[0];
    wp[1] = ema_alpha * wp[1] + (1.0 - ema_alpha) * bbo.wp_prev[1];
  }
  bbo.wp_prev = wp;
  return wp;
}

// 计算加权价格（使用 BboDepthData，即已提取的 bbo/depth 一档数据）
inline std::array<double, 2>
calculate_weighted_price(const BboDepthData &bd, data::depths_data *depth_wp,
                         const std::array<double, 2> *fp_usdt = nullptr) {
  const double ema_alpha = depth_wp != nullptr ? depth_wp->PC.ema_alpha : 1.0;
  const double ema_bidv = bd.ema_bidv * 0.6;
  const double ema_askv = bd.ema_askv * 0.7;
  double bid = bd.bid0;
  double ask = bd.ask0;
  double bidv = bd.bidv0 * bd.bid0;
  double askv = bd.askv0 * bd.ask0;
  if (fp_usdt != nullptr) {
    bidv *= (*fp_usdt)[1];
    askv *= (*fp_usdt)[0];
  }
  std::array<double, 2> wp = weighted_price_bbo(
      ask, askv, bid, bidv, bd.tick_size, bd.taker_fee, ema_bidv, ema_askv);
  wp[0] = (wp[0] > 0) ? wp[0] : bid - 30 * bd.tick_size;
  wp[1] = (wp[1] > 0) ? wp[1] : ask + 30 * bd.tick_size;
  if (depth_wp != nullptr) {
    const bool has_prev = depth_wp->wp_prev[0] > 0 && depth_wp->wp_prev[1] > 0;
    if (has_prev) {
      wp[0] = ema_alpha * wp[0] + (1.0 - ema_alpha) * depth_wp->wp_prev[0];
      wp[1] = ema_alpha * wp[1] + (1.0 - ema_alpha) * depth_wp->wp_prev[1];
    }
    depth_wp->wp_prev = wp;
  }
  return wp;
}

// 计算加权价格，如果交易对以 usdt 结尾则转换成 usd（使用 depth 数据）
// 参数：
//   - depth: depth 数据
//   - inst_str: 交易对字符串，用于判断是否以 usdt 结尾
//   - usdt_usd_bid: USDT/USD 的 bid 价格（用于转换）
//   - usdt_usd_ask: USDT/USD 的 ask 价格（用于转换）
//   - this->/*volume_threshold*/ 0.0: 成交量阈值
//   - fp_usdt: USDT 的公平价格 [bid, ask]（用于 USDT 交易对的成交量计算，可选）
// 返回：转换后的加权价格 [bid, ask]（如果以 usdt 结尾，已转换为 usd）
inline std::array<double, 2>
calculate_weighted_price(data::depths_data &depth, const PairConfig &PC,
                         bool ptq = false,
                         const std::array<double, 2> *fp_usdt = nullptr) {
  // 使用的ema_volume是每秒调用update_ob_volume计算的(半衰期1H)
  const double &ema_alpha = PC.ema_alpha;
  const double &wp_invalid_ticks = PC.wp_invalid_ticks;
  // const double ema_bidv = depth.ema_bidv * 0.6;
  // const double ema_askv = depth.ema_askv * 0.7;
  const double &ema_bidv = PC.depth5_thr_bid;
  const double &ema_askv = PC.depth5_thr_ask;
  // 使用 depth 数据
  const auto &bids = trunc_depth_data<DepNum5>(depth, Dtype::BID);
  const auto &asks = trunc_depth_data<DepNum5>(depth, Dtype::ASK);
  const auto &bidsq = trunc_depth_data<DepNum5>(depth, Dtype::BID_QUANTITY);
  const auto &asksq = trunc_depth_data<DepNum5>(depth, Dtype::ASK_QUANTITY);

  double price_convert_bid = 1.0, price_convert_ask = 1.0;
  if (fp_usdt != nullptr) {
    price_convert_bid = (*fp_usdt)[0];
    price_convert_ask = (*fp_usdt)[1];
  }
  if (ptq) {
    price_convert_bid = bids[0];
    price_convert_ask = asks[0];
  }
  std::array<double, DepNum5> bidsv =
      sum_util::Multiply(bidsq, price_convert_bid);
  std::array<double, DepNum5> asksv =
      sum_util::Multiply(asksq, price_convert_ask);

  std::array<double, 2> wp = weighted_price_depth<DepNum5>(
      asks, asksv, bids, bidsv, depth.tick_size, ema_bidv, ema_askv, 1e-10,
      wp_invalid_ticks);
  wp[0] = (wp[0] > 0) ? wp[0] : bids[0] - 30 * depth.tick_size;
  wp[1] = (wp[1] > 0) ? wp[1] : asks[0] + 30 * depth.tick_size;
  // EMA 平滑
  const bool has_prev = depth.wp_prev[0] > 0 && depth.wp_prev[1] > 0;
  if (has_prev) {
    wp[0] = ema_alpha * wp[0] + (1.0 - ema_alpha) * depth.wp_prev[0];
    wp[1] = ema_alpha * wp[1] + (1.0 - ema_alpha) * depth.wp_prev[1];
  }
  depth.wp_prev = wp;
  return wp;
}
} // namespace

void FairPriceGenerator::calculate_fp_usdc() {
  const auto &fp_usdt = fps_map_[data::currency::USDT].fps.get_latest();
  const auto &id_bn_usdcusdt = id_map.at("usdc_usdt.bn");
  const auto &id_cb_usdtusd = id_map.at("usdt_usd.cb");
  const auto &id_aim_usdcusd = id_map.at(
      DataProcess::format_main_exchange_usd_pair("usdc", CFG_.aim_exchange));
  const auto &id_aim_usdceur =
      id_map.at(DataProcess::format_main_exchange_cross_pair(
          "usdc", "eur", CFG_.aim_exchange));
  const auto &id_aim_eurusd = id_map.at(
      DataProcess::format_main_exchange_usd_pair("eur", CFG_.aim_exchange));
  auto &bbo_bn_usdcusdt = bbo_map.at(id_bn_usdcusdt);
  auto &depth_bn_usdcusdt = depth_map.at(id_bn_usdcusdt);
  auto &bbo_cb_usdtusd = bbo_map.at(id_cb_usdtusd);
  auto &depth_cb_usdtusd = depth_map.at(id_cb_usdtusd);
  auto &depth_aim_usdcusd = depth_map.at(id_aim_usdcusd);
  auto &depth_aim_usdceur = depth_map.at(id_aim_usdceur);
  const auto &depth_aim_eurusd = depth_map.at(id_aim_eurusd);
  const auto &bd_aim_usdcusd = extract_bbo_or_depth_data(
      data::bbo_data(), depth_aim_usdcusd, this->ts_tmp, NS_5S);
  const auto &bd_aim_usdceur = extract_bbo_or_depth_data(
      data::bbo_data(), depth_aim_usdceur, this->ts_tmp, NS_5S);

  // 检查 AIM 数据有效性
  if (!is_depth_valid(depth_aim_usdcusd, this->ts_tmp, NS_5S) ||
      !is_depth_valid(depth_aim_usdceur, this->ts_tmp, NS_5S) ||
      !is_depth_valid(depth_aim_eurusd, this->ts_tmp, NS_5S)) {
    ERROR_FLOG(
        "abnormal usdc fp calculation: invalid aim data! depth_aim_usdcusd={}, "
        "depth_aim_usdceur={}, depth_aim_eurusd={}, this->ts_tmp={}, "
        "depth_aim_usdcusd_local_ts={}, depth_aim_usdceur_local_ts={}, "
        "depth_aim_eurusd_local_ts={}",
        depth_aim_usdcusd.valid, depth_aim_usdceur.valid,
        depth_aim_eurusd.valid, this->ts_tmp, depth_aim_usdcusd.local_ts,
        depth_aim_usdceur.local_ts, depth_aim_eurusd.local_ts);
    InstData_.abnormal_status = data::abnormal_status::AIM_EXCH_INVALID;
    return;
  }
  // 检查外部数据有效性并设置异常状态
  const bool external_bn_valid = is_external_data_valid(
      bbo_bn_usdcusdt, depth_bn_usdcusdt, this->ts_tmp, NS_5S);
  const bool external_cb_valid = is_external_data_valid(
      bbo_cb_usdtusd, depth_cb_usdtusd, this->ts_tmp, NS_5S);

  if (!external_bn_valid || !external_cb_valid) {
    WARNING_FLOG("abnormal usdc fp calculation: invalid external data! "
                 "bbo_bn_usdcusdt={}, bbo_cb_usdtusd={}, "
                 "depth_bn_usdcusdt={}, depth_cb_usdtusd={}, "
                 "this->ts_tmp={}, "
                 "bbo_bn_usdcusdt_local_ts={}, bbo_cb_usdtusd_local_ts={}, "
                 "depth_bn_usdcusdt_local_ts={}, depth_cb_usdtusd_local_ts={}",
                 bbo_bn_usdcusdt.valid, bbo_cb_usdtusd.valid,
                 depth_bn_usdcusdt.valid, depth_cb_usdtusd.valid, this->ts_tmp,
                 bbo_bn_usdcusdt.local_ts, bbo_cb_usdtusd.local_ts,
                 depth_bn_usdcusdt.local_ts, depth_cb_usdtusd.local_ts);
    InstData_.abnormal_status = data::abnormal_status::EXTERNAL_EXCH_INVALID;
  } else {
    InstData_.abnormal_status = data::abnormal_status::NORMAL;
  }

  // 获取 EUR 价格
  double eur_bid, eur_ask;
  if (this->first_cal || fps_map_[data::currency::EUR].fps.is_empty()) {
    eur_bid = depth_aim_eurusd.bids[0][0];
    eur_ask = depth_aim_eurusd.asks[0][0];
  } else {
    const auto &fp_eur = fps_map_[data::currency::EUR].fps.get_latest();
    eur_bid = fp_eur[0];
    eur_ask = fp_eur[1];
  }
  const auto &eur = std::array<double, 2>{eur_bid, eur_ask};
  // 计算 AIM 加权价格
  const auto &wps_aim_usdcusd =
      calculate_weighted_price(bd_aim_usdcusd, &depth_aim_usdcusd);
  const auto &wps_aim_usdceur =
      calculate_weighted_price(bd_aim_usdceur, &depth_aim_usdceur, &eur);
  double fp_bid, fp_ask;
  if (InstData_.abnormal_status == data::abnormal_status::NORMAL) {
    // 提取外部数据
    const auto bn_data = extract_bbo_or_depth_data(
        bbo_bn_usdcusdt, depth_bn_usdcusdt, this->ts_tmp, NS_5S);
    const auto cb_data = extract_bbo_or_depth_data(
        bbo_cb_usdtusd, depth_cb_usdtusd, this->ts_tmp, NS_5S);
    // 计算加权价格
    const auto &wps_bn_usdcusdt =
        calculate_weighted_price(bn_data, &depth_bn_usdcusdt, &fp_usdt);
    const auto &wps_cb_usdtusd_tmp =
        calculate_weighted_price(cb_data, &depth_cb_usdtusd);
    // 计算中位数价格
    std::array<double, 5> bids = {
        wps_aim_usdcusd[0], wps_bn_usdcusdt[0] * fp_usdt[0],
        1.0 / wps_cb_usdtusd_tmp[1] * fp_usdt[0], wps_aim_usdceur[0] * eur_bid,
        depth_aim_usdcusd.bids[0][0]};
    std::array<double, 5> asks = {
        wps_aim_usdcusd[1], wps_bn_usdcusdt[1] * fp_usdt[1],
        1.0 / wps_cb_usdtusd_tmp[0] * fp_usdt[1], wps_aim_usdceur[1] * eur_ask,
        depth_aim_usdcusd.asks[0][0]};
    fp_bid = sum_util::CalMedian(bids);
    fp_ask = sum_util::CalMedian(asks);
  } else {
    // 仅使用 AIM 数据
    std::array<double, 3> bids = {wps_aim_usdcusd[0],
                                  wps_aim_usdceur[0] * eur_bid,
                                  depth_aim_usdcusd.bids[0][0]};
    std::array<double, 3> asks = {wps_aim_usdcusd[1],
                                  wps_aim_usdceur[1] * eur_ask,
                                  depth_aim_usdcusd.asks[0][0]};
    fp_bid = sum_util::CalMedian(bids);
    fp_ask = sum_util::CalMedian(asks);
  }

  // 验证并设置最终价格
  std::array<double, 2> fp = {-1.0, -1.0};
  if (std::isfinite(fp_bid) && std::isfinite(fp_ask)) {
    fp[0] = std::min(fp_bid, fp_ask);
    fp[1] = std::max(fp_bid, fp_ask);
  } else [[unlikely]] {
    ERROR_FLOG("abnormal usdc fp calculation!external_valid: {}/{}, "
               "abnormal_status: {}, fp_bid: {}, fp_ask: {}",
               external_bn_valid, external_cb_valid, InstData_.abnormal_status,
               fp_bid, fp_ask);
    InstData_.abnormal_status = data::abnormal_status::AIM_EXCH_INVALID;
    return;
  }

  // USDC 锚定检测: 利用 Circle 1:1 赎回机制做安全校验
  static constexpr double kUsdcWarnBps = 2.0;
  static constexpr double kUsdcHardBps = 10.0;
  double mid = (fp[0] + fp[1]) * 0.5;
  double dev_bp = std::abs(mid - 1.0) * 10000.0;
  if (dev_bp > kUsdcHardBps) [[unlikely]] {
    ERROR_FLOG("usdc depeg: mid={:.6f} dev={:.1f}bp, capping to ±{}bp", mid,
               dev_bp, kUsdcHardBps);
    fp[0] = std::max(fp[0], 1.0 - kUsdcHardBps / 10000.0);
    fp[1] = std::min(fp[1], 1.0 + kUsdcHardBps / 10000.0);
  } else if (dev_bp > kUsdcWarnBps) {
    WARNING_FLOG("usdc deviation: mid={:.6f} dev={:.1f}bp", mid, dev_bp);
  }

  if (CFG_.verbose_fp) [[unlikely]]
    DEBUG_FLOG("[new]usdc fp_bid: {}, fp_ask: {}, ob_bid: {}, ob_ask: {}",
               fp_bid, fp_ask, depth_aim_usdcusd.bids[0][0],
               depth_aim_usdcusd.asks[0][0]);
  const double vp = (fp_bid + fp_ask) * 0.5;
  fps_map_[data::currency::USDC].timestamps.add(this->ts_tmp);
  fps_map_[data::currency::USDC].fps.add(fp);
  fps_map_[data::currency::USDC].vps.add(vp);
  auto *IC_usdcusd = InstData_.IM.FindByUniId(id_aim_usdcusd);
  IC_usdcusd->fp_bid = fp[0];
  IC_usdcusd->fp_ask = fp[1];
  const auto &id_aim_usdcusdt =
      id_map.at(DataProcess::format_main_exchange_cross_pair(
          "usdc", "usdt", CFG_.aim_exchange));
  auto *IC_usdcusdt = InstData_.IM.FindByUniId(id_aim_usdcusdt);
  IC_usdcusdt->fp_bid = fp[0];
  IC_usdcusdt->fp_ask = fp[1];
}

void FairPriceGenerator::calculate_fp_usd() {
  fps_map_[data::currency::USD].timestamps.add(this->ts_tmp);
  fps_map_[data::currency::USD].fps.add({1.0, 1.0});
  fps_map_[data::currency::USD].vps.add(1.0);
  return;
}

void FairPriceGenerator::calculate_fp_forex(const data::currency &currency) {
  // todo: 增加溢价计算模块, 改为适用其他forex的模式
  const auto &currency_str = data::get_currency_name(currency);
  const auto &fp_usdt = fps_map_[data::currency::USDT].fps.get_latest();
  const auto &fp_usdc = fps_map_[data::currency::USDC].fps.get_latest();
  const auto &id_aim_Cusd =
      id_map.at(DataProcess::format_main_exchange_usd_pair(currency_str,
                                                           CFG_.aim_exchange));
  const auto &id_aim_usdtC =
      id_map.at(DataProcess::format_main_exchange_cross_pair(
          "usdt", currency_str, CFG_.aim_exchange));
  const auto &id_aim_usdcC =
      id_map.at(DataProcess::format_main_exchange_cross_pair(
          "usdc", currency_str, CFG_.aim_exchange));
  auto &depth_aim_Cusd = depth_map.at(id_aim_Cusd);
  auto &depth_aim_usdtC = depth_map.at(id_aim_usdtC);
  auto &depth_aim_usdcC = depth_map.at(id_aim_usdcC);

  // const auto &C = std::array<double, 2>{depth_aim_Cusd.bids[0][0],
  //                                       depth_aim_Cusd.asks[0][0]};

  const auto &relative_trading_ids =
      DataProcess::get_relative_trading_ids(InstData_, currency);
  for (const auto &id : relative_trading_ids) {
    auto *IC = InstData_.IM.FindByUniId(id);
    if (sum_util::Find(CFG_.digital_currencies, IC->base) ||
        sum_util::Find(CFG_.digital_currencies, IC->quote))
      continue;
    auto &depth_cal = depth_map.at(id);
    const auto &wps_aim_Cusd =
        calculate_weighted_price(depth_aim_Cusd, depth_cal.PC, true);
    const auto &wps_aim_usdtC = calculate_weighted_price(
        depth_aim_usdtC, depth_cal.PC, false, &fp_usdt);
    const auto &wps_aim_usdcC = calculate_weighted_price(
        depth_aim_usdcC, depth_cal.PC, false, &fp_usdc);

    std::vector<double> bids(3, std::numeric_limits<double>::quiet_NaN());
    std::vector<double> asks(3, std::numeric_limits<double>::quiet_NaN());
    // 设置 bids 和 asks
    bids = {wps_aim_Cusd[0], 1.0 / wps_aim_usdtC[1] * fp_usdt[0],
            1.0 / wps_aim_usdcC[1] * fp_usdc[0]};
    asks = {wps_aim_Cusd[1], 1.0 / wps_aim_usdtC[0] * fp_usdt[1],
            1.0 / wps_aim_usdcC[0] * fp_usdc[1]};

    const double &fp_bid = sum_util::CalMedian(bids); // 改成计算array的median
    const double &fp_ask = sum_util::CalMedian(asks);
    std::array<double, 2> fp = {-1.0, -1.0}; // 初始化为 [-1.0, -1.0]
    // 检查 NaN 和 Inf
    if (std::isfinite(fp_bid) && std::isfinite(fp_ask)) {
      fp[0] = std::min(fp_bid, fp_ask);
      fp[1] = std::max(fp_bid, fp_ask);
    } else
      ERROR_FLOG("abnormal {} fp calculation!", currency_str);
    if (IC->inst_str == DataProcess::format_main_exchange_usd_pair(
                            currency_str, CFG_.aim_exchange)) {
      fps_map_[currency].timestamps.add(this->ts_tmp);
      if (CFG_.flag_ib) { // todo
        // const auto &id_ib =
        //     id_map.at(fmt::format("{}_usd_cash.idealpro", currency_str));
        // auto &ib_data = bbo_map.at(id_ib);
        // fp_forex_IB stubbed — fp stays unchanged
      }
      fps_map_[currency].fps.add(fp);
      fps_map_[currency].vps.add((fp[0] + fp[1]) * 0.5);
      IC->fp_bid = fp[0];
      IC->fp_ask = fp[1];
    } else {
      if (IC->base == currency) {
        IC->fp_bid = fp[0];
        IC->fp_ask = fp[1];
      } else {
        const auto &fp_base = fps_map_[IC->base].fps.get_latest();
        IC->fp_bid = fp_base[0] / fp[1];
        IC->fp_ask = fp_base[1] / fp[0];
      }
    }
    if (CFG_.verbose_fp) [[unlikely]]
      DEBUG_FLOG("[new]{} fp_bid: {}, fp_ask: {}, ob_bid: {}, ob_ask: {}",
                 IC->inst_str, fp[0], fp[1], depth_cal.bids[0][0],
                 depth_cal.asks[0][0]);
  }
}

inline digital_fp::Quote2 make_q2(const std::array<double, 2> &wp, uint64_t ts,
                                  const std::array<double, 2> &median = {
                                      -1.0, -1.0}) {
  digital_fp::Quote2 q;
  q.px = wp;
  q.local_ts = ts;
  bool basic_ok = (wp[0] > 0.0 && wp[1] > 0.0 && wp[0] <= wp[1]);

  // 如果提供了中位数，检查是否偏离中位数20%
  if (median[0] > 0.0 && median[1] > 0.0) {
    const double threshold = 0.2; // 20%偏离阈值
    bool bid_ok = std::abs(wp[0] - median[0]) / median[0] <= threshold;
    bool ask_ok = std::abs(wp[1] - median[1]) / median[1] <= threshold;
    q.ok = basic_ok && bid_ok && ask_ok;
  } else {
    q.ok = basic_ok;
  }
  return q;
}

void FairPriceGenerator::calculate_fp_digital(const data::currency &currency) {
  const auto &currency_str = data::get_currency_name(currency);
  const auto &fp_usdt = fps_map_[data::currency::USDT].fps.get_latest();
  const auto &inst_bn_Cusdt =
      DataProcess::format_main_exchange_cross_pair(currency_str, "usdt", "bn");
  const auto &inst_ok_Cusdt =
      DataProcess::format_main_exchange_cross_pair(currency_str, "usdt", "ok");
  const auto &inst_cb_Cusd =
      DataProcess::format_main_exchange_usd_pair(currency_str, "cb");
  const auto &inst_aim_Cusd = DataProcess::format_main_exchange_usd_pair(
      currency_str, CFG_.aim_exchange);

  const auto &id_bn_Cusdt = id_map.at(inst_bn_Cusdt);
  const auto &id_ok_Cusdt = id_map.at(inst_ok_Cusdt);
  const auto &id_cb_Cusd = id_map.at(inst_cb_Cusd);
  const auto &id_aim_Cusd = id_map.at(inst_aim_Cusd);

  auto &bbo_bn_Cusdt = bbo_map.at(id_bn_Cusdt);
  auto &bbo_ok_Cusdt = bbo_map.at(id_ok_Cusdt);
  auto &bbo_cb_Cusd = bbo_map.at(id_cb_Cusd);

  auto &depth_bn_Cusdt = depth_map.at(id_bn_Cusdt);
  auto &depth_ok_Cusdt = depth_map.at(id_ok_Cusdt);
  auto &depth_cb_Cusd = depth_map.at(id_cb_Cusd);
  auto &depth_aim_Cusd = depth_map.at(id_aim_Cusd);

  // 检查外部数据有效性
  const bool bn_valid =
      is_external_data_valid(bbo_bn_Cusdt, depth_bn_Cusdt, this->ts_tmp, NS_5S);
  const bool ok_valid =
      is_external_data_valid(bbo_ok_Cusdt, depth_ok_Cusdt, this->ts_tmp, NS_5S);
  const bool cb_valid =
      is_external_data_valid(bbo_cb_Cusd, depth_cb_Cusd, this->ts_tmp, NS_5S);

  // 根据有效性计算 wp，如果无效则返回 invalid_array
  const auto wp_bn_Cusdt_bbo_tmp =
      bn_valid ? calculate_weighted_price(bbo_bn_Cusdt, &fp_usdt) // todo
               : invalid_array;
  const std::array<double, 2> wp_bbo_bn = {wp_bn_Cusdt_bbo_tmp[0] * fp_usdt[0],
                                           wp_bn_Cusdt_bbo_tmp[1] * fp_usdt[1]};
  const auto wp_ok_Cusdt_bbo_tmp =
      ok_valid ? calculate_weighted_price(bbo_ok_Cusdt, &fp_usdt)
               : invalid_array;
  const std::array<double, 2> wp_bbo_ok = {wp_ok_Cusdt_bbo_tmp[0] * fp_usdt[0],
                                           wp_ok_Cusdt_bbo_tmp[1] * fp_usdt[1]};
  const auto wp_bbo_cb =
      cb_valid ? calculate_weighted_price(bbo_cb_Cusd) : invalid_array;

  const auto &relative_trading_ids =
      DataProcess::get_relative_trading_ids(InstData_, currency);
  for (const auto &id : relative_trading_ids) {
    auto *IC = InstData_.IM.FindByUniId(id);
    if (sum_util::Find(CFG_.digital_currencies, IC->base) ||
        sum_util::Find(CFG_.digital_currencies, IC->quote)) {
      auto &depth_cal = depth_map.at(id);
      const auto wp_bn_Cusdt_depth_tmp =
          bn_valid ? calculate_weighted_price(depth_bn_Cusdt, depth_cal.PC,
                                              true, &fp_usdt)
                   : invalid_array;
      const std::array<double, 2> wp_depth_bn = {
          wp_bn_Cusdt_depth_tmp[0] * fp_usdt[0],
          wp_bn_Cusdt_depth_tmp[1] * fp_usdt[1]};
      const auto wp_ok_Cusdt_depth_tmp =
          ok_valid ? calculate_weighted_price(depth_ok_Cusdt, depth_cal.PC,
                                              true, &fp_usdt)
                   : invalid_array;
      const std::array<double, 2> wp_depth_ok = {
          wp_ok_Cusdt_depth_tmp[0] * fp_usdt[0],
          wp_ok_Cusdt_depth_tmp[1] * fp_usdt[1]};

      const auto wp_depth_cb =
          cb_valid
              ? calculate_weighted_price(depth_cb_Cusd, depth_cal.PC, false)
              : invalid_array;

      const auto wp_depth_aim =
          calculate_weighted_price(depth_aim_Cusd, depth_cal.PC, false);

      // 收集所有有效的wp值用于计算中位数
      std::vector<double> bid_values, ask_values;
      auto add_wp_if_valid = [&](const std::array<double, 2> &wp) {
        if (wp[0] > 0.0 && wp[1] > 0.0) {
          bid_values.push_back(wp[0]);
          ask_values.push_back(wp[1]);
        }
      };
      add_wp_if_valid(wp_depth_aim);
      add_wp_if_valid(wp_depth_bn);
      add_wp_if_valid(wp_bbo_bn);
      add_wp_if_valid(wp_depth_ok);
      add_wp_if_valid(wp_bbo_ok);
      add_wp_if_valid(wp_depth_cb);
      add_wp_if_valid(wp_bbo_cb);

      // 计算中位数
      std::array<double, 2> median = {-1.0, -1.0};
      if (!bid_values.empty() && !ask_values.empty()) {
        std::sort(bid_values.begin(), bid_values.end());
        std::sort(ask_values.begin(), ask_values.end());
        size_t n_bid = bid_values.size();
        size_t n_ask = ask_values.size();
        median[0] =
            (n_bid % 2 == 0)
                ? (bid_values[n_bid / 2 - 1] + bid_values[n_bid / 2]) / 2.0
                : bid_values[n_bid / 2];
        median[1] =
            (n_ask % 2 == 0)
                ? (ask_values[n_ask / 2 - 1] + ask_values[n_ask / 2]) / 2.0
                : ask_values[n_ask / 2];
      }

      digital_fp::Quote2 aim =
          make_q2(wp_depth_aim, depth_aim_Cusd.local_ts, median);
      // 如果你愿意，也可以选更“新鲜”的aim
      // wp：depth无效就用bbo，或比较local_ts选最新

      std::array<digital_fp::Ex2, 3> leads;
      leads[0].depth = make_q2(wp_depth_bn, depth_bn_Cusdt.local_ts, median);
      leads[0].bbo = make_q2(wp_bbo_bn, bbo_bn_Cusdt.local_ts, median);

      leads[1].depth = make_q2(wp_depth_ok, depth_ok_Cusdt.local_ts, median);
      leads[1].bbo = make_q2(wp_bbo_ok, bbo_ok_Cusdt.local_ts, median);

      leads[2].depth = make_q2(wp_depth_cb, depth_cb_Cusd.local_ts, median);
      leads[2].bbo = make_q2(wp_bbo_cb, bbo_cb_Cusd.local_ts, median);

      digital_fp::Params pp; // ex_w 已在 .h 默认平分
      // pp.tau_age_ms = CFG_.fp_tau_age_ms;
      // pp.kappa_m = CFG_.fp_kappa_m;
      // pp.beta_s = 0.0; // 强烈建议先 0
      // pp.s_min_bps = CFG_.fp_smin_bps;
      // pp.s_max_bps = CFG_.fp_smax_bps;
      // pp.g_cap_bps = CFG_.fp_gcap_bps;
      // pp.k_mad = CFG_.fp_k_mad;
      // pp.src_u[1] 你可以按经验调高/调低 bbo 的贡献
      auto res = digital_fp::compute_fp_bidask_3ex2src(
          aim, leads, this->ts_tmp, pp, fps_map_.at(currency).digital_state,
          (currency == data::currency::BTC && CFG_.amend_negative) ? true
                                                                   : false);
      if (!res.ok)
        return;

      auto fp_tmp = res.fp;
      {
        if (has_tfi_enabled(depth_cal.PC)) {
          const double tfi_bps = compute_tfi_bps_from_trades(
              InstData_.trade_map, id_bn_Cusdt, id_ok_Cusdt, id_cb_Cusd,
              id_aim_Cusd, depth_cal.PC, this->ts_tmp);
          if (CFG_.verbose_tfi) {
            const std::string inst_str =
                DataProcess::format_main_exchange_usd_pair(currency_str,
                                                           CFG_.aim_exchange);
            const auto tfi_bn = get_tfi_from_map(InstData_.trade_map,
                                                 id_bn_Cusdt, this->ts_tmp);
            const auto tfi_ok = get_tfi_from_map(InstData_.trade_map,
                                                 id_ok_Cusdt, this->ts_tmp);
            const auto tfi_cb =
                get_tfi_from_map(InstData_.trade_map, id_cb_Cusd, this->ts_tmp);
            const auto tfi_aim = get_tfi_from_map(InstData_.trade_map,
                                                  id_aim_Cusd, this->ts_tmp);
            DEBUG_FLOG("[TFI][apply] inst={} k={} bn={} ok={} cb={} aim={} "
                       "s_bps={:.4f} "
                       "factor={:.8f}",
                       inst_str, vec_to_str(depth_cal.PC.fp_tfi_scales, 3),
                       vec_to_str(tfi_bn, 4), vec_to_str(tfi_ok, 4),
                       vec_to_str(tfi_cb, 4), vec_to_str(tfi_aim, 4), tfi_bps,
                       1.0 + tfi_bps * 1e-4);
          }
          apply_tfi_scale(fp_tmp[0], fp_tmp[1], tfi_bps);
        }
        const std::string inst_str = DataProcess::format_main_exchange_usd_pair(
            currency_str, CFG_.aim_exchange);
        const double scale = depth_cal.PC.fp_momentum_scale;
        if (scale != 0.0 && depth_cal.wp_prev[0] > 0.0 &&
            depth_cal.wp_prev[1] > 0.0) {
          const auto &prev = depth_cal.wp_prev;
          apply_fp_momentum(fp_tmp[0], fp_tmp[1], prev[0], prev[1], scale);
        }
      }
      if (IC->inst_str == DataProcess::format_main_exchange_usd_pair(
                              currency_str, CFG_.aim_exchange)) {
        fps_map_[currency].timestamps.add(this->ts_tmp);
        fps_map_[currency].fps.add(fp_tmp);
        double vp = (fp_tmp[0] + fp_tmp[1]) * 0.5;
        fps_map_[currency].vps.add(vp);
        IC->fp_bid = fp_tmp[0];
        IC->fp_ask = fp_tmp[1];
      } else {
        if (!fps_map_[IC->quote].fps.is_empty()) {
          const auto &fp_quote = fps_map_[IC->quote].fps.get_latest();
          IC->fp_bid = fp_tmp[0] / fp_quote[1];
          IC->fp_ask = fp_tmp[1] / fp_quote[0];
        } else {
          IC->fp_bid = fp_tmp[0];
          IC->fp_ask = fp_tmp[1];
        }
      }
      if (CFG_.verbose_fp)
        INFO_FLOG("[new]{} fp_bid: {}, fp_ask: {}, ob_bid: {}, ob_ask: {}, ",
                  currency_str, fp_tmp[0], fp_tmp[1], depth_aim_Cusd.bids[0][0],
                  depth_aim_Cusd.asks[0][0]);
    }
  }
}

// bool FairPriceGenerator::update_volatilities_fp(data::VolatilityMethod
// method) {
//   bool flag_ready = false;
//   for (auto &one : vol_map) {
//     const auto &id = one.first;
//     auto *IC = InstData_.IM.FindByUniId(id);
//     const auto &dd = depth_map.at(id);
//     auto &vd = one.second;
//     auto &sd = vd.slope;
//     auto &cc = vd.cuscore;
//     const auto &base = IC->base;
//     const auto &quote = IC->quote;
//     const auto &fp_bid = IC->fp_bid;
//     const auto &fp_ask = IC->fp_ask;
//     if (CFG_.flag_slope)
//       update_slope(sd, fp_bid, fp_ask, IC->inst_str);
//     if (method == data::VolatilityMethod::METHOD_FP_MAX_MIN ||
//         method == data::VolatilityMethod::METHOD_FP_MAX_MIN_SHARED) {
//       vd.ts = fps_map_.at(base).timestamps.get_last();
//       auto &base_fps = fps_map_.at(base).fps;
//       auto &quote_fps = fps_map_.at(quote).fps;
//       static constexpr size_t N = 3000;
//       auto fps_bid_base_iter = base_fps.get_column_accessor<0>(N);
//       auto fps_ask_base_iter = base_fps.get_column_accessor<1>(N);
//       auto fps_bid_quote_iter = quote_fps.get_column_accessor<0>(N);
//       auto fps_ask_quote_iter = quote_fps.get_column_accessor<1>(N);
//       double min_it_bid = std::numeric_limits<double>::max(),
//              max_it_bid = std::numeric_limits<double>::min(),
//              min_it_ask = std::numeric_limits<double>::max(),
//              max_it_ask = std::numeric_limits<double>::min(), fps_bid_cp0,
//              fps_ask_cp0;
//       for (size_t i = 0; i < N; ++i) {
//         auto bid_base = *fps_bid_base_iter, ask_quote = *fps_ask_quote_iter,
//              bid_quote = *fps_bid_quote_iter, ask_base = *fps_ask_base_iter;
//         auto fps_bid_cp = (ask_quote <= 1e-9) ? 0 : bid_base / ask_quote;
//         auto fps_ask_cp = (bid_quote <= 1e-9) ? 0 : bid_quote / ask_base;
//         if (i == 0) {
//           fps_bid_cp0 = fps_bid_cp;
//           fps_ask_cp0 = fps_ask_cp;
//         }
//         if (fps_bid_cp < min_it_bid)
//           min_it_bid = fps_bid_cp;
//         if (fps_bid_cp > max_it_bid)
//           max_it_bid = fps_bid_cp;
//         if (fps_ask_cp < min_it_ask)
//           min_it_ask = fps_ask_cp;
//         if (fps_ask_cp > max_it_ask)
//           max_it_ask = fps_ask_cp;
//         ++fps_bid_base_iter;
//         ++fps_ask_base_iter;
//         ++fps_bid_quote_iter;
//         ++fps_ask_quote_iter;
//       }
//       auto bid_amp = max_it_bid - min_it_bid;
//       auto ask_amp = max_it_ask - min_it_ask;
//       if (vd.vol_bid_fp_fast < 0) {
//         vd.vol_bid_fp_fast = bid_amp;
//         vd.vol_ask_fp_fast = ask_amp;
//         vd.vol_bid_fp_slow = bid_amp;
//         vd.vol_ask_fp_slow = ask_amp;
//       } else {
//         calculate_util::EMAStepInplace(vd.vol_bid_fp_fast, bid_amp,
//                                        ema_fast_alpha);
//         calculate_util::EMAStepInplace(vd.vol_ask_fp_fast, ask_amp,
//                                        ema_fast_alpha);
//         calculate_util::EMAStepInplace(vd.vol_bid_fp_slow, bid_amp,
//                                        ema_slow_alpha);
//         calculate_util::EMAStepInplace(vd.vol_ask_fp_slow, ask_amp,
//                                        ema_slow_alpha);
//       }
//       vd.vol_rate_bid_fp = vd.vol_bid_fp / fps_bid_cp0;
//       vd.vol_rate_ask_fp = vd.vol_ask_fp / fps_ask_cp0;
//       vd.vol_bid_fp = (method == data::VolatilityMethod::METHOD_FP_MAX_MIN)
//                           ? vd.vol_bid_fp_fast
//                           : std::max(vd.vol_bid_fp_fast, vd.vol_bid_fp_slow);
//       vd.vol_ask_fp = (method == data::VolatilityMethod::METHOD_FP_MAX_MIN)
//                           ? vd.vol_ask_fp_fast
//                           : std::max(vd.vol_ask_fp_fast, vd.vol_ask_fp_slow);
//       if (CFG_.flag_cuscore && sum_util::Find(constant::available_digital,
//                                               data::get_currency_name(base)))
//                                               {
//         DataProcess::update_cuscore(cc, CFG_, fp_bid, fp_ask);
//       }
//       flag_ready = (N < 600) ? false : true;
//     } else if (method == data::VolatilityMethod::METHOD_FP_DIFF ||
//                method == data::VolatilityMethod::METHOD_FP_DIFF_SHARED) {
//       flag_ready = (vd.fp_ask < 0) ? false : true;
//       const auto &diff2_bid = (vd.fp_bid < 0)
//                                   ? fp_bid * 0.0001
//                                   : sum_util::VSquare(vd.fp_bid - fp_bid);
//       const auto &diff2_ask = (vd.fp_ask < 0)
//                                   ? fp_ask * 0.0001
//                                   : sum_util::VSquare(vd.fp_ask - fp_ask);
//       if (vd.vol_bid_fp_fast < 0) {
//         vd.ema_bid_fast = diff2_bid;
//         vd.ema_ask_fast = diff2_ask;
//         vd.ema_bid_slow = diff2_bid;
//         vd.ema_ask_slow = diff2_ask;
//       } else {
//         calculate_util::EMAStepInplace(vd.ema_bid_fast, diff2_bid,
//                                        ema_fast_alpha2);
//         calculate_util::EMAStepInplace(vd.ema_ask_fast, diff2_ask,
//                                        ema_fast_alpha2);
//         calculate_util::EMAStepInplace(vd.ema_bid_slow, diff2_bid,
//                                        ema_fast_alpha);
//         calculate_util::EMAStepInplace(vd.ema_ask_slow, diff2_ask,
//                                        ema_fast_alpha);
//       }
//       vd.vol_bid_fp_fast = std::sqrt(vd.ema_bid_fast * 5);
//       vd.vol_ask_fp_fast = std::sqrt(vd.ema_ask_fast * 5);
//       vd.vol_bid_fp_slow = std::sqrt(vd.ema_bid_slow * 5);
//       vd.vol_ask_fp_slow = std::sqrt(vd.ema_ask_slow * 5);
//       vd.vol_bid_fp = (method == data::VolatilityMethod::METHOD_FP_DIFF)
//                           ? vd.vol_bid_fp_fast
//                           : std::max(vd.vol_bid_fp_fast, vd.vol_bid_fp_slow);
//       vd.vol_ask_fp = (method == data::VolatilityMethod::METHOD_FP_DIFF)
//                           ? vd.vol_ask_fp_fast
//                           : std::max(vd.vol_ask_fp_fast, vd.vol_ask_fp_slow);
//       if (CFG_.flag_slope &&
//           static_cast<size_t>(sd.slope_cnt) > sd.fps_begin.size()) {
//         vd.vol_bid_fp *= (std::abs(sd.slope_bid) > 0.5) ? sd.factor_bid
//         : 1.0; vd.vol_ask_fp *= (std::abs(sd.slope_ask) > 0.5) ?
//         sd.factor_ask : 1.0;
//       }
//       if (CFG_.flag_cuscore && sum_util::Find(constant::available_digital,
//                                               data::get_currency_name(base)))
//                                               {
//         DataProcess::update_cuscore(cc, CFG_, fp_bid, fp_ask);
//       }
//       const auto &diff_ob_bid = fp_bid - dd.bids[0][0];
//       const auto &diff_ob_ask = fp_ask - dd.asks[0][0];
//       sum_util::EMAStepInplace(vd.ema_diffOb_bid2, diff_ob_bid,
//                                ema_fast_alpha2);
//       sum_util::EMAStepInplace(vd.ema_diffOb_ask2, diff_ob_ask,
//                                ema_fast_alpha2);
//       if (vd.cnt_diff_plus == 0 && vd.cnt_diff_minus == 0) {
//         if (diff_ob_bid > 0)
//           vd.cnt_diff_plus++;
//         else
//           vd.cnt_diff_minus++;
//       } else if (vd.cnt_diff_plus != 0) {
//         if (diff_ob_bid > 0)
//           vd.cnt_diff_plus++;
//         else {
//           vd.cnt_diff_plus = 0;
//           vd.cnt_diff_minus++;
//         }
//       } else if (vd.cnt_diff_minus != 0) {
//         if (diff_ob_bid < 0)
//           vd.cnt_diff_minus++;
//         else {
//           vd.cnt_diff_minus = 0;
//           vd.cnt_diff_plus++;
//         }
//       }
//       const auto &diff2_ob_bid = sum_util::VSquare(diff_ob_bid);
//       const auto &diff2_ob_ask = sum_util::VSquare(diff_ob_ask);
//       sum_util::EMAStepInplace(vd.ema_diffOb_bid, diff2_ob_bid,
//                                ema_fast_alpha2);
//       sum_util::EMAStepInplace(vd.ema_diffOb_ask, diff2_ob_ask,
//                                ema_fast_alpha2);
//       vd.fp_bid = fp_bid;
//       vd.fp_ask = fp_ask;
//     }
//   }
//   return flag_ready;
// }

void FairPriceGenerator::update_fp_insts() {
  // 你可以在这里放一个统一的 Params（也可以从 CFG_ 读）
  digital_fp::Params p;
  // 关键：我们只用两个“exchange”(kraken_internal + synthetic)，第三个空掉
  p.ex_w = {0.2, 0.2, 0.0};
  // 其它参数按你已有默认/配置即可
  // p.kappa_m / p.rho_z / p.gamma_z / p.alpha_* ... 按你现在那套 10ms/10s 配

  for (auto &one : InstData_.IM.IC_map_) {
    auto &IC = one.second;
    if (!IC.flag_trading)
      continue; // 你原逻辑：交易的合成inst才更新
    const auto &pc = GetPairConfig(CFG_.pair_configs, IC.inst_str);
    // // ===== 1) synthetic 通道：用已定价的 base/USD 与 quote/USD
    // // 合成base/quote=====
    // const auto &fps_base =
    //     fps_map_[IC.base].fps.get_latest(); // [bid, ask] in USD
    // const auto &fps_quote =
    //     fps_map_[IC.quote].fps.get_latest(); // [bid, ask] in USD

    // // 合成 wp_syn（保守）
    // std::array<double, 2> wp_syn;
    // wp_syn[0] = fps_base[0] / fps_quote[1]; // bid = base_bid / quote_ask
    // wp_syn[1] = fps_base[1] / fps_quote[0]; // ask = base_ask / quote_bid

    // 2) kraken 目标pair 本地通道：aim 用 bbo；internal 用 depth wp //
    auto &ob = depth_map[IC.uni_id]; // data::depths_data
    const double ob_bid = ob.bids[0][0];
    const double ob_ask = ob.asks[0][0];

    // // 合成时间戳：取更旧的那条
    // const uint64_t ts_base = fps_map_[IC.base].timestamps.get_latest()[0];
    // const uint64_t ts_quote = fps_map_[IC.quote].timestamps.get_latest()[0];
    // const uint64_t ts_syn = (ts_base < ts_quote) ? ts_base : ts_quote; //
    // todo
    // // aim wp：bid0/ask0
    // std::array<double, 2> wp_aim{{ob_bid, ob_ask}};
    // const uint64_t ts_aim =
    //     (ob.local_ts > 0) ? (uint64_t)ob.local_ts : (uint64_t)this->ts_tmp;

    // // internal lead：用该pair自身 depth 生成 wp(不需要转换)
    // const std::array<double, 2> wp_krk_depth =
    //     calculate_weighted_price(ob, ob.PC, true, &fps_quote);
    // const uint64_t ts_krk_depth =
    //     (ob.local_ts > 0) ? (uint64_t)ob.local_ts : (uint64_t)this->ts_tmp;

    // // ===== 3) 组装成“两条 lead 通道”喂给 digital_fp =====
    // // digital_fp::Quote2 aim_q = make_q2(wp_aim, ts_aim);
    // digital_fp::Quote2 aim_q = make_q2(wp_syn, ts_syn);

    // std::array<digital_fp::Ex2, 3> leads{};
    // // ex0 = kraken_internal：depth 通道
    // leads[0].depth = make_q2(wp_krk_depth, ts_krk_depth);
    // leads[0].bbo.ok = false;

    // // ex1 = synthetic：depth 通道
    // // leads[1].depth = make_q2(wp_syn, ts_syn);
    // leads[1].depth = make_q2(wp_aim, ts_aim);
    // leads[1].bbo.ok = false;

    // // ex2 unused
    // leads[2].depth.ok = false;
    // leads[2].bbo.ok = false;

    // auto res = digital_fp::compute_fp_bidask_3ex2src(
    //     aim_q, leads, (uint64_t)this->ts_tmp, p, IC.fp_state);
    // // ===== 4) 写回 IC.fp（失败则 fallback 回原比值法）=====
    // // if (res.ok) {
    // if (1 == 0) {
    //   IC.fp_bid = res.fp[0];
    //   IC.fp_ask = res.fp[1];
    // } else {
    //   // fallback：你原来的方式
    //   IC.fp_bid = wp_syn[0];
    //   IC.fp_ask = wp_syn[1];
    // }

    if (sum_util::Find(constant::available_digital, IC.base_str) &&
        IC.quote != data::currency::USD) { // 同一base的所有pair统一预期成交方向
      auto inst_Cusd_str =
          fmt::format("{}_usd.{}", IC.base_str, CFG_.aim_exchange);
      const auto &IC_Cusd = InstData_.IM.FindByInstStr(inst_Cusd_str);
      auto &ob_Cusd = depth_map[IC_Cusd->uni_id];
      auto direction_Cusd_bid =
          IC_Cusd->fp_bid > ob_Cusd.bids[0][0]; // true: 预测涨
      auto direction_Cusd_ask =
          IC_Cusd->fp_ask > ob_Cusd.asks[0][0]; // true: 预测涨
      auto direction_bid = IC.fp_bid > ob_bid;
      auto direction_ask = IC.fp_ask > ob_ask;
      // auto adjust_size = pc.wp_invalid_ticks * ob.tick_size;
      auto adjust_size = 0 * ob.tick_size;
      if (direction_Cusd_bid && !direction_bid) // Cusd预测涨 inst跌
      {
        IC.fp_bid = ob_bid + adjust_size; // inst拉回上涨预测
        IC.fp_ask = ob_ask + adjust_size; // inst拉回上涨预测
      }
      if (!direction_Cusd_bid && direction_bid) // Cusd预测跌 inst涨
      {
        IC.fp_bid = ob_bid - adjust_size; // inst拉回下跌预测
        IC.fp_ask = ob_ask - adjust_size; // inst拉回下跌预测
      }
      if (direction_Cusd_ask && !direction_ask) // Cusd预测涨 inst跌
      {
        IC.fp_bid = ob_bid + adjust_size; // inst拉回上涨预测
        IC.fp_ask = ob_ask + adjust_size; // inst拉回上涨预测
      }
      if (!direction_Cusd_ask && direction_ask) // Cusd预测跌 inst涨
      {
        IC.fp_bid = ob_bid - adjust_size; // inst拉回下跌预测
        IC.fp_ask = ob_ask - adjust_size; // inst拉回下跌预测
      }
    }

    // ===== 4.5) 计算 side-aware s_bps，阈值从 pair_configs.yml 每日重载 =====
    auto &vd = vol_map[IC.uni_id];
    if (std::isfinite(IC.fp_bid) && std::isfinite(IC.fp_ask) &&
        std::isfinite(ob_bid) && std::isfinite(ob_ask) && IC.fp_bid > 0.0 &&
        IC.fp_ask > 0.0 && ob_bid > 0.0 && ob_ask > 0.0) {
      vd.s_bid_bps = (std::log(IC.fp_bid) - std::log(ob_bid)) * 1e4;
      vd.s_ask_bps = (std::log(ob_ask) - std::log(IC.fp_ask)) * 1e4;
      vd.s_bps = std::max(vd.s_bid_bps, vd.s_ask_bps);
      vd.abs_s_bps = std::max(std::abs(vd.s_bid_bps), std::abs(vd.s_ask_bps));
    } else {
      vd.s_bid_bps = NAN;
      vd.s_ask_bps = NAN;
      vd.s_bps = NAN;
      vd.abs_s_bps = NAN;
    }
    vd.thr_bid_bps = pc.thr_bid_bps;
    vd.thr_ask_bps = pc.thr_ask_bps;
    // DEBUG_FLOG("inst: {}, fp_bid: {}, fp_ask: {}, ob_bid: {}, ob_ask: {}, "
    //            "s_bid_bps: {}, s_ask_bps: {}, s_edge_bps: {}, abs_edge_bps: "
    //            "{}",
    //            IC.inst_str, IC.fp_bid, IC.fp_ask, ob_bid, ob_ask,
    //            vd.s_bid_bps, vd.s_ask_bps, vd.s_bps, vd.abs_s_bps);

    // ===== 5) 保留你原来的偏离检测 =====
    if (CFG_.adjust_inst_fp) {
      if (sum_util::Find(constant::available_digital, IC.base_str) &&
          (IC.quote_str == "eur" || IC.quote_str == "gbp")) {
        IC.fp_bid -= vd.ema_diffOb_bid2;
        IC.fp_ask -= vd.ema_diffOb_ask2;
      }
    }

    if (IC.fp_bid - ob_bid > ob_bid * 0.2)
      vd.ts_up_diff_too_large = this->ts_tmp;
    else if (ob_ask - IC.fp_ask > ob_ask * 0.2)
      vd.ts_down_diff_too_large = this->ts_tmp;
  }
}