#pragma once

#include <cstdint>

namespace nova {
struct ModuleScheduler {
  // ===== 静态阈值 (init 时一次拷入, 单位 ns) =====
  int64_t negative_interval_ns = 0;
  int64_t fp_interval_max_ns = 0;
  int64_t order_interval_max_ns = 0;
  int64_t disconnect_retry_ns = static_cast<int64_t>(1e9);
  double fp_turnover_usd = 0.0;
  double order_turnover_usd = 0.0;

  // ===== 运行时状态 =====
  int64_t last_disconnect_ts = 0;
  int64_t last_negative_ts = 0;
  int64_t last_fp_ts = 0;
  int64_t last_order_ts = 0;
  double acc_usd_fp = 0.0;
  double acc_usd_order = 0.0;

  // ===== 状态flag (与节拍同生命周期, 集中放这里避免散落在 strategy 类) =====
  bool flag_first = true;
  bool flag_data_ready = false;
  bool disconnect_flag = false;
  int new_data_count_ = 0; // on_datainfo 计数, on_poll 消费

  inline void init(double negative_interval_ms, double fp_turnover_usd_,
                   double order_turnover_usd_, double fp_interval_max_ms,
                   double order_interval_max_ms, int64_t now_ts) {
    negative_interval_ns = static_cast<int64_t>(negative_interval_ms * 1e6);
    fp_interval_max_ns = static_cast<int64_t>(fp_interval_max_ms * 1e6);
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
  }

  // ===== 谓词 =====
  inline bool should_disconnect(int64_t ts) const {
    return disconnect_flag && ts - last_disconnect_ts > disconnect_retry_ns;
  }

  inline bool should_negative(int64_t ts) const {
    return ts - last_negative_ts > negative_interval_ns;
  }

  inline bool should_fp(int64_t ts) const {
    return acc_usd_fp >= fp_turnover_usd ||
           ts - last_fp_ts > fp_interval_max_ns;
  }
  inline bool should_order(int64_t ts) const {
    return acc_usd_order >= order_turnover_usd ||
           ts - last_order_ts > order_interval_max_ns;
  }

  // ===== Mark fired (fp/order 触发后清零累计成交额) =====
  inline void mark_disconnect(int64_t ts) { last_disconnect_ts = ts; }
  inline void mark_negative(int64_t ts) { last_negative_ts = ts; }
  inline void mark_fp(int64_t ts) {
    last_fp_ts = ts;
    acc_usd_fp = 0.0;
  }
  inline void mark_order(int64_t ts) {
    last_order_ts = ts;
    acc_usd_order = 0.0;
  }
};
} // namespace nova