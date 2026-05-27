#include "data_process.h"

namespace DataProcess {

using namespace sum_util;

// 通用交易所判断函数
bool is_main_exchange(const std::string &inst_str,
                      const std::string &aim_exchange) {
  return sum_util::EndsWith(inst_str, aim_exchange);
}

// 获取交易所的手续费
double get_exchange_taker_fee(const Configs &CFG_,
                              const std::string &exchange) {
  if (exchange == "krk")
    return CFG_.Strategy.Stable.krk_taker_fee;
  else if (exchange == "gt")
    return CFG_.Strategy.Stable.gt_taker_fee;
  else if (exchange == "bn")
    return CFG_.Strategy.Stable.bn_taker_fee;
  else if (exchange == "ok")
    return CFG_.Strategy.Stable.ok_taker_fee;
  else if (exchange == "mexc")
    return 0.0002;
  else if (exchange == "cb")
    return CFG_.Strategy.Stable.cb_taker_fee;
  else if (exchange == "idealpro")
    return 0.0;
  else
    throw std::invalid_argument("invalid exchange!");
}

// 格式化主交易所的USD交易对
std::string format_main_exchange_usd_pair(const std::string &currency_name,
                                          const std::string &aim_exchange) {
  return fmt::format("{}_usd.{}", currency_name, aim_exchange);
}

// 格式化主交易所的NONE交易对
std::string format_main_exchange_none_pair(const std::string &currency_name,
                                           const std::string &aim_exchange) {
  return fmt::format("{}_none.{}", currency_name, aim_exchange);
}

// 格式化主交易所的交叉交易对
std::string format_main_exchange_cross_pair(const std::string &base_currency,
                                            const std::string &quote_currency,
                                            const std::string &aim_exchange) {
  return fmt::format("{}_{}.{}", base_currency, quote_currency, aim_exchange);
}

bool fetch_data(const nova::quote::DataInfo &one,
                data::InstrumentData &InstData_, bool &flag_data_ready,
                const Configs &CFG_) {
  const auto &quote_type = one.quote_type();
  auto &id_map = InstData_.IM.inststr2id_;
  std::string inst_str;
  int64_t ts = 0;
  if (CFG_.Strategy.Verbose.ob)
    DEBUG_FLOG("[Ob] qtype={}", (int)quote_type);
  if (quote_type == NOVA_COIN_QUOTE_DEPTH) {
    const auto &data = *static_cast<const Depth *>(one.buffer().back());
    inst_str = data.instrument_id.symbol;
    ts = data.local_time;
    auto id = id_map.at(inst_str);
    auto &depth_map = InstData_.depth_map[id];
    if (CFG_.Strategy.Verbose.ob)
      DEBUG_FLOG("[Ob] depth {} bid0={:.4f} ask0={:.4f}", inst_str,
                 data.bid[0].price, data.ask[0].price);
    if (static_cast<uint64_t>(depth_map.sequence_num) < data.sequence_num)
      extract_depth_data(data, depth_map, InstData_.delay_map);
  } else if (quote_type == NOVA_COIN_QUOTE_DEPTH_LVN) {
    const auto &data = *static_cast<const DepthLVN *>(one.buffer().back());
    inst_str = data.instrument_id.symbol;
    ts = data.local_time;
    auto id = id_map.at(inst_str);
    auto &depth_map = InstData_.depth_map[id];
    if (CFG_.Strategy.Verbose.ob)
      DEBUG_FLOG("[Ob] depth_lvn {} bid0={:.4f} ask0={:.4f}", inst_str,
                 data.bid[0].price, data.ask[0].price);
    extract_depth_data(data, depth_map, InstData_.delay_map);
  } else if (quote_type == NOVA_COIN_QUOTE_BBO) {
    const auto &data = *static_cast<const BBO *>(one.buffer().back());
    inst_str = data.instrument_id.symbol;
    ts = data.local_time;
    auto id = id_map.at(inst_str);
    auto &bbo_map = InstData_.bbo_map[id];
    if (CFG_.Strategy.Verbose.ob)
      DEBUG_FLOG("[Ob] bbo {} bid={:.4f} ask={:.4f}", inst_str, data.bid_price,
                 data.ask_price);
    extract_bbo_data(data, bbo_map, InstData_.delay_map);
  } else if (quote_type == NOVA_COIN_QUOTE_BAR) {
    const auto &data = *static_cast<const Bar *>(one.buffer().back());
    inst_str = data.instrument_id.symbol;
    ts = data.local_time;
    auto id = id_map.at(inst_str);
    auto &bar_map = InstData_.bar_map[id];
    if (CFG_.Strategy.Verbose.ob)
      DEBUG_FLOG("{} bar high: {}, low: {}, open: {}, close: {}", inst_str,
                 data.high, data.low, data.open, data.close);
    extract_bar_data(data, bar_map, InstData_.delay_map);
  } else if (quote_type == NOVA_COIN_QUOTE_TRADE) {
    const auto &data = *static_cast<const Trade *>(one.buffer().back());
    inst_str = data.instrument_id.symbol;
    ts = data.local_time;
    auto it = id_map.find(inst_str);
    if (it == id_map.end()) return false;
    auto id = it->second;
    // 先判断是否在 trade_map 中, 是才记录
    if (!InstData_.trade_map.count(id)) return false;
    InstData_.trade_map[id].add_trade(data.local_time, data.side, data.price,
                                      data.qty);
    // 日志放在 depth/bbo 判断前面, 确保 turnover-only inst 也能打印
    if (CFG_.Strategy.Verbose.ob)
      DEBUG_FLOG("[Ob] trade {} side={} price={:.4f} qty={:.6f}", inst_str,
                 data.side == NOVA_SIDE_BUY ? "BUY" : "SELL", data.price,
                 data.qty);
    // turnover-only inst 没有 depth/bbo, 跳过后续更新
    if (!InstData_.depth_map.count(id) && !InstData_.bbo_map.count(id))
      return false;
    if (InstData_.depth_map.count(id) && InstData_.bbo_map.count(id)) {
      auto &depth_map = InstData_.depth_map[id];
      auto &bbo_map = InstData_.bbo_map[id];
      extract_trade_data(data, depth_map, bbo_map, InstData_.delay_map);
    }
  } else {
    throw std::invalid_argument("invalid quote type!");
  }
  auto &id = id_map.at(inst_str);
  if (quote_type == NOVA_COIN_QUOTE_DEPTH ||
      quote_type == NOVA_COIN_QUOTE_DEPTH_LVN)
    if (DataProcess::is_main_exchange(inst_str,
                                      CFG_.Strategy.Stable.aim_exchange)) {
      // ASSERT_SINGLE_THREAD();
      const auto &depth = InstData_.depth_map.at(id);
      const auto bid0 = depth.bids[0][0];
      const auto ask0 = depth.asks[0][0];
      auto &vol = InstData_.vol_map[id];
      if (bid0 > vol.bid_high)
        vol.bid_high = bid0;
      if (bid0 < vol.bid_low)
        vol.bid_low = bid0;
      if (ask0 > vol.ask_high)
        vol.ask_high = ask0;
      if (ask0 < vol.ask_low)
        vol.ask_low = ask0;
    }
  if (!flag_data_ready) {
    if (InstData_.global_ts == 0)
      InstData_.global_ts = ts;
    if (ts - InstData_.global_ts > 1e10)
      return true;
    else
      return false;
  }
  return true;
}

void fetch_data_all(const DataInfoManager *datainfo,
                    data::InstrumentData &InstData_, const Configs &CFG_) {
  // * 实盘中datainfo推送可以有多条
  // * 只要更新了depth/depthlvn/bbo把所有depth/bbo更新
  // ! v中订阅了没有数据的值是unknown, 需要筛选掉
  const auto &v = datainfo->datainfo();
  auto flag_data_ready = true;
  for (const auto &one : v) {
    if (!one.buffer().empty())
      fetch_data(one, InstData_, flag_data_ready, CFG_);
  }
}

void extract_depth_data(
    const Depth &md, data::depths_data &dd,
    std::unordered_map<uint16_t, data::delay_data> &delay_map) {
  if (md.update_time <= dd.server_ts) {
    return;
  }
  std::string exch = GetExchangeStrFromId(md.instrument_id.exchange);
  dd.server_ts = md.update_time;
  dd.local_ts = md.local_time;
  dd.valid = md.instrument_id.Valid();
  dd.ticker_len = md.instrument_id.ticker_len;
  dd.sequence_num = md.sequence_num;
  dd.reserved = md.reserved;
  memcpy(&dd.bids, md.bid, 25 * 2 * sizeof(double));
  memcpy(&dd.asks, md.ask, 25 * 2 * sizeof(double));

  // Calculate and update delay
  if (dd.server_ts < 1 && !sum_util::StrEqual(exch, "bn"))
    INFO_FLOG("[extract_depth_data] {} depth data abnormal, server_ts: {}",
              exch, dd.server_ts);
  if (dd.server_ts < 1 && sum_util::StrEqual(exch, "bn"))
    return;
  auto delay_key = data::delay_data::make_key(md.instrument_id.exchange,
                                              md.instrument_id.inst_type);
  auto &delay_stat = delay_map[delay_key];
  double delay =
      static_cast<double>(dd.local_ts - dd.server_ts) / 1e6; // convert to ms
  delay_stat.delay_sum += delay;
  delay_stat.delay_cnt++;
  delay_stat.delay_max = std::max(delay_stat.delay_max, delay);
  delay_stat.delay_min = std::min(delay_stat.delay_min, delay);
}

void extract_depth_data(
    const DepthLVN &md, data::depths_data &dd,
    std::unordered_map<uint16_t, data::delay_data> &delay_map) {
  if (md.update_time <= dd.server_ts) {
    return;
  }
  std::string exch = GetExchangeStrFromId(md.instrument_id.exchange);
  dd.server_ts = md.update_time;
  dd.local_ts = md.local_time;
  dd.valid = md.instrument_id.Valid();
  dd.ticker_len = md.instrument_id.ticker_len;
  dd.sequence_num = md.sequence_num;
  memcpy(&dd.bids, md.bid, 100 * 2 * 2 * sizeof(double));

  // Calculate and update delay
  if (dd.server_ts < 1)
    INFO_FLOG("[extract_depth_data] {} depth data abnormal, server_ts: {}",
              exch, dd.server_ts);
  auto delay_key = data::delay_data::make_key(md.instrument_id.exchange,
                                              md.instrument_id.inst_type);
  double delay =
      static_cast<double>(dd.local_ts - dd.server_ts) / 1e6; // convert to ms
  auto &delay_stat = delay_map[delay_key];
  delay_stat.delay_sum += delay;
  delay_stat.delay_cnt++;
  delay_stat.delay_max = std::max(delay_stat.delay_max, delay);
  delay_stat.delay_min = std::min(delay_stat.delay_min, delay);
}

void extract_bbo_data(const BBO &md, data::bbo_data &dd,
                      std::unordered_map<uint16_t, data::delay_data> &delay_map,
                      double alpha_slow, double alpha_fast) {
  if (md.update_time <= dd.server_ts) {
    return;
  }
  std::string exch = GetExchangeStrFromId(md.instrument_id.exchange);
  dd.server_ts = md.update_time;
  dd.local_ts = md.local_time;
  dd.ticker_len = md.instrument_id.ticker_len;
  dd.bid[0] = md.bid_price;
  dd.bid[1] = md.bid_qty;
  dd.ask[0] = md.ask_price;
  dd.ask[1] = md.ask_qty;
  if (!dd.valid) {
    dd.bid_ema_slow = md.bid_price;
    dd.ask_ema_slow = md.ask_price;
    dd.bid_ema_fast = md.bid_price;
    dd.ask_ema_fast = md.ask_price;
  } else {
    sum_util::EMAStepInplace(dd.bid_ema_slow, md.bid_price, alpha_slow);
    sum_util::EMAStepInplace(dd.ask_ema_slow, md.ask_price, alpha_slow);
    sum_util::EMAStepInplace(dd.bid_ema_fast, md.bid_price, alpha_fast);
    sum_util::EMAStepInplace(dd.ask_ema_fast, md.ask_price, alpha_fast);
  }
  dd.valid = md.instrument_id.Valid();

  // Calculate and update delay
  if (dd.server_ts < 1 && !sum_util::StrEqual(exch, "bn") &&
      !sum_util::StrEqual(exch, "idealpro"))
    ERROR_FLOG("[extract_bbo_data] {} bbo data abnormal, server_ts: {}", exch,
               dd.server_ts);
  if (dd.server_ts < 1 && sum_util::StrEqual(exch, "bn"))
    return;
  auto delay_key = data::delay_data::make_key(md.instrument_id.exchange,
                                              md.instrument_id.inst_type);
  double delay =
      static_cast<double>(dd.local_ts - dd.server_ts) / 1e6; // convert to ms
  auto &delay_stat = delay_map[delay_key];
  delay_stat.delay_sum += delay;
  delay_stat.delay_cnt++;
  delay_stat.delay_max = std::max(delay_stat.delay_max, delay);
  delay_stat.delay_min = std::min(delay_stat.delay_min, delay);
}

void extract_bar_data(
    const Bar &md, data::bar_data &dd,
    std::unordered_map<uint16_t, data::delay_data> &delay_map) {
  if (md.update_time <= dd.server_ts) {
    return;
  }
  dd.server_ts = md.update_time;
  dd.local_ts = md.local_time;
  dd.valid = md.instrument_id.Valid();
  dd.ticker_len = md.instrument_id.ticker_len;
  dd.high = md.high;
  dd.low = md.low;
  dd.open = md.open;
  dd.close = md.close;

  // Calculate and update delay
  if (!sum_util::EndsWith(md.instrument_id.GetSymbol(), "idealpro")) {
    auto delay_key = data::delay_data::make_key(md.instrument_id.exchange,
                                                md.instrument_id.inst_type);
    double delay =
        static_cast<double>(dd.local_ts - dd.server_ts) / 1e6; // convert to ms
    auto &delay_stat = delay_map[delay_key];
    delay_stat.delay_sum += delay;
    delay_stat.delay_cnt++;
    delay_stat.delay_max = std::max(delay_stat.delay_max, delay);
    delay_stat.delay_min = std::min(delay_stat.delay_min, delay);
  }
}

void extract_trade_data(
    const Trade &md, data::depths_data &dd, data::bbo_data &bd,
    std::unordered_map<uint16_t, data::delay_data> &delay_map) {
  if (md.timestamp < dd.server_ts) {
    return;
  }
  // 更新时间戳
  dd.local_ts = md.local_time;
  dd.server_ts = md.timestamp;
  bd.local_ts = md.local_time;
  bd.server_ts = md.timestamp;

  double remaining_qty = md.qty;

  TRACE_FLOG("[extract_trade_data] inst: {}, side: {}, price: {}, qty: {}",
             md.instrument_id.symbol, md.side == NOVA_SIDE_BUY ? "BUY" : "SELL",
             md.price, md.qty);

  // 根据交易方向逐档消耗挂单量
  // NOVA_SIDE_BUY: 买单成交，说明吃掉了ask侧的挂单
  // NOVA_SIDE_SELL: 卖单成交，说明吃掉了bid侧的挂单
  if (md.side == NOVA_SIDE_BUY) {
    // 买单成交，从ask侧逐档消耗成交量
    int remove_start = -1; // 记录被完全消耗档位的起始位置
    double old_ask0_price = dd.asks[0][0];
    double old_ask0_qty = dd.asks[0][1];

    for (int i = 0; i < 100 && remaining_qty > 1e-10; i++) {
      if (dd.asks[i][1] <= 1e-10) // 该档位已空，跳过
        continue;

      if (remaining_qty >= dd.asks[i][1]) {
        // 成交量大于等于该档位量，完全消耗该档
        if (remove_start == -1)
          remove_start = i;
        TRACE_FLOG(
            "[extract_trade_data] ask[{}] fully consumed: price={}, qty={}", i,
            dd.asks[i][0], dd.asks[i][1]);
        remaining_qty -= dd.asks[i][1];
        dd.asks[i][1] = 0;
      } else {
        // 成交量小于该档位量，部分消耗
        TRACE_FLOG("[extract_trade_data] ask[{}] partially consumed: price={}, "
                   "qty {} -> {}",
                   i, dd.asks[i][0], dd.asks[i][1],
                   dd.asks[i][1] - remaining_qty);
        dd.asks[i][1] -= remaining_qty;
        remaining_qty = 0;
        break;
      }
    }

    // 如果有档位被完全消耗，使用 memmove 批量移动内存
    if (remove_start >= 0) {
      int first_valid = remove_start;
      // 找到第一个有效档位
      while (first_valid < 100 && dd.asks[first_valid][1] <= 1e-10) {
        first_valid++;
      }

      if (first_valid < 100 && first_valid > remove_start) {
        // 使用 memmove 批量移动剩余档位到前面
        int move_count = 100 - first_valid;
        TRACE_FLOG("[extract_trade_data] memmove ask: remove_start={}, "
                   "first_valid={}, move_count={}",
                   remove_start, first_valid, move_count);
        std::memmove(&dd.asks[remove_start], &dd.asks[first_valid],
                     move_count * sizeof(dd.asks[0]));
        // 清空后续位置
        int clear_start = remove_start + move_count;
        std::memset(&dd.asks[clear_start], 0,
                    (100 - clear_start) * sizeof(dd.asks[0]));
      } else if (first_valid >= 100) {
        // 所有档位都被消耗完，全部清空
        TRACE_FLOG("[extract_trade_data] all ask levels consumed from {}",
                   remove_start);
        std::memset(&dd.asks[remove_start], 0,
                    (100 - remove_start) * sizeof(dd.asks[0]));
      }
    }

    // 更新bbo的ask为新的最优档
    bd.ask[0] = dd.asks[0][0];
    bd.ask[1] = dd.asks[0][1];

    TRACE_FLOG("[extract_trade_data] ask updated: old_best=[{}, {}], "
               "new_best=[{}, {}]",
               old_ask0_price, old_ask0_qty, dd.asks[0][0], dd.asks[0][1]);

  } else if (md.side == NOVA_SIDE_SELL) {
    // 卖单成交，从bid侧逐档消耗成交量
    int remove_start = -1; // 记录被完全消耗档位的起始位置
    double old_bid0_price = dd.bids[0][0];
    double old_bid0_qty = dd.bids[0][1];

    for (int i = 0; i < 100 && remaining_qty > 1e-10; i++) {
      if (dd.bids[i][1] <= 1e-10) // 该档位已空，跳过
        continue;

      if (remaining_qty >= dd.bids[i][1]) {
        // 成交量大于等于该档位量，完全消耗该档
        if (remove_start == -1)
          remove_start = i;
        TRACE_FLOG(
            "[extract_trade_data] bid[{}] fully consumed: price={}, qty={}", i,
            dd.bids[i][0], dd.bids[i][1]);
        remaining_qty -= dd.bids[i][1];
        dd.bids[i][1] = 0;
      } else {
        // 成交量小于该档位量，部分消耗
        TRACE_FLOG("[extract_trade_data] bid[{}] partially consumed: price={}, "
                   "qty {} -> {}",
                   i, dd.bids[i][0], dd.bids[i][1],
                   dd.bids[i][1] - remaining_qty);
        dd.bids[i][1] -= remaining_qty;
        remaining_qty = 0;
        break;
      }
    }

    // 如果有档位被完全消耗，使用 memmove 批量移动内存
    if (remove_start >= 0) {
      int first_valid = remove_start;
      // 找到第一个有效档位
      while (first_valid < 100 && dd.bids[first_valid][1] <= 1e-10) {
        first_valid++;
      }

      if (first_valid < 100 && first_valid > remove_start) {
        // 使用 memmove 批量移动剩余档位到前面
        int move_count = 100 - first_valid;
        TRACE_FLOG("[extract_trade_data] memmove bid: remove_start={}, "
                   "first_valid={}, move_count={}",
                   remove_start, first_valid, move_count);
        std::memmove(&dd.bids[remove_start], &dd.bids[first_valid],
                     move_count * sizeof(dd.bids[0]));
        // 清空后续位置
        int clear_start = remove_start + move_count;
        std::memset(&dd.bids[clear_start], 0,
                    (100 - clear_start) * sizeof(dd.bids[0]));
      } else if (first_valid >= 100) {
        // 所有档位都被消耗完，全部清空
        TRACE_FLOG("[extract_trade_data] all bid levels consumed from {}",
                   remove_start);
        std::memset(&dd.bids[remove_start], 0,
                    (100 - remove_start) * sizeof(dd.bids[0]));
      }
    }

    // 更新bbo的bid为新的最优档
    bd.bid[0] = dd.bids[0][0];
    bd.bid[1] = dd.bids[0][1];

    TRACE_FLOG("[extract_trade_data] bid updated: old_best=[{}, {}], "
               "new_best=[{}, {}]",
               old_bid0_price, old_bid0_qty, dd.bids[0][0], dd.bids[0][1]);
  }

  // 标记数据有效
  dd.valid = md.instrument_id.Valid();
  dd.ticker_len = md.instrument_id.ticker_len;
  bd.valid = md.instrument_id.Valid();
  bd.ticker_len = md.instrument_id.ticker_len;

  // Calculate and update delay
  auto delay_key = data::delay_data::make_key(md.instrument_id.exchange,
                                              md.instrument_id.inst_type);
  double delay =
      static_cast<double>(dd.local_ts - dd.server_ts) / 1e6; // convert to ms
  auto &delay_stat = delay_map[delay_key];
  delay_stat.delay_sum += delay;
  delay_stat.delay_cnt++;
  delay_stat.delay_max = std::max(delay_stat.delay_max, delay);
  delay_stat.delay_min = std::min(delay_stat.delay_min, delay);
}

std::array<double, 2> weighted_price_bbo(const double &ask, const double &askv,
                                         const double &bid, const double &bidv,
                                         const double &tick_size,
                                         const double &taker_fee,
                                         const double &vol_threshold_bid,
                                         const double &vol_threshold_ask) {
  std::array<double, 2> wps = {bid, ask};
  double bidq = bidv, askq = askv;
  if (askv > bidv * 5 && bidv != 0)
    askq = 5 * bidv;
  else if (askv * 5 < bidv && askv != 0)
    bidq = 5 * askv;

  // 检查成交量是否低于阈值
  if (bidv < vol_threshold_bid && askv < vol_threshold_ask) {
    wps[0] -= 0.5 * tick_size;
    wps[1] += 0.5 * tick_size;
  } else if (bidv < vol_threshold_bid && askv >= vol_threshold_ask)
    if (ask - bid - 1e-10 < tick_size) {
      wps[0] = (bid * (1 - taker_fee) * askq + ask * bidq) / (bidq + askq);
      wps[1] -= 0.5 * tick_size;
    } else {
      wps[0] = ((bid - 0.5 * tick_size) * (1 - taker_fee) * askq + ask * bidq) /
               (bidq + askq);
      wps[1] -= 0.5 * tick_size;
    }
  else if (bidv >= vol_threshold_bid && askv < vol_threshold_ask)
    if (ask - bid - 1e-10 < tick_size) {
      wps[0] += 0.5 * tick_size;
      wps[1] = (bid * askq + ask * (1 + taker_fee) * bidq) / (bidq + askq);
    } else {
      wps[0] += 0.5 * tick_size;
      wps[1] = (bid * askq + (ask + 0.5 * tick_size) * (1 + taker_fee) * bidq) /
               (bidq + askq);
    }
  else {
    double wp;
    if (ask - bid - 1e-10 < tick_size)
      wp = (bid * askq + ask * bidq) / (bidq + askq);
    else
      wp = (bid * (1 - taker_fee) * askq + ask * (1 + taker_fee) * bidq) /
           (bidq + askq);
    wps[0] = wp - 0.5 * tick_size;
    wps[1] = wp + 0.5 * tick_size;
  }
  if (wps[0] > wps[1]) {
    double wp_tmp = wps[0];
    wps[0] = wps[1];
    wps[1] = wp_tmp;
  }
  if (!std::isfinite(wps[0]) || !std::isfinite(wps[1])) {
    std::array<double, 2> invalid = {0.0, 0.0};
    return invalid;
  }
  return wps;
}

double fetch_attr(const data::currency &currency, const ryml::Tree &settings_,
                  const std::string &key) {
  auto currency_str = data::get_currency_name(currency);
  sum_util::StrUpper(currency_str); // 将 currency_str 转换为大写
  c4::csubstr currency_key(
      currency_str.c_str(),
      currency_str.size()); // 将 std::string 转换为 c4::csubstr
  // 使用传入的 key 参数来获取相应的值
  auto value = settings_[currency_key][key.c_str()].val();
  std::string value_str(value.str, value.len);
  return std::stod(value_str); // 将字符串转换为 double 返回
}

double get_currency_usd_mp(const std::string &currency_str,
                           const data::InstrumentData &InstData_) {
  const auto &inst_str = fmt::format("{}_usd.krk", currency_str);
  const auto &id = InstData_.IM.inststr2id_.at(inst_str);
  const auto &bid = InstData_.depth_map.at(id).bids[0][0];
  const auto &ask = InstData_.depth_map.at(id).asks[0][0];
  return (bid + ask) * 0.5;
}

double get_currency_hedge_usdt_mp(const std::string &currency_str,
                                  const data::InstrumentData &InstData_) {
  const auto &inst_str = fmt::format("{}_usdt.krk", currency_str);
  const auto &id = InstData_.IM.inststr2id_.at(inst_str);
  const auto &bid = InstData_.bbo_map.at(id).bid[0];
  const auto &ask = InstData_.bbo_map.at(id).ask[0];
  return (bid + ask) * 0.5;
}

std::string get_fps_obs_str(
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map_) {
  std::string op_fp = "fair price update for coin ";
  for (const auto &item : fps_map_) {
    const auto &currency = item.first;
    const auto &fp_data = item.second;
    double fp;
    try {
      fp = fp_data.fps.get_last();
    } catch (const std::exception &e) {
      std::string err_msg = e.what();
      ERROR_FLOG("", err_msg);
      exit(1);
    }
    op_fp += fmt::format("{} : {:.6f}", data::get_currency_name(currency), fp);
  }
  return op_fp;
}

std::string get_fps_obs_str(
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map_,
    const data::InstrumentData &InstData_) {
  std::string op_ob = "best ob update for coin ";
  for (const auto &item : fps_map_) {
    const auto &currency = item.first;
    const auto &currency_str = data::get_currency_name(currency);
    if (currency != data::currency::USD)
      continue;
    const auto &id =
        InstData_.IM.inststr2id_.at(fmt::format("{}_usd.krk", currency_str));
    const auto &depth_map = InstData_.depth_map.at(id);
    double bid0, ask0;
    try {
      bid0 = depth_map.bids[0][0];
      ask0 = depth_map.asks[0][0];
    } catch (const std::exception &e) {
      std::string err_msg = e.what();
      ERROR_FLOG("", err_msg);
      exit(1);
    }
    op_ob += fmt::format("{} : {:.6f} {:.6f}", currency_str, bid0, ask0);
  }
  return op_ob;
}

std::string get_fps_obs_str(
    const data::InstrumentData &InstData_,
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map_) {
  std::string op_ob = "fair price / best ob update for coin ";
  for (const auto &item : fps_map_) {
    const auto &currency = item.first;
    const auto &currency_str = data::get_currency_name(currency);
    if (currency != data::currency::USD)
      continue;
    const auto &id =
        InstData_.IM.inststr2id_.at(fmt::format("{}_usd.krk", currency_str));
    const auto &depth_map = InstData_.depth_map.at(id);
    const auto &fp_data = item.second;
    double fp, bid0, ask0;
    try {
      fp = fp_data.fps.get_last();
      bid0 = depth_map.bids[0][0];
      ask0 = depth_map.asks[0][0];
    } catch (const std::exception &e) {
      std::string err_msg = e.what();
      ERROR_FLOG("", err_msg);
      exit(1);
    }
    op_ob += fmt::format("{} bid0: {:.6f}, ask0: {:.6f}, fp: {:.6f} | ",
                         currency_str, bid0, ask0, fp);
  }
  return op_ob;
}

std::string get_fps_str(
    const Configs &CFG_, const data::InstrumentData &InstData_,
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map_) {
  std::string op_ob = "";
  for (const auto &currency : CFG_.Strategy.Stable.trading_currencies) {
    const auto &currency_str = data::get_currency_name(currency);
    if (currency != data::currency::USD) {
      op_ob += "1.0,";
      continue;
    }
    const auto &fp = fps_map_.at(currency).fps.get_last();
    op_ob += fmt::format("{},", fp);
  }
  for (const auto &forex : CFG_.Strategy.Stable.trading_currencies) {
    const auto &ib_str = fmt::format("{}_usd_cash.idealpro", forex);
    const auto &id_ib = InstData_.IM.inststr2id_.at(ib_str);
    if (sum_util::Find(InstData_.bbo_map, id_ib)) {
      const auto &fps_forex = InstData_.bbo_map.at(id_ib);
      if (fps_forex.valid) {
        const auto &vp_forex = (fps_forex.bid[0] + fps_forex.ask[0]) * 0.5;
        op_ob += fmt::format("{},", vp_forex);
      } else
        op_ob += "-1,";
    }
  }
  op_ob.pop_back();
  return op_ob;
}

std::string get_inst_str(const data::InstrumentData &InstData_,
                         const Configs &CFG_) {
  std::string op_inst = "instrument update for coin ";
  for (const auto &one : InstData_.depth_map) {
    const auto &id = one.first;
    auto *IC = InstData_.IM.FindByUniId(id);
    const auto &inst_str = IC->inst_str;
    const auto &depth_map = one.second;
    const auto &bid0 = depth_map.bids[0][0];
    const auto &ask0 = depth_map.asks[0][0];
    double vol_fp_bid, vol_fp_ask, fp_bid, fp_ask, cuscore_bid, cuscore_ask;
    int pp;
    if (DataProcess::is_main_exchange(inst_str,
                                      CFG_.Strategy.Stable.aim_exchange)) {
      const auto &vd = InstData_.vol_map.at(id);
      vol_fp_bid = vd.vol_bid_fp;
      vol_fp_ask = vd.vol_ask_fp;
      cuscore_bid = vd.cuscore.cuscore_bid_fast;
      cuscore_ask = vd.cuscore.cuscore_ask_fast;
      fp_bid = IC->fp_bid;
      fp_ask = IC->fp_ask;
      pp = IC->price_precision;
      auto bid = sum_util::VRound(bid0, pp);
      auto ask = sum_util::VRound(ask0, pp);
      fp_bid = sum_util::VRound(fp_bid, pp);
      fp_ask = sum_util::VRound(fp_ask, pp);
      vol_fp_bid = sum_util::VRound(vol_fp_bid, pp);
      vol_fp_ask = sum_util::VRound(vol_fp_ask, pp);
      op_inst +=
          fmt::format("{} bid0: {:.6f}, ask0: {:.6f}, fp_bid: {:.6f}, fp_ask: "
                      "{:.6f}, vol_fp_bid: {:.6f}, vol_fp_ask: {:.6f}, "
                      "cuscore_bid: {:.6f}, cuscore_ask: {:.6f} | ",
                      inst_str, bid, ask, fp_bid, fp_ask, vol_fp_bid,
                      vol_fp_ask, cuscore_bid, cuscore_ask);
    }
  }
  return op_inst;
}

std::string get_delay_str(data::InstrumentData &InstData_) {
  std::string op_delay = "delay statistics: ";
  for (auto &[key, stats] : InstData_.delay_map) {
    auto exchange = data::delay_data::get_exchange(key);
    auto inst_type = data::delay_data::get_inst_type(key);

    op_delay += fmt::format(
        "{}({}) mean: {:.2f}ms, max: {:.2f}ms, min: {:.2f}ms, cnt: {} | ",
        GetExchangeStrFromId(exchange), GetCoinInstTypeString(inst_type),
        stats.delay_mean, stats.delay_max, stats.delay_min, stats.delay_cnt);

    // Reset delay_data after printing
    stats.delay_sum = 0.0;
    stats.delay_mean = 0.0;
    stats.delay_cnt = 0;
    stats.delay_max = 0.0;
    stats.delay_min = 1e9;
  }
  return op_delay;
}

std::string get_hedge_position_str(
    const std::vector<data::currency> &digital_currencies,
    const data::InstrumentData &InstData_,
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map) {
  std::string op_pos = "hedge position update for coin(USD) ";
  for (const auto &currency : digital_currencies) {
    const auto &currency_str = data::get_currency_name(currency);
    const auto &inst_str = fmt::format("{}_usdt_swap.bn", currency_str);
    auto *IC = InstData_.IM.FindByInstStr(inst_str);
    const auto &OM = InstData_.swap_order_map.at(IC->uni_id);
    const auto &position = OM.position_->short_position;

    const auto &ask0_Cusd_krk = fps_map.at(currency).fps.get_last();
    const auto &vp_usdtusd_krk =
        fps_map.at(data::currency::USDT).fps.get_last();
    double multi = ask0_Cusd_krk / vp_usdtusd_krk /
                   InstData_.bbo_map.at(IC->uni_id).ask_ema_slow;
    double pos_usd = position / multi;
    op_pos +=
        fmt::format("{}: {:.6f} {:.6f}, ", currency_str, position, pos_usd);
  }
  return op_pos;
}

std::string get_balance_str(
    const std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    const std::vector<data::currency> &digital_currencies,
    const std::unordered_map<data::currency, data::hedge_position_data>
        &hedge_positions_,
    const std::string &inst_str) {
  std::string op =
      fmt::format("balance(hedge_positions) update({}) for coin ", inst_str);
  for (const auto &one : BlcMng_) {
    const auto &currency = one.first;
    const auto &balance = one.second.balance_live;
    std::string position = "";
    if (sum_util::Find(digital_currencies, currency)) {
      position = std::to_string(
          sum_util::VRound(hedge_positions_.at(currency).size, 4));
      op += std::string(data::get_currency_name(currency)) + ": " +
            std::to_string(sum_util::VRound(balance, 4)) + "()" + position +
            ") | ";
    }
  }
  return op;
}

std::string get_volume_str(
    const std::unordered_map<data::currency, data::volume_data> &volumes_,
    const std::string &inst_str) {
  std::string op = fmt::format("volume update({}) for coin ", inst_str);
  for (const auto &one : volumes_) {
    const auto &currency = one.first;
    const auto &volume = one.second.volume;
    op += std::string(data::get_currency_name(currency)) + ": " +
          std::to_string(sum_util::VRound(volume, 0)) + " | ";
  }
  return op;
}

std::string get_rate_limit_str(const data::InstrumentManager &InstManager_) {
  std::string op_rl = "rate limit update for instrument ";
  for (const auto &one : InstManager_.IC_map_) {
    const auto &IC = one.second;
    if (IC.flag_trading)
      op_rl += fmt::format("{}: {:.2f}, | ", IC.inst_str,
                           sum_util::VRound(IC.rate_limit, 2));
  }
  return op_rl;
}

std::vector<std::string>
get_currency_matching_insts(const data::currency &currency,
                            const data::InstrumentManager &InstManager_) {
  std::vector<std::string> matching_insts;
  for (const auto &one : InstManager_.IC_map_) {
    const auto &IC = one.second;
    if (IC.flag_trading && (currency == IC.base || currency == IC.quote))
      matching_insts.push_back(IC.inst_str);
  }
  return matching_insts;
}

double adjust_fair_price(
    const data::InstrumentComponent &IC, const data::currency &currency,
    const double &price, const double &quantity,
    const std::unordered_map<data::currency, data::Setting> &settings_,
    const std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    bool verbose, const double balance_adj, const double BP) {
  const auto &base = IC.base;
  const auto &quote = IC.quote;
  const auto &setting_base = settings_.at(base);
  const auto &setting_quote = settings_.at(quote);

  const double &tmp_max_percentage_base = setting_base.max_percentage + 1;
  const double &tmp_max_percentage_quote = setting_quote.max_percentage + 1;
  const auto &balanced_pos_base = setting_base.balance_quantity;
  const auto &balanced_pos_quote = setting_quote.balance_quantity;
  const auto &credit_base = setting_base.credit;
  const auto &credit_quote = setting_quote.credit;

  if (base == currency) {
    const auto &balanced_percentage_base_tmp =
        (BlcMng_.at(base).balance_live * balance_adj - quantity + credit_base) /
        (balanced_pos_base + credit_base);
    const auto &balanced_percentage_quote =
        (BlcMng_.at(quote).balance_live * balance_adj + quantity * price +
         credit_quote) /
        (balanced_pos_quote + credit_quote);
    if (verbose) {
      DEBUG_FLOG("[GetMargin-{}]", data::get_currency_name(currency));
    }
    if (balanced_percentage_quote > tmp_max_percentage_quote)
      return -1;
    const auto &balanced_percentage_base =
        std::min(balanced_percentage_base_tmp, tmp_max_percentage_base);
    const auto &sell_open_slop_base = setting_base.sell_open_slop;
    const auto &buy_open_slop_base = setting_base.buy_open_slop;
    const auto &sell_close_slop_base = setting_base.sell_close_slop;
    const auto &spread_base = setting_base.spread;
    const auto &sell_close_extension_base = setting_base.sell_close_extension;
    double adjust_base;
    if (balanced_percentage_base <= -1.0)
      adjust_base = -sell_open_slop_base * BP * (balanced_percentage_base - 1) +
                    1 + spread_base * BP;
    else if (1 + sell_close_extension_base > balanced_percentage_base &&
             balanced_percentage_base > 1)
      adjust_base = -buy_open_slop_base * BP * (balanced_percentage_base - 1) +
                    1 + spread_base * BP;
    else
      adjust_base =
          -sell_close_slop_base * BP * (balanced_percentage_base - 1) + 1 +
          spread_base * BP +
          (sell_close_slop_base * BP - buy_open_slop_base * BP) *
              sell_close_extension_base;

    const auto &buy_open_slop_quote = setting_quote.buy_open_slop;
    const auto &sell_open_slop_quote = setting_quote.sell_open_slop;
    const auto &buy_close_slop_quote = setting_quote.buy_close_slop;
    const auto &spread_quote = setting_quote.spread;
    const auto &buy_close_extension_quote = setting_quote.buy_close_extension;
    double adjust_quote;
    if (balanced_percentage_quote > 1.0)
      adjust_quote =
          -buy_open_slop_quote * BP * (balanced_percentage_quote - 1) + 1 -
          spread_quote * BP;
    else if (1 - buy_close_extension_quote < balanced_percentage_quote &&
             balanced_percentage_quote <= 1)
      adjust_quote =
          -sell_open_slop_quote * BP * (balanced_percentage_quote - 1) + 1 -
          spread_quote * BP;
    else
      adjust_quote =
          -buy_close_slop_quote * BP * (balanced_percentage_quote - 1) + 1 -
          spread_quote * BP -
          (buy_close_slop_quote * BP - sell_open_slop_quote * BP) *
              buy_close_extension_quote;
    return adjust_quote / adjust_base;
  } else {
    const auto &balanced_percentage_base =
        (BlcMng_.at(base).balance_live * balance_adj + quantity / price +
         credit_base) /
        (balanced_pos_base + credit_base);
    const auto &balanced_percentage_quote_tmp =
        (BlcMng_.at(quote).balance_live * balance_adj - quantity +
         credit_quote) /
        (balanced_pos_quote + credit_quote);
    if (balanced_percentage_base > tmp_max_percentage_base)
      return -1;
    const auto &buy_open_slop_base = setting_base.sell_open_slop;
    const auto &sell_open_slop_base = setting_base.buy_open_slop;
    const auto &buy_close_slop_base = setting_base.sell_close_slop;
    const auto &spread_base = setting_base.spread;
    const auto &buy_close_extension_base = setting_base.sell_close_extension;
    double adjust_base;
    if (balanced_percentage_base > 1.0)
      adjust_base = -buy_open_slop_base * BP * (balanced_percentage_base - 1) +
                    1 - spread_base * BP;
    else if (1 - buy_close_extension_base < balanced_percentage_base &&
             balanced_percentage_base <= 1)
      adjust_base = -sell_open_slop_base * BP * (balanced_percentage_base - 1) +
                    1 - spread_base * BP;
    else
      adjust_base = -buy_close_slop_base * BP * (balanced_percentage_base - 1) +
                    1 - spread_base * BP -
                    (buy_close_slop_base * BP - sell_open_slop_base * BP) *
                        buy_close_extension_base;

    const auto &balanced_percentage_quote =
        std::min(balanced_percentage_quote_tmp, tmp_max_percentage_quote);
    const auto &sell_open_slop_quote = setting_quote.buy_open_slop;
    const auto &buy_open_slop_quote = setting_quote.sell_open_slop;
    const auto &sell_close_slop_quote = setting_quote.buy_close_slop;
    const auto &spread_quote = setting_quote.spread;
    const auto &sell_close_extension_quote = setting_quote.buy_close_extension;
    double adjust_quote;
    if (balanced_percentage_quote <= 1.0)
      adjust_quote =
          -sell_open_slop_quote * BP * (balanced_percentage_quote - 1) + 1 +
          spread_quote * BP;
    else if (1 + sell_close_extension_quote > balanced_percentage_quote &&
             balanced_percentage_quote > 1)
      adjust_quote =
          -buy_open_slop_quote * BP * (balanced_percentage_quote - 1) + 1 +
          spread_quote * BP;
    else
      adjust_quote =
          -sell_close_slop_quote * BP * (balanced_percentage_quote - 1) + 1 +
          spread_quote * BP +
          (sell_close_slop_quote * BP - buy_open_slop_quote * BP) *
              sell_close_extension_quote;
    return adjust_quote / adjust_base;
  }
}

// 检查UTC时间是否在周五晚上22点到周日晚上22点之间
bool is_weekend_window_utc(int64_t timestamp_ns) {
  // 将纳秒时间戳转换为UTC时间
  auto tp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      std::chrono::time_point<std::chrono::system_clock,
                              std::chrono::nanoseconds>(
          std::chrono::nanoseconds(timestamp_ns)));
  std::time_t t = std::chrono::system_clock::to_time_t(tp);
  std::tm utc_tm;
  gmtime_r(&t, &utc_tm); // 使用UTC时间

  int weekday = utc_tm.tm_wday; // 0=Sunday, 1=Monday, ..., 5=Friday, 6=Saturday
  int hour = utc_tm.tm_hour;

  // 检查是否在周五22:00到周日22:00之间
  if (weekday == 5 && hour >= 22) { // 周五22:00及以后
    return true;
  } else if (weekday == 6) { // 周六全天
    return true;
  } else if (weekday == 0 && hour < 22) { // 周日22:00之前
    return true;
  }
  return false;
}

void calculate_expected_return(data::InstrumentData &InstData_,
                               const Configs &CFG_, bool verbose) {
  double expected_return = 0.0, usd_values = 0.0;
  for (auto &[id, OM] : InstData_.order_map) {
    auto *IC = InstData_.IM.FindByUniId(id);
    auto &left_orders = OM.left_order_;
    auto &operation_flags = OM.operation_flags;
    if (left_orders.empty())
      continue;
    const auto &inst_usd = format_main_exchange_usd_pair(
        IC->base_str, CFG_.Strategy.Stable.aim_exchange);
    auto *IC_usd = InstData_.IM.FindByInstStr(inst_usd);
    const auto &dd_usd = InstData_.depth_map[IC_usd->uni_id];
    for (auto *left_order : left_orders) {
      const auto &NvID = left_order->nova_id.id;
      auto &operation_flag = operation_flags[NvID];
      auto &order = left_order->order;
      const auto &depth_data =
          (order.side == NOVA_SIDE_BUY) ? dd_usd.bids : dd_usd.asks;
      const auto &usd_value = order.quantity * depth_data[0][0];
      usd_values += usd_value;
      expected_return += operation_flag.margin * usd_value;
      if (verbose)
        INFO_FLOG(
            "order.price: {}, depth: {}, usd_value: {}, expected_return: {}",
            order.price, depth_data[0][0], usd_value, expected_return);
    }
  }
}

void get_orders_margin(
    data::InstrumentData &InstData_, const Configs &CFG_,
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map_,
    const std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    bool verbose) {
  for (auto &[id, OM] : InstData_.order_map) {
    auto *IC = InstData_.IM.FindByUniId(id);
    const auto &dd = InstData_.depth_map[id];
    auto &left_orders = OM.left_order_;
    auto &operation_flags = OM.operation_flags;
    if (left_orders.empty())
      continue;
    for (auto *left_order : left_orders) {
      const auto &NvID = left_order->nova_id.id;
      auto &operation_flag = operation_flags[NvID];
      auto &order = left_order->order;
      double qty = 0.0, depth = 0.0, fp = 0.0, ret = 0.0;
      int level = 0;
      data::currency currency;
      if (order.side == NOVA_SIDE_BUY) {
        currency = IC->quote;
        qty = order.price * order.quantity;
        fp = IC->fp_bid;
        ret = fp / order.price;
        if (dd.bids[0][0] <= 0.0 || dd.bids[0][0] < order.price) {
          depth = 0.0;
          level = 0;
        } else {
          for (int i = 0; i < 100; i++) {
            const double &bid_price = dd.bids[i][0];
            const double &bid_qty = dd.bids[i][1];
            double depth_tmp = bid_price * bid_qty;
            if (bid_price > order.price) {
              level = i;
              depth += depth_tmp;
            } else if (std::abs(bid_price - order.price) < 1e-10) {
              depth += std::max(0.0, depth_tmp - order.quantity * order.price);
              level = i;
              break;
            } else {
              break;
            }
          }
        }
      } else {
        currency = IC->base;
        qty = order.quantity;
        fp = IC->fp_ask;
        ret = order.price / fp;
        if (dd.asks[0][0] <= 0.0 || dd.asks[0][0] > order.price) {
          depth = 0.0;
          level = 0;
        } else {
          for (int i = 0; i < 100; i++) {
            const double &ask_price = dd.asks[i][0];
            const double &ask_qty = dd.asks[i][1];
            if (ask_price < order.price) {
              level = i;
              depth += ask_qty;
            } else if (std::abs(ask_price - order.price) < 1e-10) {
              depth += std::max(0.0, ask_qty - order.quantity);
              level = i;
              break;
            } else {
              break;
            }
          }
        }
      }
      double factor = DataProcess::adjust_fair_price(*IC, currency, order.price,
                                                     qty, CFG_.Strategy.setting,
                                                     BlcMng_, verbose);
      INFO_FLOG("order.price: {}, depth: {}", order.price, depth);
      double ret_adjust =
          (order.side == NOVA_SIDE_SELL) ? ret / factor : ret * factor;
      double vp = fps_map_.at(currency).vps.get_last();
      auto prob_params = (order.side == NOVA_SIDE_SELL) ? IC->prob_params_sell
                                                        : IC->prob_params_buy;
      auto model = data::TailFillModel(prob_params);
      double EFU = model.expected_fill_usd(depth, qty, vp, 60);
      double edge0 = std::log(ret_adjust);
      int side = (order.side == NOVA_SIDE_SELL) ? 1 : 0;
      double drift = IC->markout_table.calculate_drift_cost_micro(
          side, order.price, IC->fp_bid, IC->fp_ask, 60);
      double edge_net = edge0 - drift;
      double margin = edge_net * EFU;
      if (edge0 > 0 && EFU > 0 && edge0 < drift) {
        auto ts_now = InstData_.global_ts;
        if (ts_now - operation_flag.create_ts < 60 * 1e9)
          operation_flag.drift_delay = true;
        else
          operation_flag.drift_delay = false;
      } else
        operation_flag.drift_delay = false;
      operation_flag.margin = margin;
      operation_flag.level = level;
      operation_flag.EFU = EFU;
    }
  }
}

std::vector<data::UniInstID>
get_relative_trading_ids(const data::InstrumentData &InstData_,
                         const data::currency &currency) {
  std::vector<data::UniInstID> relative_trading_ids;
  for (const auto &id : InstData_.trading_ids) {
    auto *IC = InstData_.IM.FindByUniId(id);
    if (IC->quote == currency || IC->base == currency)
      relative_trading_ids.push_back(id);
  }
  return relative_trading_ids;
}

} // namespace DataProcess