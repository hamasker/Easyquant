#include "data_process.h"
#include "data.h"
#include "nova_api_data_type.h"
#include "nova_api_exch.h"
#include "str_util.h"

namespace DataProcess {

// 通用交易所判断函数
bool is_main_exchange(const std::string &inst_str,
                      const std::string &aim_exchange) {
  return sum_util::EndsWith(inst_str, aim_exchange);
}

// 获取交易所的手续费
double get_exchange_taker_fee(const StrategyConfig &CFG_,
                              const std::string &exchange) {
  if (exchange == "krk")
    return CFG_.krk_taker_fee;
  else if (exchange == "gt")
    return CFG_.gt_taker_fee;
  else if (exchange == "bn")
    return CFG_.bn_taker_fee;
  else if (exchange == "ok")
    return CFG_.ok_taker_fee;
  else if (exchange == "cb")
    return CFG_.cb_taker_fee;
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

bool debug_verb = false;
static double alpha = 0.0005554;

using namespace sum_util;

bool fetch_data(const nova::quote::DataInfo &one,
                data::InstrumentData &InstData_, bool &flag_data_ready,
                const StrategyConfig &CFG_) {
  const auto &quote_type = one.quote_type();
  auto &id_map = InstData_.IM.inststr2id_;
  std::string inst_str;
  int64_t ts = 0;
  if (quote_type == NOVA_COIN_QUOTE_DEPTH) {
    const auto &data = *static_cast<const Depth *>(one.buffer().back());
    inst_str = data.instrument_id.symbol;
    ts = data.local_time;
    auto id = id_map.at(inst_str);
    auto &depth_map = InstData_.depth_map[id];
    if (CFG_.backtest ||
        static_cast<uint64_t>(depth_map.sequence_num) < data.sequence_num)
      extract_depth_data(data, depth_map, InstData_.delay_map);
    else
      return flag_data_ready;
  } else if (quote_type == NOVA_COIN_QUOTE_DEPTH_LVN) {
    const auto &data = *static_cast<const DepthLVN *>(one.buffer().back());
    inst_str = data.instrument_id.symbol;
    ts = data.local_time;
    auto id = id_map.at(inst_str);
    auto &depth_map = InstData_.depth_map[id];
    extract_depth_data(data, depth_map, InstData_.delay_map);
  } else if (quote_type == NOVA_COIN_QUOTE_BBO) {
    const auto &data = *static_cast<const BBO *>(one.buffer().back());
    inst_str = data.instrument_id.symbol;
    ts = data.local_time;
    auto id = id_map.at(inst_str);
    auto &bbo_map = InstData_.bbo_map[id];
    extract_bbo_data(data, bbo_map, InstData_.delay_map);
  } else if (quote_type == NOVA_COIN_QUOTE_BAR) {
    const auto &data = *static_cast<const Bar *>(one.buffer().back());
    inst_str = data.instrument_id.symbol;
    ts = data.local_time;
    auto id = id_map.at(inst_str);
    auto &bar_map = InstData_.bar_map[id];
    extract_bar_data(data, bar_map, InstData_.delay_map);
  } else if (quote_type == NOVA_COIN_QUOTE_TRADE) {
    const auto &data = *static_cast<const Trade *>(one.buffer().back());
    inst_str = data.instrument_id.symbol;
    ts = data.local_time;
    auto id = id_map.at(inst_str);
    InstData_.trade_map[id].add_trade(data.local_time, data.side, data.price,
                                      data.qty);
    if (!InstData_.depth_map.count(id) || !InstData_.bbo_map.count(id))
      return false;
    // 使用trade数据更新depth和bbo数据，提供更实时的价格信息
    auto &depth_map = InstData_.depth_map[id];
    auto &bbo_map = InstData_.bbo_map[id];
    extract_trade_data(data, depth_map, bbo_map, InstData_.delay_map);
  } else {
    throw std::invalid_argument("invalid quote type!");
  }
  auto &id = id_map.at(inst_str);
  if (quote_type == NOVA_COIN_QUOTE_DEPTH ||
      quote_type == NOVA_COIN_QUOTE_DEPTH_LVN)
    if (DataProcess::is_main_exchange(inst_str, CFG_.aim_exchange)) {
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
                    data::InstrumentData &InstData_,
                    const StrategyConfig &CFG_) {
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
  if (md.update_time < dd.server_ts) {
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
  if (md.update_time < dd.server_ts) {
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
  if (md.update_time < dd.server_ts) {
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
  if (md.update_time < dd.server_ts) {
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

std::vector<double> extract_depth_data_vec(const Depth *md,
                                           const data::depth_type &dtype,
                                           const int8_t &level) {
  bool flag_qty = false;
  const NovaCoinPriceLevel *extract_data = nullptr;
  if (dtype == data::depth_type::BID ||
      dtype == data::depth_type::BID_QUANTITY) {
    extract_data = md->bid;
  } else if (dtype == data::depth_type::ASK ||
             dtype == data::depth_type::ASK_QUANTITY) {
    extract_data = md->ask;
  } else {
    throw std::invalid_argument("invalid depth type!");
  }
  std::vector<double> data(level);
  for (int i = 0; i < level; i++) {
    data[i] = flag_qty ? extract_data[i].qty : extract_data[i].price;
  }
  return data;
}

std::vector<double> extract_depth_data_vec(const Depth *md, const int8_t &level,
                                           const data::depth_type &dtype) {
  const NovaCoinPriceLevel *extract_data = nullptr;
  extract_data = (dtype == data::depth_type::BID) ? md->bid : md->ask;
  std::vector<double> data(level * 2);
  for (int i = 0; i < level; i++) {
    data[i] = extract_data[i].price;
    data[level + i] = extract_data[i].qty;
  }
  return data;
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

double fetch_balance(const data::currency &currency,
                     const ryml::Tree &settings_) {
  auto currency_str = data::get_currency_name(currency);
  sum_util::StrUpper(currency_str);
  c4::csubstr currency_key(
      currency_str.c_str(),
      currency_str.size()); // 将 std::string 转换为 c4::csubstr
  auto balance = settings_[currency_key]["balance_quantity"].val();
  std::string balance_str(balance.str, balance.len);
  return std::stod(balance_str);
}

double fetch_credit(const data::currency &currency,
                    const ryml::Tree &settings_) {
  auto currency_str = data::get_currency_name(currency);
  sum_util::StrUpper(currency_str);
  c4::csubstr currency_key(
      currency_str.c_str(),
      currency_str.size()); // 将 std::string 转换为 c4::csubstr
  auto credit = settings_[currency_key]["credit"].val();
  std::string credit_str(credit.str, credit.len);
  return std::stod(credit_str);
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

double fetch_max_percentage(const data::currency &currency,
                            const ryml::Tree &settings_) {
  auto currency_str = data::get_currency_name(currency);
  sum_util::StrUpper(currency_str);
  c4::csubstr currency_key(
      currency_str.c_str(),
      currency_str.size()); // 将 std::string 转换为 c4::csubstr
  auto max_percentage = settings_[currency_key]["max_percentage"].val();
  std::string max_percentage_str(max_percentage.str, max_percentage.len);
  return std::stod(max_percentage_str);
}

double get_currency_usd_mp(const std::string currency_str,
                           const data::InstrumentData &InstData_) {
  const auto &inst_str = fmt::format("{}_usd.krk", currency_str);
  const auto &id = InstData_.IM.inststr2id_.at(inst_str);
  const auto &bid = InstData_.depth_map.at(id).bids[0][0];
  const auto &ask = InstData_.depth_map.at(id).asks[0][0];
  return (bid + ask) * 0.5;
}

double get_currency_hedge_usdt_mp(const std::string currency_str,
                                  const data::InstrumentData &InstData_) {
  const auto &inst_str = fmt::format("{}_usdt.krk", currency_str);
  const auto &id = InstData_.IM.inststr2id_.at(inst_str);
  const auto &bid = InstData_.bbo_map.at(id).bid[0];
  const auto &ask = InstData_.bbo_map.at(id).ask[0];
  return (bid + ask) * 0.5;
}

void update_pnl(
    data::pnl_data &pnl_data, const data::InstrumentData &InstData_,
    std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    std::unordered_map<data::currency, data::hedge_position_data>
        &hedge_positions_,
    const std::vector<data::currency> &digital_currencies) {
  double end_usd = 0;
  double end_usd_no_operation = 0;
  double hedge_pnl_static = 0;
  double hedge_pnl_dynamic = 0;
  for (const auto &one : BlcMng_) {
    const auto &currency = one.first;
    const auto &currency_str = data::get_currency_name(currency);
    const auto &balance = one.second.balance_live;
    const auto &balance_default = one.second.balance_default;
    double mid_price = 1, mid_price_hedge = 1;
    if (currency != data::currency::USD)
      mid_price = get_currency_usd_mp(currency_str, InstData_);
    if (!pnl_data.update) {
      pnl_data.begin_balances[currency] = balance;
      pnl_data.begin_prices[currency] = mid_price;
      pnl_data.begin_usd += balance * mid_price;
      if (sum_util::Find((digital_currencies), currency))
        pnl_data.begin_hedge_usdt +=
            balance * get_currency_hedge_usdt_mp(currency_str, InstData_);
    }
    pnl_data.end_balances[currency] = balance;
    pnl_data.end_prices[currency] = mid_price;
    pnl_data.end_usd += balance * mid_price;
    end_usd_no_operation +=
        pnl_data.begin_balances.at(currency) * pnl_data.end_prices.at(currency);
    hedge_pnl_static += pnl_data.begin_balances.at(currency) -
                        pnl_data.end_prices.at(currency) * balance_default;
    if (sum_util::Find((digital_currencies), currency)) {
      mid_price_hedge = get_currency_hedge_usdt_mp(currency_str, InstData_);
      auto &hedge_position = hedge_positions_.at(currency);
      if (hedge_position.average_price == 0)
        hedge_position.average_price = mid_price_hedge;
      else {
        const auto &unrealized_profit =
            (hedge_position.average_price - mid_price_hedge) *
            hedge_position.size;
        hedge_pnl_dynamic = hedge_position.realized_profit + unrealized_profit;
      }
    }
  }
  if (!pnl_data.update)
    pnl_data.update = true;
  pnl_data.end_usd = end_usd;
  pnl_data.pnl = pnl_data.end_usd - pnl_data.begin_usd;
  pnl_data.pnl_no_operation =
      end_usd_no_operation - pnl_data.begin_usd + pnl_data.hedge_pnl_static;
  pnl_data.hedge_pnl_static = hedge_pnl_static;
  pnl_data.hedge_pnl_dynamic = hedge_pnl_dynamic;
  pnl_data.pnl_static_hedge = pnl_data.pnl + pnl_data.hedge_pnl_static;
  pnl_data.pnl_dynamic_hedge = pnl_data.pnl + pnl_data.hedge_pnl_dynamic;
}

void update_hedge_pnl(
    const data::currency currency, const double &position, const double &price,
    std::unordered_map<data::currency, data::hedge_position_data>
        &hedge_positions_) {
  auto &hedge_position = hedge_positions_.at(currency);
  hedge_position.trade.price = price;
  const auto &old_size = hedge_position.size;
  const auto &new_size = position;
  if (new_size - old_size > 0) {
    const double &qty = new_size - old_size;
    hedge_position.trade.volume = qty;
    hedge_position.trade.s = data::side::SELL;
    hedge_position.average_price =
        (hedge_position.average_price * old_size + price * qty) / new_size;
    hedge_position.size = new_size;
  } else {
    const double &qty = old_size - new_size;
    hedge_position.trade.volume = qty;
    hedge_position.trade.s = data::side::BUY;
    const auto &realized_profit = (hedge_position.average_price - price) * qty;
    hedge_position.realized_profit += realized_profit;
    hedge_position.size -= qty;
  }
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
    const StrategyConfig &CFG_, const data::InstrumentData &InstData_,
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map_) {
  std::string op_ob = "";
  for (const auto &currency : CFG_.trading_currencies) {
    const auto &currency_str = data::get_currency_name(currency);
    if (currency != data::currency::USD) {
      op_ob += "1.0,";
      continue;
    }
    const auto &fp = fps_map_.at(currency).fps.get_last();
    op_ob += fmt::format("{},", fp);
  }
  for (const auto &forex : CFG_.available_forex) {
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
                         const StrategyConfig &CFG_) {
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
    if (DataProcess::is_main_exchange(inst_str, CFG_.aim_exchange)) {
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

void remove_exceed_limit_orders(std::vector<data::order_data> &place_bid_orders,
                                std::vector<data::order_data> &place_ask_orders,
                                const double &rate_limit,
                                const int &flag_balance_base) {
  const int &held_num = place_ask_orders.size() + place_bid_orders.size();
  int remove_num = 0;
  if (rate_limit <= 0)
    remove_num = held_num;
  else if (rate_limit < 64)
    remove_num =
        std::min(held_num, static_cast<int>(std::ceil(64 - rate_limit) / 10));
  else if (held_num * 10 - rate_limit > 0)
    remove_num = std::min(held_num, static_cast<int>(std::ceil(
                                        (held_num * 10 - rate_limit) / 10)));
  else
    throw std::runtime_error("rate_limit is too large");
  for (int i = 0; i < remove_num; i++) {
    if (place_bid_orders.size() > place_ask_orders.size())
      place_bid_orders.pop_back();
    else if (place_bid_orders.size() < place_ask_orders.size())
      place_ask_orders.pop_back();
    else if (flag_balance_base == 1 && place_bid_orders.size() != 0)
      place_bid_orders.pop_back();
    else if (flag_balance_base == 2 && place_ask_orders.size() != 0)
      place_ask_orders.pop_back();
    else if (place_bid_orders.back().margin > place_ask_orders.back().margin &&
             place_ask_orders.size() != 0)
      place_ask_orders.pop_back();
    else if (place_bid_orders.back().margin < place_ask_orders.back().margin &&
             place_bid_orders.size() != 0)
      place_bid_orders.pop_back();
    else {
      if (place_bid_orders.size() != 0)
        place_bid_orders.pop_back();
      else if (place_ask_orders.size() != 0)
        place_ask_orders.pop_back();
    }
  }
  if (place_bid_orders.size() > 10)
    place_bid_orders = {};
  if (place_ask_orders.size() > 10)
    place_ask_orders = {};
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

void update_orders_cost_sum(
    std::unordered_map<data::currency, data::fetch_orders_data>
        &fetch_orders_map_,
    const data::InstrumentManager &InstManager_,
    const std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    const StrategyConfig &CFG_) {
  calculate_cost_transfer_sum(fetch_orders_map_, InstManager_, BlcMng_, false);
  const auto &settings = CFG_.setting;
  for (auto &item : fetch_orders_map_) {
    const auto &currency = item.first;
    const auto &currency_str = data::get_currency_name(currency);
    auto &cost_sum = item.second.cost_sum;
    auto &transfer_sum = item.second.transfer_sum;
    const auto &balance = BlcMng_.at(currency).balance_live;
    const auto &balance_default = BlcMng_.at(currency).balance_default;
    const auto &multi = (sum_util::Find(CFG_.digital_currencies, currency))
                            ? 1 - CFG_.digital_position_thre
                            : 0.05;
    if (cost_sum > 0 && balance - cost_sum < balance_default * multi) {
      double transfer_factor =
          std::max(0.0, (balance - balance_default * multi) / cost_sum);
      auto &place_orders_dict = item.second.place_order_points_dict;
      for (auto &item2 : place_orders_dict) {
        const auto &id = item2.first;
        auto *IC = InstManager_.FindByUniId(id);
        const auto &quantity_size = IC->quantity_size;
        for (auto &order : item2.second) {
          order.quantity *= transfer_factor;
        }
        item2.second.erase(
            std::remove_if(item2.second.begin(), item2.second.end(),
                           [quantity_size](const data::order_data &order) {
                             return order.quantity <= quantity_size * 10;
                           }),
            item2.second.end());
      }
    }
    const auto &max_percentage = settings.at(currency).max_percentage + 1;
    if (transfer_sum > 0 &&
        balance + transfer_sum > balance_default * max_percentage) {
      double transfer_factor = std::max(
          0.0, (balance_default * max_percentage - balance) / transfer_sum);
      for (auto &item3 : fetch_orders_map_)
        if (item3.first != currency) {
          auto &place_orders_dict = item3.second.place_order_points_dict;
          for (auto &item4 : place_orders_dict) {
            const auto &id = item4.first;
            auto *IC = InstManager_.FindByUniId(id);
            const auto &quantity_size = IC->quantity_size;
            if (IC->base == currency || IC->quote == currency) {
              for (auto &order : item4.second) {
                double qty_tmp = order.quantity;
                if (CFG_.flexible_adjust) {
                  const auto &vp = (IC->fp_bid + IC->fp_ask) * 0.5;
                  const auto &limit_qty = CFG_.limit_usd / vp;
                  order.quantity =
                      (order.quantity * transfer_factor < limit_qty)
                          ? limit_qty
                          : order.quantity * transfer_factor;
                } else
                  order.quantity *= transfer_factor;
                INFO_FLOG(
                    "[UpdateCost]transfer {} to {} too much, qty_before: {}, "
                    "qty_after: {}",
                    (IC->base == currency) ? IC->quote_str : IC->base_str,
                    currency_str, qty_tmp, order.quantity);
              }
              item4.second.erase(
                  std::remove_if(
                      item4.second.begin(), item4.second.end(),
                      [quantity_size](const data::order_data &order) {
                        return order.quantity <= quantity_size * 10;
                      }),
                  item4.second.end());
            }
          }
        }
    }
  }
  calculate_cost_transfer_sum(fetch_orders_map_, InstManager_, BlcMng_, true);
  for (auto &one : fetch_orders_map_) {
    const auto &currency = one.first;
    auto &transfer_sum = one.second.transfer_sum;
    auto &cost_sum = one.second.cost_sum;
    auto &rest_sum = one.second.rest_sum;
    const auto &balance = BlcMng_.at(currency).balance_live;
    one.second.rest_sum = std::max(0.0, balance - cost_sum);
    const auto &max_percentage = settings.at(currency).max_percentage + 1;
    DEBUG_FLOG("[UpdateCost]currency: {}, balance: {},transfer_sum: {}, "
               "cost_sum: {}, rest_sum: {}, max_percentage: {}",
               data::get_currency_name(currency), balance, transfer_sum,
               cost_sum, rest_sum, max_percentage);
  }
}

void calculate_cost_transfer_sum(
    std::unordered_map<data::currency, data::fetch_orders_data>
        &fetch_orders_map_,
    const data::InstrumentManager &InstManager_,
    const std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    bool verb1) {
  for (auto &item : fetch_orders_map_) {
    item.second.cost_sum = 0;
    item.second.transfer_sum = 0;
  }
  for (auto &one : fetch_orders_map_) {
    const auto &currency = one.first;
    double &cost_sum = one.second.cost_sum;
    auto &place_orders_dict = one.second.place_order_points_dict;
    for (const auto &item : place_orders_dict) {
      const auto &id = item.first;
      const auto &place_orders = item.second;
      auto *IC = InstManager_.FindByUniId(id);
      const auto &inst_str = IC->inst_str;
      for (const auto &order : place_orders) {
        if (currency == IC->base) {
          cost_sum += order.quantity;
          fetch_orders_map_[IC->quote].transfer_sum +=
              order.quantity * order.price;
        } else if (currency == IC->quote) {
          cost_sum += order.quantity * order.price;
          fetch_orders_map_[IC->base].transfer_sum += order.quantity;
        }
        if (verb1) {
          DEBUG_FLOG("[UpdateCost]currency: {}, inst: {}, qty: {}, price: {}",
                     data::get_currency_name(currency), inst_str,
                     order.quantity, order.price);
        }
      }
    }
  }
  for (auto &one : fetch_orders_map_) {
    const auto &currency = one.first;
    one.second.rest_sum =
        std::max(0.0, BlcMng_.at(currency).balance_live - one.second.cost_sum);
  }
}

std::vector<std::string>
load_factor_pool(const std::string &factor_pool_filepath) {
  std::vector<std::string> pool;
  std::ifstream file(factor_pool_filepath);
  std::string factor;
  while (std::getline(file, factor)) {
    pool.push_back(factor);
  }
  file.close();
  return pool;
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

void update_cuscore(data::cuscore_data &cc, StrategyConfig &CFG_,
                    const double &fp_bid, const double &fp_ask, bool verb) {
  // ⚠️ NaN风险点0: 输入参数检查
  // - 如果fp_bid或fp_ask是NaN，后续所有计算都会传播NaN
  // - 如果alpha_fast/mid/slow是NaN或异常值，EMAStep会传播NaN
  if (verb) {
    INFO_FLOG("[update_cuscore] Input: fp_bid={}, fp_ask={}, cuscore_cnt={}, "
              "ema_bid_slow={}, ema_ask_slow={}, sigma_bid={}, sigma_ask={}",
              fp_bid, fp_ask, cc.cuscore_cnt, cc.ema_bid_slow, cc.ema_ask_slow,
              cc.sigma_bid, cc.sigma_ask);
  }
  auto &params = CFG_.cuscore_params;
  const auto &span_slow = params.span_slow;
  const auto &span_fast = params.span_fast;
  const auto &span_mid = params.span_mid;
  auto &alpha_fast = params.alpha_fast;
  auto &alpha_mid = params.alpha_mid;
  auto &alpha_slow = params.alpha_slow;
  if (verb) {
    INFO_FLOG("[update_cuscore] Params: alpha_fast={}, alpha_mid={}, "
              "alpha_slow={}, span_fast={}, span_mid={}, span_slow={}",
              alpha_fast, alpha_mid, alpha_slow, span_fast, span_mid,
              span_slow);
  }
  if (cc.cuscore_cnt <= span_fast)
    params.coefs_t_fast = sum_util::ExpDecayCoefficients(cc.cuscore_cnt);
  if (cc.cuscore_cnt <= span_slow)
    params.coefs_t_slow = sum_util::ExpDecayCoefficients(cc.cuscore_cnt);
  if (cc.cuscore_cnt <= span_mid)
    params.coefs_t_mid = sum_util::ExpDecayCoefficients(cc.cuscore_cnt);
  if (cc.ema_bid_slow < 0) {
    cc.ema_bid_slow = fp_bid;
    cc.ema_ask_slow = fp_ask;
    cc.ema_bid_fast = fp_bid;
    cc.ema_ask_fast = fp_ask;
    cc.ema_bid_mid = fp_bid;
    cc.ema_ask_mid = fp_ask;
  } else {
    // ⚠️ NaN风险点0.5: EMAStepInplace可能传播NaN
    // - 如果cc.ema_bid_slow等已经是NaN，EMAStepInplace会保持NaN
    // - 如果fp_bid/fp_ask是NaN，EMAStepInplace会传播NaN
    // - 如果alpha值异常，可能导致数值不稳定
    if (verb) {
      INFO_FLOG(
          "[update_cuscore] Before EMA: ema_bid_slow={}, ema_ask_slow={}, "
          "ema_bid_fast={}, ema_ask_fast={}, ema_bid_mid={}, ema_ask_mid={}",
          cc.ema_bid_slow, cc.ema_ask_slow, cc.ema_bid_fast, cc.ema_ask_fast,
          cc.ema_bid_mid, cc.ema_ask_mid);
    }
    EMAStepInplace(cc.ema_bid_slow, fp_bid, alpha_slow);
    EMAStepInplace(cc.ema_ask_slow, fp_ask, alpha_slow);
    EMAStepInplace(cc.ema_bid_fast, fp_bid, alpha_fast);
    EMAStepInplace(cc.ema_ask_fast, fp_ask, alpha_fast);
    EMAStepInplace(cc.ema_bid_mid, fp_bid, alpha_mid);
    EMAStepInplace(cc.ema_ask_mid, fp_ask, alpha_mid);
    if (verb) {
      INFO_FLOG(
          "[update_cuscore] After EMA: ema_bid_slow={}, ema_ask_slow={}, "
          "ema_bid_fast={}, ema_ask_fast={}, ema_bid_mid={}, ema_ask_mid={}",
          cc.ema_bid_slow, cc.ema_ask_slow, cc.ema_bid_fast, cc.ema_ask_fast,
          cc.ema_bid_mid, cc.ema_ask_mid);
    }
    // ⚠️ NaN风险点1: VSqrt可能产生NaN
    // -
    // 如果cc.sigma_bid已经是NaN，VSquare(NaN)=NaN，EMAStep会传播NaN，VSqrt(NaN)=NaN
    // - 如果fp_bid或cc.ema_bid_slow是NaN，fp_bid-cc.ema_bid_slow=NaN，会传播
    // -
    // 如果EMAStep的结果是负数（理论上不应该，但数值误差可能导致），VSqrt会产生NaN
    // - 如果alpha_slow导致数值不稳定，EMAStep结果可能异常
    double diff_bid = fp_bid - cc.ema_bid_slow;
    double diff_ask = fp_ask - cc.ema_ask_slow;
    double sigma_bid_sq_before = VSquare(cc.sigma_bid);
    double sigma_ask_sq_before = VSquare(cc.sigma_ask);
    double diff_bid_sq = VSquare(diff_bid);
    double diff_ask_sq = VSquare(diff_ask);
    double ema_sigma_bid_sq =
        EMAStep(sigma_bid_sq_before, diff_bid_sq, alpha_slow);
    double ema_sigma_ask_sq =
        EMAStep(sigma_ask_sq_before, diff_ask_sq, alpha_slow);
    if (verb) {
      INFO_FLOG("[update_cuscore] Sigma calc: diff_bid={}, diff_ask={}, "
                "sigma_bid_sq_before={}, sigma_ask_sq_before={}, "
                "diff_bid_sq={}, diff_ask_sq={}, ema_sigma_bid_sq={}, "
                "ema_sigma_ask_sq={}",
                diff_bid, diff_ask, sigma_bid_sq_before, sigma_ask_sq_before,
                diff_bid_sq, diff_ask_sq, ema_sigma_bid_sq, ema_sigma_ask_sq);
    }
    cc.sigma_bid = VSqrt(ema_sigma_bid_sq);
    cc.sigma_ask = VSqrt(ema_sigma_ask_sq);
    if (verb) {
      INFO_FLOG("[update_cuscore] After VSqrt: sigma_bid={}, sigma_ask={}",
                cc.sigma_bid, cc.sigma_ask);
    }
  }
  // ⚠️ NaN风险点2: 除法操作可能产生NaN或Inf
  // - 如果cc.sigma_bid为0（虽然初始值是1e-9，但理论上可能变成0），会产生Inf
  // - 如果cc.sigma_bid为NaN，会产生NaN
  // - 如果fp_bid、fp_ask、cc.ema_bid_slow等是NaN，会产生NaN
  // - 如果cc.sigma_bid非常小（接近0），可能导致数值不稳定
  double diff_bid_slow = (fp_bid - cc.ema_bid_slow) / cc.sigma_bid;
  double diff_ask_slow = (fp_ask - cc.ema_ask_slow) / cc.sigma_ask;
  double diff_bid_fast = (fp_bid - cc.ema_bid_fast) / cc.sigma_bid;
  double diff_ask_fast = (fp_ask - cc.ema_ask_fast) / cc.sigma_ask;
  double diff_bid_mid = (fp_bid - cc.ema_bid_mid) / cc.sigma_bid;
  double diff_ask_mid = (fp_ask - cc.ema_ask_mid) / cc.sigma_ask;
  if (verb) {
    INFO_FLOG(
        "[update_cuscore] Divisions: diff_bid_slow={}, diff_ask_slow={}, "
        "diff_bid_fast={}, diff_ask_fast={}, diff_bid_mid={}, diff_ask_mid={}, "
        "sigma_bid={}, sigma_ask={}",
        diff_bid_slow, diff_ask_slow, diff_bid_fast, diff_ask_fast,
        diff_bid_mid, diff_ask_mid, cc.sigma_bid, cc.sigma_ask);
  }
  cc.diffs_bid_slow.add(diff_bid_slow);
  cc.diffs_ask_slow.add(diff_ask_slow);
  cc.diffs_bid_fast.add(diff_bid_fast);
  cc.diffs_ask_fast.add(diff_ask_fast);
  cc.diffs_bid_mid.add(diff_bid_mid);
  cc.diffs_ask_mid.add(diff_ask_mid);
  if (cc.cuscore_cnt > span_slow && CFG_.cuscore_threshold_slow < 10) {
    double cuscore_bid_slow = 0, cuscore_ask_slow = 0;
    auto diffs_bid_slow_iter =
        cc.diffs_bid_slow.get_column_accessor<0>(span_slow);
    auto diffs_ask_slow_iter =
        cc.diffs_ask_slow.get_column_accessor<0>(span_slow);
    // ⚠️ NaN风险点3: cuscore累加可能产生NaN
    // -
    // 如果*diffs_bid_slow_iter或*diffs_ask_slow_iter是NaN（来自上面的除法），累加结果会是NaN
    // - 如果params.coefs_t_slow[i]是NaN（理论上不应该），也会传播NaN
    // - NaN + 任何数 = NaN，所以一旦有NaN就会传播
    for (auto i = 0; i < span_slow; i++) {
      double diff_bid_val = *diffs_bid_slow_iter;
      double diff_ask_val = *diffs_ask_slow_iter;
      double coef = params.coefs_t_slow[i];
      double term_bid = diff_bid_val * coef;
      double term_ask = diff_ask_val * coef;
      if (verb && (i < 5 || i == span_slow - 1)) {
        INFO_FLOG(
            "[update_cuscore] Slow cuscore[{}]: diff_bid={}, diff_ask={}, "
            "coef={}, term_bid={}, term_ask={}, cuscore_bid_before={}, "
            "cuscore_ask_before={}",
            i, diff_bid_val, diff_ask_val, coef, term_bid, term_ask,
            cuscore_bid_slow, cuscore_ask_slow);
      }
      cuscore_bid_slow += term_bid;
      cuscore_ask_slow += term_ask;
      ++diffs_bid_slow_iter;
      ++diffs_ask_slow_iter;
    }
    if (verb) {
      INFO_FLOG("[update_cuscore] Slow cuscore result: cuscore_bid_slow={}, "
                "cuscore_ask_slow={}",
                cuscore_bid_slow, cuscore_ask_slow);
    }
    cc.cuscore_bid_slow = cuscore_bid_slow;
    cc.cuscore_ask_slow = cuscore_ask_slow;
  } else {
    cc.cuscore_bid_slow = 0.0;
    cc.cuscore_ask_slow = 0.0;
  }
  if (cc.cuscore_cnt > span_mid && CFG_.cuscore_threshold_mid < 10) {
    double cuscore_bid_mid = 0, cuscore_ask_mid = 0;
    auto diffs_bid_mid_iter = cc.diffs_bid_mid.get_column_accessor<0>(span_mid);
    auto diffs_ask_mid_iter = cc.diffs_ask_mid.get_column_accessor<0>(span_mid);
    // ⚠️ NaN风险点4: cuscore_mid累加可能产生NaN（同上）
    // 检查buffer中是否有异常值
    if (verb) {
      double max_diff_bid = -1e300, max_diff_ask = -1e300;
      double min_diff_bid = 1e300, min_diff_ask = 1e300;
      int nan_count_bid = 0, nan_count_ask = 0;
      int inf_count_bid = 0, inf_count_ask = 0;
      int large_count_bid = 0, large_count_ask = 0; // |diff| > 1000
      auto check_iter_bid = cc.diffs_bid_mid.get_column_accessor<0>(span_mid);
      auto check_iter_ask = cc.diffs_ask_mid.get_column_accessor<0>(span_mid);
      for (auto i = 0; i < span_mid; i++) {
        double val_bid = *check_iter_bid;
        double val_ask = *check_iter_ask;
        if (std::isnan(val_bid))
          nan_count_bid++;
        if (std::isnan(val_ask))
          nan_count_ask++;
        if (std::isinf(val_bid))
          inf_count_bid++;
        if (std::isinf(val_ask))
          inf_count_ask++;
        if (std::abs(val_bid) > 1000)
          large_count_bid++;
        if (std::abs(val_ask) > 1000)
          large_count_ask++;
        if (val_bid > max_diff_bid)
          max_diff_bid = val_bid;
        if (val_bid < min_diff_bid)
          min_diff_bid = val_bid;
        if (val_ask > max_diff_ask)
          max_diff_ask = val_ask;
        if (val_ask < min_diff_ask)
          min_diff_ask = val_ask;
        ++check_iter_bid;
        ++check_iter_ask;
      }
      INFO_FLOG("[update_cuscore] Mid diffs stats: max_bid={}, min_bid={}, "
                "max_ask={}, min_ask={}, nan_bid={}, nan_ask={}, inf_bid={}, "
                "inf_ask={}, large_bid={}, large_ask={}",
                max_diff_bid, min_diff_bid, max_diff_ask, min_diff_ask,
                nan_count_bid, nan_count_ask, inf_count_bid, inf_count_ask,
                large_count_bid, large_count_ask);
    }
    for (auto i = 0; i < span_mid; i++) {
      double diff_bid_val = *diffs_bid_mid_iter;
      double diff_ask_val = *diffs_ask_mid_iter;
      double coef = params.coefs_t_mid[i];
      double term_bid = diff_bid_val * coef;
      double term_ask = diff_ask_val * coef;
      // 检测异常值
      if (verb &&
          (i < 5 || i == span_mid - 1 || std::abs(diff_bid_val) > 1000 ||
           std::abs(diff_ask_val) > 1000 || std::isnan(diff_bid_val) ||
           std::isnan(diff_ask_val) || std::isinf(diff_bid_val) ||
           std::isinf(diff_ask_val))) {
        INFO_FLOG("[update_cuscore] Mid cuscore[{}]: diff_bid={}, diff_ask={}, "
                  "coef={}, term_bid={}, term_ask={}, cuscore_bid_before={}, "
                  "cuscore_ask_before={}",
                  i, diff_bid_val, diff_ask_val, coef, term_bid, term_ask,
                  cuscore_bid_mid, cuscore_ask_mid);
      }
      cuscore_bid_mid += term_bid;
      cuscore_ask_mid += term_ask;
      ++diffs_bid_mid_iter;
      ++diffs_ask_mid_iter;
    }
    if (verb) {
      INFO_FLOG("[update_cuscore] Mid cuscore result: cuscore_bid_mid={}, "
                "cuscore_ask_mid={}",
                cuscore_bid_mid, cuscore_ask_mid);
    }
    cc.cuscore_bid_mid = cuscore_bid_mid;
    cc.cuscore_ask_mid = cuscore_ask_mid;
  } else {
    cc.cuscore_bid_mid = 0.0;
    cc.cuscore_ask_mid = 0.0;
  }
  if (cc.cuscore_cnt > span_fast && CFG_.cuscore_threshold_fast < 10) {
    double cuscore_bid_fast = 0, cuscore_ask_fast = 0;
    auto diffs_bid_fast_iter =
        cc.diffs_bid_fast.get_column_accessor<0>(span_fast);
    auto diffs_ask_fast_iter =
        cc.diffs_ask_fast.get_column_accessor<0>(span_fast);
    // ⚠️ NaN风险点5: cuscore_fast累加可能产生NaN（同上）
    for (auto i = 0; i < span_fast; i++) {
      double diff_bid_val = *diffs_bid_fast_iter;
      double diff_ask_val = *diffs_ask_fast_iter;
      double coef = params.coefs_t_fast[i];
      double term_bid = diff_bid_val * coef;
      double term_ask = diff_ask_val * coef;
      if (verb && (i < 5 || i == span_fast - 1)) {
        INFO_FLOG(
            "[update_cuscore] Fast cuscore[{}]: diff_bid={}, diff_ask={}, "
            "coef={}, term_bid={}, term_ask={}, cuscore_bid_before={}, "
            "cuscore_ask_before={}",
            i, diff_bid_val, diff_ask_val, coef, term_bid, term_ask,
            cuscore_bid_fast, cuscore_ask_fast);
      }
      cuscore_bid_fast += term_bid;
      cuscore_ask_fast += term_ask;
      ++diffs_bid_fast_iter;
      ++diffs_ask_fast_iter;
    }
    if (verb) {
      INFO_FLOG("[update_cuscore] Fast cuscore result: cuscore_bid_fast={}, "
                "cuscore_ask_fast={}",
                cuscore_bid_fast, cuscore_ask_fast);
    }
    cc.cuscore_bid_fast = cuscore_bid_fast;
    cc.cuscore_ask_fast = cuscore_ask_fast;
  } else {
    cc.cuscore_bid_fast = 0.0;
    cc.cuscore_ask_fast = 0.0;
  }
  if (cc.cuscore_cnt < 5 * CFG_.cuscore_span_slow)
    cc.cuscore_cnt++;
  // double thre = CFG_.cuscore_threshold_fast;
  double thre_fast = 0.0, thre_mid = 0.0, thre_slow = 0.0;
  // cuscore触发趋势后 判断是否已回复
  if (cc.cuscore_bid_flag_fast &&
      (cc.cuscore_bid_fast < CFG_.cuscore_threshold_fast - thre_fast))
    cc.cuscore_bid_flag_fast = false;
  if (cc.cuscore_bid_flag_mid &&
      (cc.cuscore_bid_mid < CFG_.cuscore_threshold_mid - thre_mid))
    cc.cuscore_bid_flag_mid = false;
  if (cc.cuscore_bid_flag_slow &&
      (cc.cuscore_bid_slow < CFG_.cuscore_threshold_slow - thre_slow))
    cc.cuscore_bid_flag_slow = false;
  if (cc.cuscore_ask_flag_fast &&
      (cc.cuscore_ask_fast > -CFG_.cuscore_threshold_fast + thre_fast))
    cc.cuscore_ask_flag_fast = false;
  if (cc.cuscore_ask_flag_mid &&
      (cc.cuscore_ask_mid > -CFG_.cuscore_threshold_mid + thre_mid))
    cc.cuscore_ask_flag_mid = false;
  if (cc.cuscore_ask_flag_slow &&
      (cc.cuscore_ask_slow > -CFG_.cuscore_threshold_slow + thre_slow))
    cc.cuscore_ask_flag_slow = false;
  if (!cc.cuscore_bid_flag_fast && !cc.cuscore_bid_flag_slow &&
      !cc.cuscore_bid_flag_mid)
    cc.cuscore_bid_flag = false;
  if (!cc.cuscore_ask_flag_fast && !cc.cuscore_ask_flag_slow &&
      !cc.cuscore_ask_flag_mid)
    cc.cuscore_ask_flag = false;
  // cuscore触发趋势
  if (cc.cuscore_bid_fast > CFG_.cuscore_threshold_fast)
    cc.cuscore_bid_flag_fast = true;
  if (cc.cuscore_bid_mid > CFG_.cuscore_threshold_mid)
    cc.cuscore_bid_flag_mid = true;
  if (cc.cuscore_bid_slow > CFG_.cuscore_threshold_slow)
    cc.cuscore_bid_flag_slow = true;
  if (cc.cuscore_ask_fast < -CFG_.cuscore_threshold_fast)
    cc.cuscore_ask_flag_fast = true;
  if (cc.cuscore_ask_mid < -CFG_.cuscore_threshold_mid)
    cc.cuscore_ask_flag_mid = true;
  if (cc.cuscore_ask_slow < -CFG_.cuscore_threshold_slow)
    cc.cuscore_ask_flag_slow = true;
  if (cc.cuscore_bid_flag_slow || cc.cuscore_bid_flag_fast ||
      cc.cuscore_bid_flag_mid)
    cc.cuscore_bid_flag = true;
  if (cc.cuscore_ask_flag_slow || cc.cuscore_ask_flag_fast ||
      cc.cuscore_ask_flag_mid)
    cc.cuscore_ask_flag = true;
  if (verb) {
    INFO_FLOG("[update_cuscore] Final results: cuscore_bid_slow={}, "
              "cuscore_ask_slow={}, cuscore_bid_mid={}, cuscore_ask_mid={}, "
              "cuscore_bid_fast={}, cuscore_ask_fast={}, sigma_bid={}, "
              "sigma_ask={}, cuscore_cnt={}",
              cc.cuscore_bid_slow, cc.cuscore_ask_slow, cc.cuscore_bid_mid,
              cc.cuscore_ask_mid, cc.cuscore_bid_fast, cc.cuscore_ask_fast,
              cc.sigma_bid, cc.sigma_ask, cc.cuscore_cnt);
  }
}

void update_ob_volume(
    data::InstrumentData &InstData_,
    std::unordered_map<data::currency, data::fair_price_data> &fps_map_) {
  // 在计算wp时使用 需要统一转换为USD计价
  for (auto &one : InstData_.depth_map) {
    const auto &id = one.first;
    auto *IC = InstData_.IM.FindByUniId(id);
    auto &dd = one.second;
    double bidv = dd.bids[0][0] * dd.bids[0][1];
    double askv = dd.asks[0][0] * dd.asks[0][1];
    if (IC->quote != data::currency::USD) {
      bidv *= fps_map_[IC->quote].fps.get_latest()[1];
      askv *= fps_map_[IC->quote].fps.get_latest()[0];
    }
    dd.ema_cnt++;
    dd.ema_bidv = (dd.ema_cnt == 1) ? bidv : EMAStep(dd.ema_bidv, bidv, alpha);
    dd.ema_askv = (dd.ema_cnt == 1) ? askv : EMAStep(dd.ema_askv, askv, alpha);
  }
  for (auto &one : InstData_.bbo_map) {
    const auto &id = one.first;
    auto *IC = InstData_.IM.FindByUniId(id);
    auto &dd = one.second;
    double bidv = dd.bid[1] * dd.bid[0];
    double askv = dd.ask[1] * dd.ask[0];
    if (IC->quote != data::currency::USD) {
      bidv *= fps_map_[IC->quote].fps.get_latest()[1];
      askv *= fps_map_[IC->quote].fps.get_latest()[0];
    }
    dd.ema_cnt++;
    dd.ema_bidv = (dd.ema_cnt == 1) ? bidv : EMAStep(dd.ema_bidv, bidv, alpha);
    dd.ema_askv = (dd.ema_cnt == 1) ? askv : EMAStep(dd.ema_askv, askv, alpha);
  }
}

std::array<double, 2> fp_forex_IB(const double &fp_bid, const double &fp_ask,
                                  const bool &flag_ib_, const double &ts_tmp,
                                  const std::string &currency_str,
                                  data::bbo_data &ib_data) {
  if (flag_ib_) {
    if ((ts_tmp - ib_data.local_ts > 10 * 60 * 1e9) || (!ib_data.valid)) {

      ib_data.premium_bid = -1;
      ib_data.premium_ask = -1;
      ib_data.premium_cnt = 0;
      ib_data.valid = false;
      return {fp_bid, fp_ask};
    } else {
      const auto &ib_param = ib_data.ib_param;
      const auto &premium_multi = ib_param.premium_multi;
      const auto &thre_premium = ib_param.thre_premium;
      const auto &beta = ib_param.get_beta(currency_str);
      const auto &intercept = ib_param.get_intercept(currency_str);
      double premium_bid, premium_ask;
      if (ib_data.bid[0] == -1) {
        premium_bid = ib_data.premium_bid;
        premium_ask = ib_data.premium_ask;
      } else {
        premium_bid = log(fp_bid) - log(ib_data.bid[0]);
        premium_ask = log(fp_ask) - log(ib_data.ask[0]);
      }
      ib_data.premium_ts = ts_tmp;
      ib_data.premium_bid =
          (ib_data.premium_bid == -1)
              ? premium_bid
              : EMAStep(ib_data.premium_bid, premium_bid, premium_multi);
      ib_data.premium_ask =
          (ib_data.premium_ask == -1)
              ? premium_ask
              : EMAStep(ib_data.premium_ask, premium_ask, premium_multi);
      ib_data.premium_cnt++;
      if (ib_data.premium_cnt > thre_premium) {
        const auto &premium_bid_adj = ib_param.clamp_premium(
            currency_str, ib_data.premium_bid * beta + intercept);
        const auto &premium_ask_adj = ib_param.clamp_premium(
            currency_str, ib_data.premium_ask * beta + intercept);
        const auto &fp_tmp_bid = ib_data.bid[0] * (1 + premium_bid_adj);
        const auto &fp_tmp_ask = ib_data.ask[0] * (1 + premium_ask_adj);
        return {std::min(fp_tmp_bid, fp_tmp_ask),
                std::max(fp_tmp_bid, fp_tmp_ask)};
      } else {
        if (ib_data.premium_cnt % 100 == 0)
          INFO_FLOG("waiting for {} ib data engouh! {}", currency_str,
                    ib_data.premium_cnt);
        return {fp_bid, fp_ask};
      }
    }
  } else {
    return {fp_bid, fp_ask};
  }
}

bool extract_fps(StrategyConfig &CFG_, data::InstrumentData &InstData_,
                 int64_t &global_ts, const std::string &fp_file_path) {
  const auto &available_forex = CFG_.available_forex;
  if (!sum_util::FileExists(fp_file_path)) {
    INFO_FLOG("fp_file_path: {} not found", fp_file_path.c_str());
    return false;
  }
  rapidcsv::Document doc(fp_file_path);

  // 调试：检查CSV文件的列名
  auto column_names = doc.GetColumnNames();
  std::string columns_str;
  for (size_t i = 0; i < column_names.size(); ++i) {
    if (i > 0)
      columns_str += ", ";
    columns_str += column_names[i];
  }
  INFO_FLOG("CSV file columns: {}", columns_str);
  INFO_FLOG("CSV file row count: {}", doc.GetRowCount());

  // 检查timestamp列是否存在
  bool has_timestamp = false;
  for (const auto &col_name : column_names) {
    if (col_name == "timestamp") {
      has_timestamp = true;
      break;
    }
  }
  if (!has_timestamp) {
    ERROR_FLOG(
        "CSV file does not contain 'timestamp' column. Available columns: {}",
        columns_str);
    return false;
  }

  // 安全读取 timestamp 列
  std::vector<double> timestamps;
  try {
    timestamps = doc.GetColumn<double>("timestamp");
  } catch (const std::exception &e) {
    ERROR_FLOG("Failed to read timestamp column: {}", e.what());
    return false;
  }

  INFO_FLOG("Timestamp column size: {}", timestamps.size());

  if (timestamps.empty()) {
    ERROR_FLOG("Timestamp column is empty in CSV file: {}", fp_file_path);
    return false;
  }

  if (global_ts - timestamps.back() > 10 * 60 * 1e9) {
    INFO_FLOG("global_ts: {} is too old", global_ts);
    return false;
  }

  // 安全读取所有货币的 fair price 列，跳过缺失或不完整的列
  std::unordered_map<data::currency, std::vector<double>> fps;
  for (const auto &currency : CFG_.trading_currencies) {
    const std::string &currency_name = data::get_currency_name(currency);

    // 检查列是否存在
    int column_idx = doc.GetColumnIdx(currency_name);
    if (column_idx < 0) {
      INFO_FLOG("Column '{}' not found in CSV file, skipping", currency_name);
      continue;
    }

    // 尝试读取列，捕获可能的行数据不一致异常
    try {
      std::vector<double> fp = doc.GetColumn<double>(currency_name);
      fps[currency] = std::move(fp);
    } catch (const std::out_of_range &e) {
      ERROR_FLOG("Failed to read column '{}': {}. This may indicate "
                 "inconsistent row lengths in CSV file. Skipping this column.",
                 currency_name, e.what());
      continue;
    } catch (const std::exception &e) {
      ERROR_FLOG("Error reading column '{}': {}. Skipping this column.",
                 currency_name, e.what());
      continue;
    }
  }

  // 检查是否有足够的有效数据
  if (fps.empty()) {
    ERROR_FLOG("No valid fair price columns found in CSV file: {}",
               fp_file_path);
    return false;
  }
  for (auto &id : InstData_.trading_ids) {
    auto *IC = InstData_.IM.FindByUniId(id);
    auto &vd = InstData_.vol_map[id];
    auto &sd = vd.slope;
    auto &cc = vd.cuscore;
    const auto &base = IC->base;
    const auto &quote = IC->quote;
    const auto &base_str = IC->base_str;

    // 检查所需的货币数据是否存在
    if (fps.find(base) == fps.end() || fps.find(quote) == fps.end()) {
      INFO_FLOG("Missing fair price data for base={} or quote={}, skipping "
                "instrument {}",
                base_str, data::get_currency_name(quote), id);
      continue;
    }

    const auto &fps_base = fps[base];
    const auto &fps_quote = fps[quote];

    // 确保两个向量长度一致
    if (fps_base.size() != fps_quote.size()) {
      ERROR_FLOG("Mismatched fair price vector sizes: base={} (size={}), "
                 "quote={} (size={}), skipping instrument {}",
                 base_str, fps_base.size(), data::get_currency_name(quote),
                 fps_quote.size(), id);
      continue;
    }

    const auto &fps_tmp = sum_util::Divide(fps_base, fps_quote);
    auto start_it = (fps_tmp.size() > sd.fps_begin.get_max_length())
                        ? fps_tmp.end() - sd.fps_begin.get_max_length()
                        : fps_tmp.begin();
    for (auto it = start_it; it != fps_tmp.end(); ++it) {
      double value = *it;
      sd.fps_begin.add({value, value});
      sd.fps_fast_begin.add({value, value});
      if (sum_util::Find(CFG_.digital_currencies, base))
        DataProcess::update_cuscore(cc, CFG_, value, value);
    }
    if (quote == data::currency::USD &&
        sum_util::Find(available_forex, base_str)) {
      const auto &ib_inst = fmt::format("{}_usd_cash.idealpro", base_str);

      // 检查 ib_inst 是否存在
      if (InstData_.IM.inststr2id_.find(ib_inst) ==
          InstData_.IM.inststr2id_.end()) {
        INFO_FLOG("IB instrument '{}' not found, skipping", ib_inst);
        continue;
      }

      const auto &id_ib_inst = InstData_.IM.inststr2id_[ib_inst];

      // 检查 bbo_map 中是否存在该 instrument
      if (InstData_.bbo_map.find(id_ib_inst) == InstData_.bbo_map.end()) {
        INFO_FLOG("BBO data for instrument '{}' not found, skipping", ib_inst);
        continue;
      }

      auto &ib_data = InstData_.bbo_map[id_ib_inst];
      const auto &ib_param = ib_data.ib_param;
      const auto &thre_premium = ib_param.thre_premium;
      const auto &beta = ib_param.get_beta(base_str);
      const auto &intercept = ib_param.get_intercept(base_str);

      // 安全读取 IB fair price 列
      const std::string ib_col_name = fmt::format("ib_{}", base_str);
      int ib_column_idx = doc.GetColumnIdx(ib_col_name);
      if (ib_column_idx < 0) {
        INFO_FLOG("IB column '{}' not found in CSV file, skipping",
                  ib_col_name);
        continue;
      }

      try {
        std::vector<double> fp_ib = doc.GetColumn<double>(ib_col_name);
        if (!fp_ib.empty() && fp_ib.back() != -1 && !fps_tmp.empty()) {
          const auto &premium_bid_adj = fps_tmp.back() / fp_ib.back() - 1;
          ib_data.premium_bid = (premium_bid_adj - intercept) / beta;
          ib_data.premium_ask = (premium_bid_adj - intercept) / beta;
          ib_data.premium_cnt = thre_premium;
          ib_data.local_ts = static_cast<int64_t>(timestamps.back());
        }
      } catch (const std::out_of_range &e) {
        ERROR_FLOG("Failed to read IB column '{}': {}. This may indicate "
                   "inconsistent row lengths in CSV file. Skipping.",
                   ib_col_name, e.what());
        continue;
      } catch (const std::exception &e) {
        ERROR_FLOG("Error reading IB column '{}': {}. Skipping.", ib_col_name,
                   e.what());
        continue;
      }
    }
  }
  return true;
}

void dump_fp(StrategyConfig &CFG_, std::ofstream &file_cache_,
             const std::string &inputs, const std::string &date) {
  auto &file_cache_path = CFG_.Server.Log.fp_path;
  const auto &trading_currencies = CFG_.trading_currencies;
  const auto &available_forex = CFG_.available_forex;
  std::filesystem::path dir_path =
      std::filesystem::path(file_cache_path).parent_path();
  if (!dir_path.empty() && !std::filesystem::exists(dir_path)) {
    std::filesystem::create_directories(dir_path);
  }
  if (file_cache_path.find("{date}") != std::string::npos) {
    sum_util::StrReplace(file_cache_path, "{date}", date.c_str());
  } else if (file_cache_path.find(date) != std::string::npos) {
  } else {
    if (file_cache_.is_open())
      file_cache_.close();
    std::string prev_date = sum_util::GetPreviousDate(date);
    sum_util::StrReplace(file_cache_path, prev_date.c_str(), date.c_str());
  }
  if (!std::filesystem::exists(file_cache_path)) {
    std::string header = "timestamp,";
    for (auto &currency : trading_currencies) {
      header += fmt::format("{},", data::get_currency_name(currency));
    }
    for (auto &forex : available_forex) {
      header += fmt::format("ib_{},", forex);
    }
    header.pop_back();
    file_cache_.open(file_cache_path, std::ios::app);
    file_cache_ << header << std::endl;
  } else if (!file_cache_.is_open())
    file_cache_.open(file_cache_path, std::ios::app);
  if (!file_cache_.is_open()) {
    INFO_FLOG("Failed to open file: {}", file_cache_path.c_str());
    exit(1);
  }
  if (inputs == "")
    return;
  else
    file_cache_ << inputs << std::endl;
}

int judge_calculate_params(const StrategyConfig &CFG_, int64_t &global_ts,
                           data::InstrumentData &InstData_,
                           const std::string &dist_symbol) {
  const auto &root_path = CFG_.root_path;
  const auto &digital_currencies = CFG_.digital_currencies;
  const auto &available_forex = CFG_.available_forex;

  const auto &json_path =
      root_path +
      fmt::format("config/cal_params/cal_params_{}.json", dist_symbol);
  nlohmann::json j;
  if (!std::filesystem::exists(json_path)) {
    INFO_FLOG("json_path: {} not found", json_path.c_str());
    j["timestamp"] = -1;
    j["flag_save"] = false;
    auto parentDir = std::filesystem::path(json_path).parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir))
      std::filesystem::create_directories(parentDir);
    std::ofstream out(json_path);
    out << j.dump(4);
    out.close();
    return 0;
  } else {
    INFO_FLOG("json_path: {} found", json_path.c_str());
    std::ifstream in(json_path);
    in >> j;
    int64_t ts = j["timestamp"];
    bool flag = j["flag_save"];
    if (flag && (global_ts - ts < 10 * 60 * 1e9)) {
      INFO_FLOG("global_ts: {} is too old", global_ts);
      const auto &js_cuscore = j["params"]["cuscore"];
      const auto &js_ib = j["params"]["ib"];
      const auto &js_vol = j["params"]["vol"];
      for (auto &id : InstData_.trading_ids) {
        auto *IC = InstData_.IM.FindByUniId(id);
        const auto &inst_str = IC->inst_str;
        const auto &base = IC->base;
        const auto &quote = IC->quote;
        const auto &base_str = IC->base_str;
        auto &vd = InstData_.vol_map[id];
        auto &cc = vd.cuscore;
        if (!js_vol.contains(inst_str))
          continue;
        const auto &j_vol = js_vol[inst_str];
        if (js_cuscore.contains(inst_str)) {
          const auto &j_cuscore = js_cuscore.at(inst_str);
          if (sum_util::Find(digital_currencies, base)) {
            cc.ema_bid_slow = j_cuscore["ema_bid_slow"];
            cc.ema_ask_slow = j_cuscore["ema_ask_slow"];
            cc.sigma_bid = j_cuscore["sigma_bid"];
            cc.sigma_ask = j_cuscore["sigma_ask"];
            cc.ema_bid_mid = j_cuscore["ema_bid_mid"];
            cc.ema_ask_mid = j_cuscore["ema_ask_mid"];
            cc.ema_bid_fast = j_cuscore["ema_bid_fast"];
            cc.ema_ask_fast = j_cuscore["ema_ask_fast"];
            std::vector<double> diffs_bid_slow = j_cuscore["diffs_bid_slow"];
            std::vector<double> diffs_ask_slow = j_cuscore["diffs_ask_slow"];
            std::vector<double> diffs_bid_mid = j_cuscore["diffs_bid_mid"];
            std::vector<double> diffs_ask_mid = j_cuscore["diffs_ask_mid"];
            std::vector<double> diffs_bid_fast = j_cuscore["diffs_bid_fast"];
            std::vector<double> diffs_ask_fast = j_cuscore["diffs_ask_fast"];
            for (auto value = diffs_bid_slow.rbegin();
                 value != diffs_bid_slow.rend(); ++value)
              cc.diffs_bid_slow.add(*value);
            for (auto value = diffs_ask_slow.rbegin();
                 value != diffs_ask_slow.rend(); ++value)
              cc.diffs_ask_slow.add(*value);
            for (auto value = diffs_bid_mid.rbegin();
                 value != diffs_bid_mid.rend(); ++value)
              cc.diffs_bid_mid.add(*value);
            for (auto value = diffs_ask_mid.rbegin();
                 value != diffs_ask_mid.rend(); ++value)
              cc.diffs_ask_mid.add(*value);
            for (auto value = diffs_bid_fast.rbegin();
                 value != diffs_bid_fast.rend(); ++value)
              cc.diffs_bid_fast.add(*value);
            for (auto value = diffs_ask_fast.rbegin();
                 value != diffs_ask_fast.rend(); ++value)
              cc.diffs_ask_fast.add(*value);
            cc.cuscore_bid_slow = j_cuscore["cuscore_bid_slow"];
            cc.cuscore_ask_slow = j_cuscore["cuscore_ask_slow"];
            cc.cuscore_bid_mid = j_cuscore["cuscore_bid_mid"];
            cc.cuscore_ask_mid = j_cuscore["cuscore_ask_mid"];
            cc.cuscore_bid_fast = j_cuscore["cuscore_bid_fast"];
            cc.cuscore_ask_fast = j_cuscore["cuscore_ask_fast"];
            cc.cuscore_cnt = j_cuscore["cuscore_cnt"];
          }
        }
        if (js_ib.contains(inst_str)) {
          const auto &j_ib = js_ib.at(inst_str);
          if (quote == data::currency::USD &&
              sum_util::Find(available_forex, base_str)) {
            const auto &ib_inst = fmt::format("{}_usd_cash.idealpro", base_str);
            const auto &id_ib_inst = InstData_.IM.inststr2id_.at(ib_inst);
            auto &ib_data = InstData_.bbo_map.at(id_ib_inst);
            ib_data.premium_bid = j_ib["premium_bid"];
            ib_data.premium_ask = j_ib["premium_ask"];
            ib_data.premium_cnt = j_ib["premium_cnt"];
            ib_data.local_ts = ts;
          }
        }
        vd.ema_bid_fast = j_vol["ema_bid_fast"];
        vd.ema_ask_fast = j_vol["ema_ask_fast"];
        vd.ema_bid_slow = j_vol["ema_bid_slow"];
        vd.ema_ask_slow = j_vol["ema_ask_slow"];
        vd.vol_bid_fp_fast = j_vol["vol_bid_fp_fast"];
        vd.vol_ask_fp_fast = j_vol["vol_ask_fp_fast"];
        vd.vol_bid_fp_slow = j_vol["vol_bid_fp_slow"];
        vd.vol_ask_fp_slow = j_vol["vol_ask_fp_slow"];
        vd.vol_bid_fp = j_vol["vol_bid_fp"];
        vd.vol_ask_fp = j_vol["vol_ask_fp"];
        vd.ema_diffOb_bid = j_vol["ema_diffOb_bid"];
        vd.ema_diffOb_ask = j_vol["ema_diffOb_ask"];
        vd.fp_bid = j_vol["fp_bid"];
        vd.fp_ask = j_vol["fp_ask"];
        vd.vol_bid_50ms = j_vol["vol_bid_50ms"];
        vd.vol_ask_50ms = j_vol["vol_ask_50ms"];
        vd.vol_bid_hl = j_vol["vol_bid_hl"];
        vd.vol_ask_hl = j_vol["vol_ask_hl"];
        vd.vol_rate_bid_hl = j_vol["vol_rate_bid_hl"];
        vd.vol_rate_ask_hl = j_vol["vol_rate_ask_hl"];
      }
      return 1;
    } else {
      INFO_FLOG("global_ts: {} is too new", global_ts);
      return 2;
    }
  }
}

void save_calculate_params(const StrategyConfig &CFG_, int64_t &global_ts,
                           data::InstrumentData &InstData_,
                           const std::string &dist_symbol) {
  const auto &json_path =
      CFG_.root_path +
      fmt::format("config/cal_params/cal_params_{}.json", dist_symbol);
  nlohmann::json j;
  j["timestamp"] = global_ts;
  j["flag_save"] = true;

  for (auto &id : InstData_.trading_ids) {
    auto *IC = InstData_.IM.FindByUniId(id);
    const auto &inst_str = IC->inst_str;
    auto &vd = InstData_.vol_map[id];
    auto &cc = vd.cuscore;
    const auto &base = IC->base;
    const auto &quote = IC->quote;
    const auto &base_str = IC->base_str;
    if (sum_util::Find(CFG_.digital_currencies, base)) {
      auto N = std::min(cc.diffs_bid_slow.get_max_length(),
                        cc.diffs_bid_slow.size());
      auto N_fast = std::min(cc.diffs_bid_fast.get_max_length(),
                             cc.diffs_bid_fast.size());
      auto N_mid =
          std::min(cc.diffs_bid_mid.get_max_length(), cc.diffs_bid_mid.size());
      j["params"]["cuscore"][inst_str] = {
          {"ema_bid_slow", cc.ema_bid_slow},
          {"ema_ask_slow", cc.ema_ask_slow},
          {"sigma_bid", cc.sigma_bid},
          {"sigma_ask", cc.sigma_ask},
          {"ema_bid_fast", cc.ema_bid_fast},
          {"ema_ask_fast", cc.ema_ask_fast},
          {"ema_bid_mid", cc.ema_bid_mid},
          {"ema_ask_mid", cc.ema_ask_mid},
          {"diffs_bid_slow", cc.diffs_bid_slow.get_latest_N_vec(N)},
          {"diffs_ask_slow", cc.diffs_ask_slow.get_latest_N_vec(N)},
          {"diffs_bid_mid", cc.diffs_bid_mid.get_latest_N_vec(N_mid)},
          {"diffs_ask_mid", cc.diffs_ask_mid.get_latest_N_vec(N_mid)},
          {"diffs_bid_fast", cc.diffs_bid_fast.get_latest_N_vec(N_fast)},
          {"diffs_ask_fast", cc.diffs_ask_fast.get_latest_N_vec(N_fast)},
          {"cuscore_bid_slow", cc.cuscore_bid_slow},
          {"cuscore_ask_slow", cc.cuscore_ask_slow},
          {"cuscore_bid_mid", cc.cuscore_bid_mid},
          {"cuscore_ask_mid", cc.cuscore_ask_mid},
          {"cuscore_bid_fast", cc.cuscore_bid_fast},
          {"cuscore_ask_fast", cc.cuscore_ask_fast},
          {"cuscore_cnt", cc.diffs_ask_slow.size()}};
    }
    if (quote == data::currency::USD &&
        sum_util::Find(CFG_.available_forex, base_str)) {
      const auto &ib_inst = fmt::format("{}_usd_cash.idealpro", base_str);
      const auto &id_ib_inst = InstData_.IM.inststr2id_.at(ib_inst);
      auto &ib_data = InstData_.bbo_map[id_ib_inst];
      j["params"]["ib"][inst_str] = {{"premium_bid", ib_data.premium_bid},
                                     {"premium_ask", ib_data.premium_ask},
                                     {"premium_cnt", 1000000},
                                     {"local_ts", global_ts}};
    }
    j["params"]["vol"][inst_str] = {{"ema_bid_fast", vd.ema_bid_fast},
                                    {"ema_ask_fast", vd.ema_ask_fast},
                                    {"ema_bid_mid", vd.ema_bid_mid},
                                    {"ema_ask_mid", vd.ema_ask_mid},
                                    {"ema_bid_slow", vd.ema_bid_slow},
                                    {"ema_ask_slow", vd.ema_ask_slow},
                                    {"vol_bid_fp_fast", vd.vol_bid_fp_fast},
                                    {"vol_ask_fp_fast", vd.vol_ask_fp_fast},
                                    {"vol_bid_fp_slow", vd.vol_bid_fp_slow},
                                    {"vol_ask_fp_slow", vd.vol_ask_fp_slow},
                                    {"vol_bid_fp", vd.vol_bid_fp},
                                    {"vol_ask_fp", vd.vol_ask_fp},
                                    {"ema_diffOb_bid", vd.ema_diffOb_bid},
                                    {"ema_diffOb_ask", vd.ema_diffOb_ask},
                                    {"fp_bid", vd.fp_bid},
                                    {"fp_ask", vd.fp_ask},
                                    {"vol_bid_50ms", vd.vol_bid_50ms},
                                    {"vol_ask_50ms", vd.vol_ask_50ms},
                                    {"vol_bid_hl", vd.vol_bid_hl},
                                    {"vol_ask_hl", vd.vol_ask_hl},
                                    {"vol_rate_bid_hl", vd.vol_rate_bid_hl},
                                    {"vol_rate_ask_hl", vd.vol_rate_ask_hl}};
    std::ofstream out(json_path);
    if (out.is_open()) {
      INFO_FLOG("save_calculate_params: {}", json_path.c_str());
      out << j.dump(4);
      out.close();
    } else
      ERROR_FLOG("Failed to open file: {}", json_path.c_str());
  }
}

void cal_and_save_pair_statics(const data::PairStats &pair_stats,
                               const StrategyConfig &CFG_,
                               const data::InstrumentData &InstData_,
                               const std::string &dist_symbol) {
  const size_t n = pair_stats.tick_len;
  if (n == 0 || pair_stats.per_inst.empty())
    return;
  auto percentile_fn = [](double p, double *beg, double *end) -> double {
    const size_t nn = static_cast<size_t>(end - beg);
    if (nn == 0)
      return std::numeric_limits<double>::quiet_NaN();
    std::sort(beg, end);
    const double idx = p * (nn - 1);
    const size_t lo = static_cast<size_t>(std::floor(idx));
    const size_t hi = std::min(lo + 1, nn - 1);
    const double frac = idx - lo;
    return (lo == hi) ? beg[lo] : beg[lo] * (1 - frac) + beg[hi] * frac;
  };
  // 已排序数组上直接按分位取值，避免重复 sort
  auto percentile_from_sorted = [](double p, const double *beg,
                                   size_t nn) -> double {
    if (nn == 0)
      return std::numeric_limits<double>::quiet_NaN();
    const double idx = p * (nn - 1);
    const size_t lo = static_cast<size_t>(std::floor(idx));
    const size_t hi = std::min(lo + 1, nn - 1);
    const double frac = idx - lo;
    return (lo == hi) ? beg[lo] : beg[lo] * (1 - frac) + beg[hi] * frac;
  };
  const auto &root_path = CFG_.root_path;
  std::filesystem::path dir_path = std::filesystem::path(root_path);
  const auto pair_stats_root =
      sum_util::JoinPath(dir_path.string(), "log", "pair_stats");
  const auto summary_path = sum_util::JoinPath(
      pair_stats_root, "s_bps_summary_" + dist_symbol + ".csv");

  // 创建 pair_stats 根目录（和 fp 日志同级）
  std::error_code ec_pair_root;
  std::filesystem::create_directories(pair_stats_root, ec_pair_root);
  if (ec_pair_root) {
    WARNING_FLOG(
        "[log_fp_only]Failed to create pair_stats root dir: {}, ec: {}",
        pair_stats_root, ec_pair_root.message());
  }
  std::ofstream fout(summary_path);
  if (!fout.is_open()) {
    WARNING_FLOG("[log_fp_only]Failed to open summary file: {}", summary_path);
    return;
  }
  fout << "inst,s_bps_percentile_bid,s_bps_percentile_ask,depth5_volume_"
          "bid_percentile,depth5_volume_ask_percentile,s_bps_pct_bid,s_bps_"
          "pct_ask,depth5_pct_bid,depth5_pct_ask,ticks,bid_samples,ask_"
          "samples\n";
  for (const auto &kv : pair_stats.per_inst) {
    const auto id = kv.first;
    const auto &inst_data = kv.second;
    auto *IC = InstData_.IM.FindByUniId(id);
    const std::string inst_str = IC ? IC->inst_str : "";
    const auto &pc = GetPairConfig(CFG_.pair_configs, inst_str);
    const double p_s_bid = pc.s_bps_summary_percentile_bid / 100.0;
    const double p_s_ask = pc.s_bps_summary_percentile_ask / 100.0;
    const double p_d5_bid = pc.depth5_summary_percentile_bid / 100.0;
    const double p_d5_ask = pc.depth5_summary_percentile_ask / 100.0;
    std::vector<double> s_bid_samples;
    std::vector<double> s_ask_samples;
    s_bid_samples.reserve(n);
    s_ask_samples.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      const double s_bid = inst_data.s_bid_bps[i];
      const double s_ask = inst_data.s_ask_bps[i];
      if (std::isfinite(s_bid) && s_bid > 0.0)
        s_bid_samples.push_back(s_bid);
      if (std::isfinite(s_ask) && s_ask > 0.0)
        s_ask_samples.push_back(s_ask);
    }
    const size_t m_bid = s_bid_samples.size();
    const size_t m_ask = s_ask_samples.size();
    if (m_bid > 0)
      std::sort(s_bid_samples.data(), s_bid_samples.data() + m_bid);
    if (m_ask > 0)
      std::sort(s_ask_samples.data(), s_ask_samples.data() + m_ask);
    const double s_bps_pct_bid =
        percentile_from_sorted(p_s_bid, s_bid_samples.data(), m_bid);
    const double s_bps_pct_ask =
        percentile_from_sorted(p_s_ask, s_ask_samples.data(), m_ask);
    std::vector<double> d5_bid(inst_data.depth5_volume_bid.data(),
                               inst_data.depth5_volume_bid.data() + n);
    std::vector<double> d5_ask(inst_data.depth5_volume_ask.data(),
                               inst_data.depth5_volume_ask.data() + n);
    const double depth5_bid_pct =
        percentile_fn(p_d5_bid, d5_bid.data(), d5_bid.data() + n);
    const double depth5_ask_pct =
        percentile_fn(p_d5_ask, d5_ask.data(), d5_ask.data() + n);

    // 1) 写 summary 行（分位数等聚合指标）
    fout << fmt::format(
        "{},{},{},{},{},{},{},{},{},{},{},{}\n", inst_str, s_bps_pct_bid,
        s_bps_pct_ask, depth5_bid_pct, depth5_ask_pct,
        pc.s_bps_summary_percentile_bid, pc.s_bps_summary_percentile_ask,
        pc.depth5_summary_percentile_bid, pc.depth5_summary_percentile_ask, n,
        s_bid_samples.size(), s_ask_samples.size());

    // 2) 写该 pair 的完整时间序列到 pair_stats/<pair>/<inst>.csv
    const auto pair_dir = sum_util::JoinPath(pair_stats_root, inst_str);
    std::error_code ec_pair;
    std::filesystem::create_directories(pair_dir, ec_pair);
    if (ec_pair) {
      WARNING_FLOG("[log_fp_only]Failed to create pair dir: {}, ec: {}",
                   pair_dir, ec_pair.message());
      continue;
    }
    const auto seq_path =
        sum_util::JoinPath(pair_dir, fmt::format("{}.csv", dist_symbol));
    std::ofstream seq_out(seq_path);
    if (!seq_out.is_open()) {
      WARNING_FLOG("[log_fp_only]Failed to open seq file: {}", seq_path);
      continue;
    }
    seq_out << "tick_idx,s_bid_bps,s_ask_bps,s_edge_bps,depth5_volume_bid,"
               "depth5_volume_ask\n";
    for (size_t i = 0; i < n; ++i) {
      seq_out << fmt::format("{},{},{},{},{},{}\n", i, inst_data.s_bid_bps[i],
                             inst_data.s_ask_bps[i], inst_data.s_bps[i],
                             inst_data.depth5_volume_bid[i],
                             inst_data.depth5_volume_ask[i]);
    }
    seq_out.close();
  }
  fout.close();
  INFO_FLOG("[log_fp_only]side-aware s_bps summary by pair (percentile from "
            "pair_configs), {} pairs, ticks: {}, saved to {}",
            pair_stats.per_inst.size(), n, summary_path);
}

void process_balances_monitor(
    std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    const data::depths_data &obs_quote_usd,
    const std::vector<data::currency> &digital_currencies,
    const data::InstrumentComponent &IC, const double &trade_qty,
    const double &avg_price) {
  if (sum_util::Find(digital_currencies, IC.base)) {
    auto &bm = BlcMng_.at(IC.base).balance_monitor;
    double trade_price;
    if (IC.quote == data::currency::USD)
      trade_price = avg_price;
    else {
      const auto &ob =
          (trade_qty > 0) ? obs_quote_usd.asks[0][0] : obs_quote_usd.bids[0][0];
      trade_price = avg_price * ob;
    }
    const auto &balance_quantity = BlcMng_.at(IC.base).balance_default;
    bm.position = BlcMng_.at(IC.base).balance_live - balance_quantity;
    if ((bm.position > 0 && trade_qty < 0) ||
        (bm.position < 0 && trade_qty > 0)) {
      double close_qty = 0.0;
      if (std::abs(trade_qty) <= std::abs(bm.position))
        close_qty = trade_qty;
      else
        close_qty = -bm.position;
      if (close_qty != 0.0)
        bm.realized_pnl += (bm.avg_cost - trade_price) * close_qty;
    }
    if ((bm.position >= 0 && trade_qty >= 0) ||
        (bm.position < 0 && trade_qty < 0)) {
      double new_position = bm.position + trade_qty;
      bm.avg_cost = (bm.avg_cost * std::abs(bm.position) +
                     trade_price * std::abs(trade_qty)) /
                    std::abs(new_position);
      bm.position = new_position;
    } else if ((bm.position >= 0 && trade_qty < 0) ||
               (bm.position < 0 && trade_qty > 0)) {
      if (std::abs(trade_qty) < std::abs(bm.position)) {
        bm.position += trade_qty;
        if (std::abs(bm.position) < 1e-1)
          bm.avg_cost = 0.0;
      } else if (std::abs(std::abs(trade_qty) - std::abs(bm.position)) <
                 1e-11) {
        bm.position = 0.0;
        bm.avg_cost = 0.0;
      } else {
        bm.position = trade_qty + bm.position;
        bm.avg_cost = trade_price;
      }
    }
  }
}

void print_fetch_orders_map(
    const std::unordered_map<data::currency, data::fetch_orders_data>
        &fetch_orders_map,
    const data::InstrumentData &InstData_) {
  INFO_FLOG("==== fetch_orders_map_ 内容打印 ====");
  for (const auto &one : fetch_orders_map) {
    const auto &currency = one.first;
    const auto &data = one.second;
    INFO_FLOG("币种: {}", data::get_currency_name(currency));
    for (const auto &item : data.place_order_points_dict) {
      const auto &id = item.first;
      const auto &orders = item.second;
      auto *IC = InstData_.IM.FindByUniId(id);
      const auto &inst_str = IC->inst_str;
      const auto &depth_data = InstData_.depth_map.at(id);
      INFO_FLOG("  交易对id: {} ({})", id, inst_str);
      for (const auto &order : orders) {
        std::string side_str =
            (order.s == data::side::BUY)
                ? "买"
                : ((order.s == data::side::SELL) ? "卖" : "未知");
        INFO_FLOG("    side: {}, price: {}, qty: {}, depth: {}", side_str,
                  order.price, order.quantity, depth_data.bids[0][0]);
      }
    }
  }
  INFO_FLOG("==== fetch_orders_map_ 打印结束 ====");
}

// 读取ret.csv文件
bool load_ret_csv(StrategyConfig &CFG_) {
  try {
    // 从CFG_.Quote.backtest.begin_time提取日期
    std::string date_str = "";
    if (CFG_.backtest && !CFG_.Quote.backtest.begin_time.empty()) {
      // begin_time格式: "20250101.00:00:00" 或 "20250101 00:00:00"
      const auto &time_split =
          sum_util::StrSplit(CFG_.Quote.backtest.begin_time, '.');
      if (time_split.size() >= 1) {
        date_str = time_split[0];
      } else {
        // 如果没有点号，尝试按空格分割
        const auto &space_split =
            sum_util::StrSplit(CFG_.Quote.backtest.begin_time, ' ');
        if (space_split.size() >= 1) {
          date_str = space_split[0];
        }
      }
    }

    // 构建文件路径
    std::string ret_csv_path;
    if (!date_str.empty()) {
      ret_csv_path = sum_util::JoinPath(CFG_.root_path,
                                        fmt::format("ret_{}.csv", date_str));
      INFO_FLOG("Using date-specific ret file: {}", ret_csv_path);
    } else {
      ret_csv_path = sum_util::JoinPath(CFG_.root_path, "ret.csv");
      INFO_FLOG("Using default ret file: {}", ret_csv_path);
    }

    if (sum_util::FileExists(ret_csv_path)) {
      INFO_FLOG("Reading ret.csv file from: {}", ret_csv_path);

      // 使用file_util.h中的ReadCsv函数读取CSV文件
      auto csv_data =
          sum_util::ReadCsv<std::string>(ret_csv_path, ',', true, false);

      // 清空现有数据
      CFG_.ts_ret.clear();
      CFG_.rets.clear();
      CFG_.idx_ret = 0; // 重置ret数据索引

      if (csv_data.empty()) {
        WARNING_FLOG("CSV file is empty, skipping ret.csv reading");
        return false;
      }

      // 获取列数
      size_t num_columns = csv_data[0].size();
      INFO_FLOG("CSV has {} columns", num_columns);

      // 打印第一行数据（如果没有header）
      INFO_FLOG("First row data:");
      for (size_t i = 0; i < num_columns; ++i) {
        INFO_FLOG("  Column {}: '{}'", i, csv_data[0][i]);
      }

      // 检查是否有足够的列（至少需要：time, timestamp,
      // 以及至少一个currency列）
      if (num_columns >= 3) {
        // 为每个digital_currency初始化vector
        for (const auto &currency : CFG_.digital_currencies) {
          CFG_.rets[currency] = std::vector<double>();
        }

        // 解析数据行（跳过header行）
        for (size_t row_idx = 1; row_idx < csv_data.size(); ++row_idx) {
          const auto &row = csv_data[row_idx];

          if (row.size() < num_columns) {
            WARNING_FLOG("Row {} has insufficient columns, skipping", row_idx);
            continue;
          }

          try {
            // 解析timestamp列（第1列，索引0）
            int64_t timestamp = std::stoll(row[0]);
            CFG_.ts_ret.push_back(timestamp);

            // 解析每个digital_currency的ret值
            for (const auto &currency : CFG_.digital_currencies) {
              std::string currency_name = data::get_currency_name(currency);
              std::string column_name = fmt::format(
                  "ret_{}t",
                  sum_util::StrUpper(std::string_view(currency_name)));

              // 查找对应的列
              size_t col_idx = 0;
              bool found = false;
              for (size_t i = 0; i < num_columns; ++i) {
                if (csv_data[0][i] == column_name) {
                  col_idx = i;
                  found = true;
                  break;
                }
              }

              if (found && col_idx < row.size()) {
                double ret = std::stod(row[col_idx]);
                CFG_.rets[currency].push_back(ret);
              } else {
                // 如果找不到对应的列，填充0或跳过
                WARNING_FLOG(
                    "Column '{}' not found for currency {}, filling with 0",
                    column_name, currency_name);
                CFG_.rets[currency].push_back(0.0);
              }
            }
          } catch (const std::exception &e) {
            WARNING_FLOG("Failed to parse row {}: {}, error: {}", row_idx,
                         fmt::format("{}", fmt::join(row, ",")), e.what());
            continue;
          }
        }

        INFO_FLOG("Successfully loaded {} rows from ret.csv",
                  CFG_.ts_ret.size());
        if (!CFG_.ts_ret.empty()) {
          INFO_FLOG("Timestamp range: {} to {}", CFG_.ts_ret.front(),
                    CFG_.ts_ret.back());
        }

        // 显示加载的currency信息
        INFO_FLOG("Loaded rets for {} currencies:", CFG_.rets.size());
        for (const auto &[currency, rets_vector] : CFG_.rets) {
          INFO_FLOG("  Currency: {}, Records: {}, Sample values: [{}, {}, {}]",
                    data::get_currency_name(currency), rets_vector.size(),
                    rets_vector.empty() ? 0.0 : rets_vector[0],
                    rets_vector.size() > 1 ? rets_vector[1] : 0.0,
                    rets_vector.size() > 2 ? rets_vector[2] : 0.0);
        }
        return true;
      } else {
        WARNING_FLOG("CSV file has insufficient columns ({}), skipping "
                     "ret.csv reading",
                     num_columns);
        return false;
      }
    } else {
      WARNING_FLOG("ret CSV file not found at: {}", ret_csv_path);
      if (!date_str.empty()) {
        WARNING_FLOG("Tried to load date-specific file: ret_{}.csv", date_str);
      }
      return false;
    }
  } catch (const std::exception &e) {
    ERROR_FLOG("Failed to read ret.csv file: {}", e.what());
    return false;
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
                               const StrategyConfig &CFG_, bool verbose) {
  double expected_return = 0.0, usd_values = 0.0;
  for (auto &[id, OM] : InstData_.order_map) {
    auto *IC = InstData_.IM.FindByUniId(id);
    auto &left_orders = OM.left_order_;
    auto &operation_flags = OM.operation_flags;
    if (left_orders.empty())
      continue;
    const auto &inst_usd =
        format_main_exchange_usd_pair(IC->base_str, CFG_.aim_exchange);
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
    data::InstrumentData &InstData_, const StrategyConfig &CFG_,
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
      double factor = DataProcess::adjust_fair_price(
          *IC, currency, order.price, qty, CFG_.setting, BlcMng_, verbose);
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