#pragma once

#include "base/base_async_log.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace nova {
struct ModuleScheduler {
  // ===== 静态阈值 (init 时一次拷入, 单位 ns) =====
  int64_t negative_interval_ns = 0;
  int64_t fp_interval_max_ns = 0;
  int64_t fp_interval_min_ns = 0;
  int64_t order_interval_min_ns = 0;
  int64_t order_interval_max_ns = 0;
  int64_t disconnect_retry_ns = static_cast<int64_t>(1e9);
  double fp_turnover_usd = 0.0;
  double order_turnover_usd = 0.0;
  int fp_bbo_trigger_cnt = 50;    // BBO 累积次数触发阈值

  // ===== 运行时状态 =====
  int64_t last_disconnect_ts = 0;
  int64_t last_negative_ts = 0;
  int64_t last_fp_ts = 0;
  int64_t last_order_ts = 0;
  double acc_usd_fp = 0.0;
  double acc_usd_order = 0.0;
  int acc_bbo_cnt = 0;            // BBO 更新累积计数

  // 每 N 秒输出一次各 pair 推送频率 (调用方负责传入 current_ts_ns)
  inline void log_pair_frequency(int64_t now_ns, int64_t interval_ns = 30'000'000'000LL) {
    if (last_freq_log_ts_ == 0) {
      last_freq_log_ts_ = now_ns;
      return;
    }
    int64_t elapsed = now_ns - last_freq_log_ts_;
    if (elapsed < interval_ns)
      return;
    if (pair_push_cnt_.empty())
      return;

    // 按推送次数降序排列
    std::vector<std::pair<std::string, int64_t>> sorted(
        pair_push_cnt_.begin(), pair_push_cnt_.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });

    double elapsed_s = static_cast<double>(elapsed) / 1e9;
    fmt::memory_buffer buf;
    fmt::format_to(std::back_inserter(buf), "[PairFreq] {:.0f}s interval:\n", elapsed_s);
    for (const auto &[inst, cnt] : sorted) {
      double freq = static_cast<double>(cnt) / elapsed_s;
      fmt::format_to(std::back_inserter(buf), "  {} cnt={} freq={:.1f}/s\n", inst, cnt, freq);
    }
    INFO_FLOG("{}", fmt::to_string(buf));

    // 重置计数器
    pair_push_cnt_.clear();
    last_freq_log_ts_ = now_ns;
  }

  // ===== turnover 速率统计 (不被 mark_* reset) =====
  double acc_turnover_total = 0.0;
  double prev_turnover_total = 0.0;
  int64_t last_turnover_rate_ts = 0;
  double turnover_rate_max = 0.0;   // 本次运行观测到的最大1秒速率

  // ===== 状态flag (与节拍同生命周期, 集中放这里避免散落在 strategy 类) =====
  bool flag_first = true;
  bool flag_data_ready = false;
  bool disconnect_flag = false;
  int64_t last_aim_data_ts_ = 0;     // aim exchange 最近数据到达时间, 0=未初始化
  std::atomic<int> new_data_count_{0}; // on_datainfo 计数, on_poll 消费
  bool flag_new_data = false;          // on_datainfo 设置, mark_fp 清除

  // ===== 每 pair 推送频率统计 =====
  std::unordered_map<std::string, int64_t> pair_push_cnt_;
  int64_t last_freq_log_ts_ = 0;

  inline void init(double negative_interval_ms, double fp_turnover_usd_,
                   double order_turnover_usd_,
                   double fp_interval_min_ms, double fp_interval_max_ms,
                   double order_interval_min_ms, double order_interval_max_ms,
                   int64_t now_ts) {
    negative_interval_ns = static_cast<int64_t>(negative_interval_ms * 1e6);
    fp_interval_min_ns = static_cast<int64_t>(fp_interval_min_ms * 1e6);
    fp_interval_max_ns = static_cast<int64_t>(fp_interval_max_ms * 1e6);
    order_interval_min_ns = static_cast<int64_t>(order_interval_min_ms * 1e6);
    order_interval_max_ns = static_cast<int64_t>(order_interval_max_ms * 1e6);
    fp_turnover_usd = fp_turnover_usd_;
    order_turnover_usd = order_turnover_usd_;
    last_disconnect_ts = last_negative_ts = last_fp_ts = last_order_ts = now_ts;
  }

  inline void add_turnover(double usd) {
    if (usd <= 0)
      return;
    acc_usd_fp += usd;
    acc_usd_order += usd;
    acc_turnover_total += usd;
  }

  // 每秒打一次 turnover 速率 (调用方负责传入 current_ts_ns)
  inline void log_turnover_rate(int64_t now_ns) {
    constexpr int64_t kIntervalNs = 1'000'000'000LL; // 1 秒
    if (last_turnover_rate_ts == 0) {
      last_turnover_rate_ts = now_ns;
      prev_turnover_total = acc_turnover_total;
      return;
    }
    int64_t elapsed = now_ns - last_turnover_rate_ts;
    if (elapsed < kIntervalNs)
      return;
    double delta = acc_turnover_total - prev_turnover_total;
    double rate = delta / (static_cast<double>(elapsed) / 1e9);
    if (rate > turnover_rate_max)
      turnover_rate_max = rate;
    INFO_FLOG("[TurnoverRate] {:.0f} $/s (1s delta={:.0f}$, total={:.0f}$, "
              "max_rate={:.0f}$/s, acc_fp={:.0f}$, acc_order={:.0f}$)",
              rate, delta, acc_turnover_total, turnover_rate_max, acc_usd_fp,
              acc_usd_order);
    prev_turnover_total = acc_turnover_total;
    last_turnover_rate_ts = now_ns;
  }

  // ===== 谓词 =====
  inline bool should_disconnect(int64_t ts) {
    // 检测 aim exchange 断连: 30s 无数据 → 触发撤单
    if (last_aim_data_ts_ > 0 &&
        ts - last_aim_data_ts_ > 30'000'000'000LL) {
      disconnect_flag = true;
      last_aim_data_ts_ = 0;
    }
    return disconnect_flag && ts - last_disconnect_ts > disconnect_retry_ns;
  }

  inline bool should_negative(int64_t ts) const {
    return ts - last_negative_ts > negative_interval_ns;
  }

  inline bool should_fp(int64_t ts) const {
    return (acc_usd_fp >= fp_turnover_usd &&
            ts - last_fp_ts > fp_interval_min_ns) ||
           ts - last_fp_ts > fp_interval_max_ns;       // 成交额+最小间隔 或 最大间隔兜底
  }
  inline bool should_order(int64_t ts) const {
    return (acc_usd_order >= order_turnover_usd &&
            ts - last_order_ts > order_interval_min_ns) ||
           ts - last_order_ts > order_interval_max_ns;
  }

  // ===== Mark fired (fp/order 触发后清零累计成交额) =====
  inline void mark_disconnect(int64_t ts) {
    last_disconnect_ts = ts;
    disconnect_flag = false;
  }
  inline void mark_negative(int64_t ts) { last_negative_ts = ts; }
  inline void mark_fp(int64_t ts) {
    last_fp_ts = ts;
    acc_usd_fp = 0.0;
    acc_bbo_cnt = 0;
    flag_new_data = false;
  }
  inline void mark_order(int64_t ts) {
    last_order_ts = ts;
    acc_usd_order = 0.0;
  }
};
} // namespace nova