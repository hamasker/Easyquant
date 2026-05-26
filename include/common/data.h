#ifndef DATA_H
#define DATA_H

#include "circular_buffer.h"
#include "configs/pair_configs.h"
#include "nova_trader_api.h"
#include "util_tools/sum_util.h"

#include "common/data_currency.h"
#include "common/data_currency_pair.h"
#include "common/data_exchange.h"
#include "common/data_status.h"
#include "constant.h"
#include "markout.h"

#include <limits>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <unordered_map>

namespace data {
using UniInstID = uint16_t;
/*
 * order_type
 */
enum class order_type : int8_t { UNKNOWN = 0, LIMIT = 1, MARKET };

inline std::string get_order_type_name(order_type e) {
  switch (e) {
  case order_type::LIMIT:
    return "limit";
  case order_type::MARKET:
    return "market";
  default:
    return "unknown";
  }
}

static std::string order_type_strings[] = {"limit", "market"};

inline order_type str_to_order_type(const std::string &name) {
  /*
   * 注意: 和order_type的定义顺序相关
   */
  int i = 1;
  for (const auto &order_type_str : order_type_strings) {
    if (order_type_str == name) {
      return static_cast<order_type>(i);
    }
    i++;
  }
  return order_type::UNKNOWN;
}

/*
 * order_status
 */
enum class order_status : int8_t {
  UNKNOWN = 0,
  NONE = 1,
  NEW,
  FAILED,
  PARTIAL_FILLED,
  FILLED,
  CANCELED,
  CLOSED,
  EXPIRED
};

inline std::string get_order_status_name(order_status e) {
  switch (e) {
  case order_status::NONE:
    return "none";
  case order_status::NEW:
    return "new";
  case order_status::FAILED:
    return "failed";
  case order_status::PARTIAL_FILLED:
    return "partial_filled";
  case order_status::FILLED:
    return "filled";
  case order_status::CANCELED:
    return "canceled";
  case order_status::CLOSED:
    return "closed";
  case order_status::EXPIRED:
    return "expired";
  default:
    return "unknown";
  }
}

static std::string order_status_strings[] = {
    "none",   "new",      "failed", "partial_filled",
    "filled", "canceled", "closed", "expired"};

inline order_status str_to_order_status(const std::string &name) {
  /*
   * 注意: 和order_status的定义顺序相关
   */
  int i = 1;
  for (const auto &order_status_str : order_status_strings) {
    if (order_status_str == name) {
      return static_cast<order_status>(i);
    }
    i++;
  }
  return order_status::UNKNOWN;
}

/*
 * position_side
 */

enum class position_side : int8_t { UNKNOWN = 0, BOTH = 1, LONG, SHORT };

inline std::string get_position_side_name(position_side e) {
  switch (e) {
  case position_side::BOTH:
    return "both";
  case position_side::LONG:
    return "long";
  case position_side::SHORT:
    return "short";
  default:
    return "unknown";
  }
}

static std::string position_side_strings[] = {"both", "long", "short"};

inline position_side str_to_position_side(const std::string &name) {
  /*
   * 注意: 和position_side的定义顺序相关
   */
  int i = 1;
  for (const auto &position_side_str : position_side_strings) {
    if (position_side_str == name) {
      return static_cast<position_side>(i);
    }
    i++;
  }
  return position_side::UNKNOWN;
}

/*
 * margin_mode
 */

enum class margin_mode : int8_t { UNKNOWN = 0, CROSS = 1, ISOLATED };

inline std::string get_margin_mode_name(margin_mode e) {
  switch (e) {
  case margin_mode::CROSS:
    return "cross";
  case margin_mode::ISOLATED:
    return "isolated";
  default:
    return "unknown";
  }
}

static std::string margin_mode_strings[] = {"cross", "isolated"};

inline margin_mode str_to_margin_mode(const std::string &name) {
  /*
   * 注意: 和margin_mode的定义顺序相关
   */
  int i = 1;
  for (const auto &margin_mode_str : margin_mode_strings) {
    if (margin_mode_str == name) {
      return static_cast<margin_mode>(i);
    }
    i++;
  }
  return margin_mode::UNKNOWN;
}

/*
 * side
 */
enum class side : int8_t { UNKNOWN = 0, BUY = 1, SELL };

inline std::string get_side_name(side e) {
  switch (e) {
  case side::BUY:
    return "buy";
  case side::SELL:
    return "sell";
  default:
    return "unknown";
  }
}

static std::string side_strings[] = {"buy", "sell"};

inline side str_to_side(const std::string &name) {
  /*
   * 注意: 和side的定义顺序相关
   */
  int i = 1;
  for (const auto &side_str : side_strings) {
    if (side_str == name) {
      return static_cast<side>(i);
    }
    i++;
  }
  return side::UNKNOWN;
}

/*
 * status
 */
enum class status : int8_t {
  UNKNOWN = 0,
  NORMAL = 1,
  ERROR,
};

inline std::string get_status_name(status e) {
  switch (e) {
  case status::NORMAL:
    return "normal";
  case status::ERROR:
    return "error";
  default:
    return "unknown";
  }
}

static std::string status_strings[] = {"normal", "error"};

inline status str_to_status(const std::string &name) {
  /*
   * 注意: 和status的定义顺序相关
   */
  int i = 1;
  for (const auto &status_str : status_strings) {
    if (status_str == name) {
      return static_cast<status>(i);
    }
    i++;
  }
  return status::UNKNOWN;
}

/*
 * depth type
 */
enum class depth_type : int8_t {
  UNKNOWN = 0,
  BID = 1,
  ASK,
  BID_QUANTITY,
  ASK_QUANTITY,
};

inline std::string get_depth_type_name(depth_type e) {
  switch (e) {
  case depth_type::BID:
    return "bid";
  case depth_type::ASK:
    return "ask";
  case depth_type::BID_QUANTITY:
    return "bid_quantity";
  case depth_type::ASK_QUANTITY:
    return "ask_quantity";
  default:
    return "unknown";
  }
}

static std::string depth_type_strings[] = {"bid", "ask", "bid_quantity",
                                           "ask_quantity"};

inline depth_type str_to_depth_type(const std::string &name) {
  /*
   * 注意: 和status的定义顺序相关
   */
  int i = 1;
  for (const auto &depth_type_str : depth_type_strings) {
    if (depth_type_str == name) {
      return static_cast<depth_type>(i);
    }
    i++;
  }
  return depth_type::UNKNOWN;
}

struct order_book_data {
  exchange ex;
  currency_pair symbol;
  int64_t server_timestamp; // 时间戳微秒
  int64_t local_timestamp;  // 时间戳微秒
  int ask_size;             // 有效数据长度
  int bid_size;             // 有效数据长度
  double asks_price[128];
  double asks_volume[128];
  double bids_price[128];
  double bids_volume[128];
};

inline std::string order_book_data_snapshot_csv_header(int level) {
  if (level == 1) {
    return std::string(
        "exchange,symbol,server_timestamp,local_timestamp,"
        "asks[0].price,asks[0].volume,bids[0].price,bids[0].volume");
  } else if (level == 5) {
    return std::string(
        "exchange,symbol,server_timestamp,local_timestamp,"
        "asks[0].price,asks[0].volume,bids[0].price,bids[0].volume,"
        "asks[1].price,asks[1].volume,bids[1].price,bids[1].volume,"
        "asks[2].price,asks[2].volume,bids[2].price,bids[2].volume,"
        "asks[3].price,asks[3].volume,bids[3].price,bids[3].volume,"
        "asks[4].price,asks[4].volume,bids[4].price,bids[4].volume");
  } else if (level == 10) {
    return std::string(
        "exchange,symbol,server_timestamp,local_timestamp,"
        "asks[0].price,asks[0].volume,bids[0].price,bids[0].volume,"
        "asks[1].price,asks[1].volume,bids[1].price,bids[1].volume,"
        "asks[2].price,asks[2].volume,bids[2].price,bids[2].volume,"
        "asks[3].price,asks[3].volume,bids[3].price,bids[3].volume,"
        "asks[4].price,asks[4].volume,bids[4].price,bids[4].volume,"
        "asks[5].price,asks[5].volume,bids[5].price,bids[5].volume,"
        "asks[6].price,asks[6].volume,bids[6].price,bids[6].volume,"
        "asks[7].price,asks[7].volume,bids[7].price,bids[7].volume,"
        "asks[8].price,asks[8].volume,bids[8].price,bids[8].volume,"
        "asks[9].price,asks[9].volume,bids[9].price,bids[9].volume");
  }
  return std::string("");
}
inline std::string order_book_data_to_snapshot_csv(order_book_data &data,
                                                   int level) {
  if (level == 1) {
    return fmt::format(
        "{},{},{},{},"
        "{},{},{},{}",
        get_exchange_name(data.ex), get_currency_pair_name(data.symbol),
        data.server_timestamp, data.local_timestamp, data.asks_price[0],
        data.asks_volume[0], data.bids_price[0], data.bids_volume[0]);
  } else if (level == 5) {
    return fmt::format(
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{}",
        get_exchange_name(data.ex), get_currency_pair_name(data.symbol),
        data.server_timestamp, data.local_timestamp, data.asks_price[0],
        data.asks_volume[0], data.bids_price[0], data.bids_volume[0],
        data.asks_price[1], data.asks_volume[1], data.bids_price[1],
        data.bids_volume[1], data.asks_price[2], data.asks_volume[2],
        data.bids_price[2], data.bids_volume[2], data.asks_price[3],
        data.asks_volume[3], data.bids_price[3], data.bids_volume[3],
        data.asks_price[4], data.asks_volume[4], data.bids_price[4],
        data.bids_volume[4]);
  } else if (level == 10) {
    return fmt::format(
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{},"
        "{},{},{},{}",
        get_exchange_name(data.ex), get_currency_pair_name(data.symbol),
        data.server_timestamp, data.local_timestamp, data.asks_price[0],
        data.asks_volume[0], data.bids_price[0], data.bids_volume[0],
        data.asks_price[1], data.asks_volume[1], data.bids_price[1],
        data.bids_volume[1], data.asks_price[2], data.asks_volume[2],
        data.bids_price[2], data.bids_volume[2], data.asks_price[3],
        data.asks_volume[3], data.bids_price[3], data.bids_volume[3],
        data.asks_price[4], data.asks_volume[4], data.bids_price[4],
        data.bids_volume[4], data.asks_price[5], data.asks_volume[5],
        data.bids_price[5], data.bids_volume[5], data.asks_price[6],
        data.asks_volume[6], data.bids_price[6], data.bids_volume[6],
        data.asks_price[7], data.asks_volume[7], data.bids_price[7],
        data.bids_volume[7], data.asks_price[8], data.asks_volume[8],
        data.bids_price[8], data.bids_volume[8], data.asks_price[9],
        data.asks_volume[9], data.bids_price[9], data.bids_volume[9]);
  }
  return std::string("");
}

struct trade_data {
  exchange ex;
  currency_pair symbol;
  int64_t trade_timestamp;  // 成交时间
  int64_t server_timestamp; // 时间戳微秒
  int64_t local_timestamp;  // 时间戳微秒
  side s;                   // 交易方向
  double price;
  double volume;
};

inline std::string trade_data_csv_header() {
  return std::string(
      "exchange,symbol,trade_timestamp,server_timestamp,local_timestamp,"
      "side,price,volume");
}
inline std::string trade_data_to_csv(trade_data &data) {
  /*
   * exchange,symbol,trade_timestamp,server_timestamp,local_timestamp,side,price,volume
   */
  return fmt::format("{},{},{},{},{},{},{},{}", get_exchange_name(data.ex),
                     get_currency_pair_name(data.symbol), data.trade_timestamp,
                     data.server_timestamp, data.local_timestamp,
                     get_side_name(data.s), data.price, data.volume);
}

struct ticker_data {
  exchange ex;
  currency_pair symbol;
  int64_t server_timestamp; // 时间戳微秒
  int64_t local_timestamp;  // 时间戳微秒
  double ask_price;
  double ask_volume;
  double bid_price;
  double bid_volume;
};

inline std::string ticker_data_csv_header() {
  return std::string("exchange,symbol,server_timestamp,local_timestamp,"
                     "ask_price,ask_volume,bid_price,bid_volume");
}
inline std::string ticker_data_to_csv(ticker_data &data) {
  /*
   * exchange,symbol,server_timestamp,local_timestamp,ask_price,ask_volume,bid_price,bid_volume
   */
  return fmt::format("{},{},{},{},"
                     "{},{},{},{}",
                     get_exchange_name(data.ex),
                     get_currency_pair_name(data.symbol), data.server_timestamp,
                     data.local_timestamp, data.ask_price, data.ask_volume,
                     data.bid_price, data.bid_volume);
}

struct triple_data {
  int64_t local_timestamp; // 时间戳微秒
  double ask;
  double mid;
  double bid;
};

inline std::string triple_data_csv_header(std::string symbol = "") {
  if (symbol.empty()) {
    return std::string("bid,mid,ask,local_timestamp");
  } else {
    return std::string("symbol,bid,mid,ask,local_timestamp");
  }
}
inline std::string triple_data_to_csv(triple_data &data,
                                      std::string symbol = "") {
  if (symbol.empty()) {
    /*
     * bid,mid,ask,local_timestamp
     */
    return fmt::format("{},{},{},{}", data.bid, data.mid, data.ask,
                       data.local_timestamp);
  } else {
    /*
     * symbol,bid,mid,ask,local_timestamp
     */
    return fmt::format("{},{},{},{},{}", symbol, data.bid, data.mid, data.ask,
                       data.local_timestamp);
  }
}

struct pair_data {
  int64_t local_timestamp; // 时间戳微秒
  double ask;
  double bid;
};

inline std::string pair_data_csv_header(std::string symbol = "") {
  if (symbol.empty()) {
    return std::string("bid,ask,local_timestamp");
  } else {
    return std::string("symbol,bid,ask,local_timestamp");
  }
}
inline std::string pair_data_to_csv(pair_data &data, std::string symbol = "") {
  if (symbol.empty()) {
    /*
     * bid,ask,local_timestamp
     */
    return fmt::format("{},{},{}", data.bid, data.ask, data.local_timestamp);
  } else {
    /*
     * symbol,bid,ask,local_timestamp
     */
    return fmt::format("{},{},{},{}", symbol, data.bid, data.ask,
                       data.local_timestamp);
  }
}

struct value_data {
  int64_t local_timestamp; // 时间戳微秒
  double value;
};

inline std::string value_data_csv_header(std::string symbol = "") {
  if (symbol.empty()) {
    return std::string("value,local_timestamp");
  } else {
    return std::string("symbol,value,local_timestamp");
  }
}
inline std::string value_data_to_csv(value_data &data,
                                     std::string symbol = "") {
  if (symbol.empty()) {
    /*
     * value,local_timestamp
     */
    return fmt::format("{},{}", data.value, data.local_timestamp);
  } else {
    /*
     * symbol,value,local_timestamp
     */
    return fmt::format("{},{},{}", symbol, data.value, data.local_timestamp);
  }
}

struct my_order_data {
  exchange ex;
  currency_pair symbol;
  int64_t
      create_timestamp; // 订单创建的本地时间,
                        // 第一次收到这个订单websocket推送的时间, 本地时间(微秒)
  int64_t server_timestamp; // 订单最近一次更新的时间, 服务器时间(微秒)
  int64_t local_timestamp; // 订单最近一次更新的时间, 本地时间(微秒)
  char order_id[128];      // 订单号
  char client_order_id[128]; // 自定义订单号
  side s;                    // 交易方向
  order_status os;           // 订单状态
  order_type ot;             // 订单类型
  double price;              // 下单价格
  double avg_price;          // 平均成交价
  double quantity; // 原始的订单数量, 一旦设定, 后续不会再修改
  double exec_quantity; // 已经成交的订单数量
  double fee;           // 手续费
};

inline std::string my_order_data_csv_header() {
  return std::string(
      "exchange,symbol,create_timestamp,server_timestamp,local_timestamp,"
      "order_id,client_order_id,side,order_status,order_type,"
      "price,avg_price,quantity,exec_quantity,fee");
}
inline std::string my_order_data_to_csv(my_order_data &data) {
  /*
   * exchange,symbol,create_timestamp,server_timestamp,local_timestamp,order_id,client_order_id,side,order_status,order_type,price,avg_price,quantity,exec_quantity,fee
   */
  return fmt::format(
      "{},{},{},{},{},"
      "{},{},{},{},{},"
      "{},{},{},{},{}",
      get_exchange_name(data.ex), get_currency_pair_name(data.symbol),
      data.create_timestamp, data.server_timestamp, data.local_timestamp,
      data.order_id, data.client_order_id, get_side_name(data.s),
      get_order_status_name(data.os), get_order_type_name(data.ot), data.price,
      data.avg_price, data.quantity, data.exec_quantity, data.fee);
}

struct my_trade_data {
  exchange ex;
  currency_pair symbol;
  int64_t trade_timestamp;   // 成交时间
  int64_t server_timestamp;  // 时间戳微秒
  int64_t local_timestamp;   // 时间戳微秒
  char trade_id[128];        // trade id
  char order_id[128];        // 订单号
  char client_order_id[128]; // 自定义订单号
  side s;                    // 交易方向
  double price;              // 成交价格
  double volume;             // 成交量
  double fee;                // 手续费
};

inline std::string my_trade_data_csv_header() {
  return std::string(
      "exchange,symbol,trade_timestamp,server_timestamp,local_timestamp,"
      "trade_id,order_id,client_order_id,side,"
      "price,volume,fee");
}
inline std::string my_trade_data_to_csv(my_trade_data &data) {
  /*
   * exchange,symbol,trade_timestamp,server_timestamp,local_timestamp,trade_id,order_id,client_order_id,side,price,volume,fee
   */
  return fmt::format("{},{},{},{},{},"
                     "{},{},{},{},"
                     "{},{},{}",
                     get_exchange_name(data.ex),
                     get_currency_pair_name(data.symbol), data.trade_timestamp,
                     data.server_timestamp, data.local_timestamp, data.trade_id,
                     data.order_id, data.client_order_id, get_side_name(data.s),
                     data.price, data.volume, data.fee);
}

struct my_position_data {
  exchange ex;
  currency_pair symbol;
  int64_t server_timestamp; // position最近一次更新的时间, 服务器时间(微秒)
  int64_t local_timestamp; // position最近一次更新的时间, 本地时间(微秒)
  double leverage;         // 杠杆倍率
  double unrealized_pnl;    // 持仓未实现盈亏
  double avg_price;         // 开仓均价
  double mark_price;        // 标记价格
  double liquidation_price; // 强平价格
  double position_quantity; // 仓数量, 注意: '做多为正数, 做空为负数'
  position_side ps;         // 持仓方向: BOTH/LONG/SHORT
  margin_mode mm;           // "全仓(cross)"或者"逐仓(isolated)"
};
inline std::string my_position_data_csv_header() {
  return std::string(
      "exchange,symbol,server_timestamp,local_timestamp,"
      "leverage,unrealized_pnl,avg_price,mark_price,liquidation_price,"
      "position_quantity,position_side,margin_mode");
}
inline std::string my_position_data_to_csv(my_position_data &data) {
  /*
   * exchange,symbol,server_timestamp,local_timestamp,leverage,unrealized_pnl,avg_price,mark_price,liquidation_price,position_quantity,position_side,margin_mode
   */
  return fmt::format("{},{},{},{},"
                     "{},{},{},{},{},"
                     "{},{},{}",
                     get_exchange_name(data.ex),
                     get_currency_pair_name(data.symbol), data.server_timestamp,
                     data.local_timestamp, data.leverage, data.unrealized_pnl,
                     data.avg_price, data.mark_price, data.liquidation_price,
                     data.position_quantity, get_position_side_name(data.ps),
                     get_margin_mode_name(data.mm));
}

struct DigitalFpChanState {
  bool inited{false};
  double ema_m{0.0};

  double var{0.0};      // fast var (EWMA of err^2)
  double var_base{0.0}; // slow baseline var (EWMA of var)

  // ===== return-only 需要 =====
  bool has_prev_m{false};
  double prev_m{0.0};
};

struct FpEvalState {
  bool inited{false};

  double prev_m_fair{0.0};

  // EWMA 指标（bps）
  double ema_abs_dev_bps{0.0};  // |m_fair - m_k|
  double ema_abs_step_bps{0.0}; // |Δm_fair|
  double ema_gate{0.0};         // gate_x 平均
  double ema_xpos{0.0};         // I(x>0) 超 band 比例
  double ema_overshoot{0.0};    // overshoot proxy
  double ema_abs_z_bps{0.0};    // |z| 的 bps 量级

  uint64_t last_report_ts{0}; // ns
  uint64_t sample_cnt{0};     // 计数
};

struct DigitalFpState {
  std::array<DigitalFpChanState, 6> ch;

  // ===== 变化率记忆状态 =====
  double z{0.0}; // z_t = rho*z_{t-1} + r_lead
  bool z_inited{false};
  // ★新增：aim 的 prev_m_k，用来算 r_aim
  double prev_m_k{0.0};
  bool has_prev_m_k{false};

  // ★在线评估状态
  FpEvalState eval;
};

struct fair_price_data {
  CircularBuffer<10000, 1> timestamps;
  CircularBuffer<10000, 2> fps;
  CircularBuffer<10000, 1> vps;
  DigitalFpState digital_state;
};

inline std::string get_pair_from_inst(std::string_view inst_str) {
  auto dot_pos = inst_str.find('.');
  if (dot_pos == std::string_view::npos)
    return "";

  auto core = inst_str.substr(0, dot_pos); // 去掉 .exchange 后部分

  auto first_us = core.find('_');
  if (first_us == std::string_view::npos)
    return "";

  auto second_us = core.find('_', first_us + 1);

  // 截取 base 和 quote
  std::string_view base = core.substr(0, first_us);
  std::string_view quote;
  if (second_us == std::string_view::npos) {
    // 只有 base_quote 格式
    quote = core.substr(first_us + 1);
  } else {
    // 有 base_quote_dtype 格式
    quote = core.substr(first_us + 1, second_us - first_us - 1);
  }

  // 拼接 base/quote，返回 std::string
  std::string result;
  result.reserve(base.size() + 1 + quote.size());
  result.append(base).append("/").append(quote);
  return result;
}

inline std::string get_inst_from_pair(std::string_view pair,
                                      std::string_view exchange = "krk",
                                      std::string_view dtype = "") {
  auto slash_pos = pair.find('/');
  if (slash_pos == std::string_view::npos)
    return "";

  std::string_view base = pair.substr(0, slash_pos);
  std::string_view quote = pair.substr(slash_pos + 1);

  std::string result;
  result.reserve(base.size() + 1 + quote.size() + 1 + dtype.size() + 1 +
                 exchange.size());

  result.append(base).append("_").append(quote);
  if (!dtype.empty()) {
    result.append("_").append(dtype);
  }
  result.append(".").append(exchange);
  return result;
}

struct InstrumentComponent {
  UniInstID uni_id;
  InstrumentId inst_id;
  const InstrumentBaseInfo *base_info;
  std::string config_path;
  std::string inst_str;
  std::string exchange_str_nova;
  data::exchange exchange;
  std::string currency_pair_str;
  data::currency_pair currency_pair;
  std::string base_str;
  data::currency base;
  std::string quote_str;
  data::currency quote;
  double limit_usd;
  double rate_limit;
  double tick_size;
  double quantity_size;
  double quantity_precision;
  double min_notional;
  double fp_bid;
  double fp_ask;
  double adjust_factor;
  int price_precision;
  bool flag_exceed = false;
  bool flag_trading = false;
  int64_t ts_abnormal;
  std::vector<std::vector<double>> prob_params_buy;
  std::vector<std::vector<double>> prob_params_sell;
  MarkoutTable markout_table; // 新增 MarkoutTable 成员
  // ★新增：每个合成inst自己维护一份 state（6通道 + z 等）
  data::DigitalFpState fp_state;

  InstrumentComponent() = default;

  InstrumentComponent(InstrumentId id, const InstrumentBaseInfo *bi,
                      std::string path)
      : inst_id(id), base_info(bi), config_path(path), markout_table() {
    static UniInstID next_id = 1; // 静态变量，类型为UniInstID
    uni_id = next_id++;           // 分配唯一ID并自增
    inst_str = inst_id.symbol;
    exchange_str_nova = GetExchangeStrFromId(inst_id.exchange);
    exchange = data::str_to_exchange("kraken");
    currency_pair_str = sum_util::StrSplit(inst_str, '.')[0];
    sum_util::StrReplace(currency_pair_str, "_", "/");
    currency_pair = data::str_to_currency_pair(currency_pair_str, exchange);
    base = data::get_currency_pair_base(currency_pair);
    quote = data::get_currency_pair_quote(currency_pair);
    base_str = data::get_currency_name(base);
    quote_str = data::get_currency_name(quote);
    if (base_info == nullptr) {
      min_notional = 0;
      tick_size = 0;
      quantity_size = 0;
    } else {
      min_notional = base_info->min_notional_;
      tick_size = base_info->price_.tick_size;
      quantity_size =
          sum_util::tick_size_to_price_precision(base_info->qty_.min_qty);
    }
    price_precision = sum_util::tick_size_to_price_precision(tick_size);
    quantity_precision = std::pow(10, -quantity_size);
    // INFO_FLOG("inst: {}, quantity_size: {}, quantiy_precision: {}",
    //   inst_str, quantity_size, quantity_precision);
    auto currency_pair_read = currency_pair_str;
    sum_util::StrReplace(currency_pair_read, "/", "-");
    sum_util::StrReplace(currency_pair_read, "xbt", "btc");
    sum_util::StrUpper(currency_pair_read);

    std::string file_dir_buy = config_path + currency_pair_read + "_buy.csv";
    std::string file_dir_sell = config_path + currency_pair_read + "_sell.csv";
    try {
      prob_params_buy = sum_util::ReadCsv(file_dir_sell);
      prob_params_sell = sum_util::ReadCsv(file_dir_buy);
    } catch (const std::exception &e) {
      // CSV 文件不存在或读取失败: 保留空向量
      prob_params_buy.clear();
      prob_params_sell.clear();
    } // ! 注意prob的含义
    std::string file_dir_edges = config_path + currency_pair_str + "_edges.csv";
    std::string file_dir_markout =
        config_path + currency_pair_str + "_markout.csv";
    if (sum_util::FileExists(file_dir_edges) &&
        sum_util::FileExists(file_dir_markout)) {
      markout_table.load_edges_csv(file_dir_edges);
      markout_table.load_markout_csv(file_dir_markout);
    }
  }
};

class InstrumentManager {
public:
  // 插入InstrumentComponent
  void Insert(const InstrumentComponent &comp) {
    IC_map_[comp.uni_id] = comp;
    inststr2id_[comp.inst_str] = comp.uni_id;
  }

  // 通过inst_str查找InstrumentComponent，返回指针，找不到返回nullptr
  InstrumentComponent *FindByInstStr(const std::string &inst_str) {
    auto it = inststr2id_.find(inst_str);
    if (it != inststr2id_.end()) {
      auto ait = IC_map_.find(it->second);
      if (ait != IC_map_.end()) {
        return &ait->second;
      }
    }
    return nullptr;
  }
  // const重载，返回const指针
  const InstrumentComponent *FindByInstStr(const std::string &inst_str) const {
    auto it = inststr2id_.find(inst_str);
    if (it != inststr2id_.end()) {
      auto ait = IC_map_.find(it->second);
      if (ait != IC_map_.end()) {
        return &ait->second;
      }
    }
    return nullptr;
  }

  // 通过uni_id查找InstrumentComponent，返回指针，找不到返回nullptr
  InstrumentComponent *FindByUniId(UniInstID id) {
    auto it = IC_map_.find(id);
    if (it != IC_map_.end()) {
      return &it->second;
    }
    return nullptr;
  }
  // const重载，返回const指针
  const InstrumentComponent *FindByUniId(UniInstID id) const {
    auto it = IC_map_.find(id);
    if (it != IC_map_.end()) {
      return &it->second;
    }
    return nullptr;
  }

  // 删除InstrumentComponent（通过uni_id）
  void EraseByUniId(UniInstID id) {
    auto it = IC_map_.find(id);
    if (it != IC_map_.end()) {
      inststr2id_.erase(it->second.inst_str);
      IC_map_.erase(it);
    }
  }

  // 删除InstrumentComponent（通过inst_str）
  void EraseByInstStr(const std::string &inst_str) {
    auto it = inststr2id_.find(inst_str);
    if (it != inststr2id_.end()) {
      IC_map_.erase(it->second);
      inststr2id_.erase(it);
    }
  }

  // 获取当前InstrumentComponent数量
  size_t size() const { return IC_map_.size(); }

  // 验证是否已经创建过（通过uni_id或inst_str）
  bool Exists(const InstrumentComponent &comp) const {
    // 只要uni_id或inst_str有一个已存在就认为已创建过
    if (IC_map_.find(comp.uni_id) != IC_map_.end())
      return true;
    if (inststr2id_.find(comp.inst_str) != inststr2id_.end())
      return true;
    return false;
  }

  // 重载：通过uni_id判断是否已存在
  bool Exists(UniInstID id) const { return IC_map_.find(id) != IC_map_.end(); }

public:
  std::unordered_map<UniInstID, InstrumentComponent> IC_map_;
  std::unordered_map<std::string, UniInstID> inststr2id_;
};

struct ob_side_data {
  double price;
  double amount;
};

struct depth_data {
  double price;
  double depth;
};

struct order_data {
  double price;
  double quantity;
  side s = side::UNKNOWN;
  double margin = 0;
  double EFU = 0;
};

struct fetch_orders_data {
  double margin;
  double costs_opp_sum_max;
  std::tuple<double, std::unordered_map<data::UniInstID, double>>
      searched_result;
  std::unordered_map<UniInstID, std::vector<order_data>>
      place_order_points_dict;
  std::unordered_map<UniInstID, std::vector<order_data>>
      cancel_order_points_dict;
  double cost_sum = 0;
  double rest_sum = 0;
  double transfer_sum = 0;
  std::unordered_map<UniInstID, bool> flags_use_vol;
};

struct insert_orders_data {
  bool flag_taker = false;
  bool flag_insert = false;
  double margin;
};

struct CuscoreParams {
  // 2 / (54000(3h) + 1)
  static constexpr double alpha_slow = 0.0000370364;
  // 2 / (9000(0.5h) + 1)
  static constexpr double alpha_mid = 0.00022219753360737697;
  // 2 / (900(3m) + 1)
  static constexpr double alpha_fast = 0.0022197558268590455;
  bool flag_cuscore = false;
  double threshold_slow = 1000;
  double threshold_mid = 1000;
  double threshold_fast = 1000;
  std::vector<double> coefs_t_slow;
  std::vector<double> coefs_t_mid;
  std::vector<double> coefs_t_fast;
  int span_slow = 0;
  int span_mid = 0;
  int span_fast = 0;
};

struct cuscore_data {
  double ema_bid_slow = -1;
  double ema_ask_slow = -1;
  double ema_bid_mid = -1;
  double ema_ask_mid = -1;
  double ema_bid_fast = -1;
  double ema_ask_fast = -1;
  double sigma_bid = 1e-9;
  double sigma_ask = 1e-9;
  CircularBuffer<54000, 1> diffs_bid_slow;
  CircularBuffer<54000, 1> diffs_ask_slow;
  CircularBuffer<9000, 1> diffs_bid_mid;
  CircularBuffer<9000, 1> diffs_ask_mid;
  CircularBuffer<900, 1> diffs_bid_fast;
  CircularBuffer<900, 1> diffs_ask_fast;
  double cuscore_bid_slow = 0;
  double cuscore_ask_slow = 0;
  double cuscore_bid_mid = 0;
  double cuscore_ask_mid = 0;
  double cuscore_bid_fast = 0;
  double cuscore_ask_fast = 0;
  bool cuscore_bid_flag_fast = false;
  bool cuscore_ask_flag_fast = false;
  bool cuscore_bid_flag_mid = false;
  bool cuscore_ask_flag_mid = false;
  bool cuscore_bid_flag_slow = false;
  bool cuscore_ask_flag_slow = false;
  bool cuscore_bid_flag = false;
  bool cuscore_ask_flag = false;
  int cuscore_cnt = 1;
};

struct slope_data {
  CircularBuffer<108000, 2> fps_begin;
  double alpha = 0.0006664445;
  double alpha_vol = 0.00000309;
  CircularBuffer<9000, 2> fps_fast_begin;
  double alpha_fast = 0.0002222;
  double alpha_vol_fast = 0.00003704;
  double factor_min;
  double factor_max;
  double k;
  double log_ret_bid = 0;
  double log_ret_ask = 0;
  double log_ret_fast_bid = 0;
  double log_ret_fast_ask = 0;
  double log_ret_vol_bid = 0;
  double log_ret_vol_ask = 0;
  double log_ret_vol_fast_bid = 0;
  double log_ret_vol_fast_ask = 0;
  double slope_bid = 1;
  double slope_ask = 1;
  double factor_bid = 1;
  double factor_ask = 1;
  double slope_fast_bid = 1;
  double slope_fast_ask = 1;
  double factor_fast_bid = 1;
  double factor_fast_ask = 1;
  int slope_cnt = 0;
  int slope_fast_cnt = 0;
};

struct vol_data {
  double vol_bid_50ms = -1;
  double vol_ask_50ms = -1;
  double bid_high = -1;
  double bid_low = 1e7;
  double ask_high = -1;
  double ask_low = 1e7;
  double fp_bid = -1;
  double fp_ask = -1;
  double ema_bid_fast = -1;
  double ema_ask_fast = -1;
  double ema_bid_mid = -1;
  double ema_ask_mid = -1;
  double ema_bid_slow = -1;
  double ema_ask_slow = -1;
  double ema_diffOb_bid = -1;
  double ema_diffOb_ask = -1;
  double ema_diffOb_bid2 = 0;
  double ema_diffOb_ask2 = 0;
  double cnt_diff_plus = 0;
  double cnt_diff_minus = 0;
  double vol_bid_hl = 0;
  double vol_ask_hl = 0;
  double vol_rate_bid_hl = 0;
  double vol_rate_ask_hl = 0;
  double vol_bid_fp = -1;
  double vol_ask_fp = -1;
  double vol_bid_fp_fast = -1;
  double vol_ask_fp_fast = -1;
  double vol_bid_fp_slow = -1;
  double vol_ask_fp_slow = -1;
  double vol_rate_bid_fp = -1;
  double vol_rate_ask_fp = -1;
  int vol_cnt = 0;
  int64_t ts = 0;
  int ema_cnt = 0;
  double fp_bid_slope_begin = -1;
  double fp_bid_slope_end = -1;
  double slope_bid = 0;
  double fp_ask_slope_begin = -1;
  double fp_ask_slope_end = -1;
  double slope_ask = 0;
  int slope_cnt = 0;
  data::cuscore_data cuscore;
  data::slope_data slope;
  int64_t ts_up_diff_too_large = 0;
  int64_t ts_down_diff_too_large = 0;

  // ===== FP eval-aligned gating stats (for live strategy) =====
  // side-aware executable edge:
  // - s_bid_bps = (log(fp_bid) - log(ob_bid)) * 1e4
  // - s_ask_bps = (log(ob_ask) - log(fp_ask)) * 1e4
  // s_bps / abs_s_bps keep compatibility as combined diagnostics.
  double s_bid_bps = std::numeric_limits<double>::quiet_NaN();
  double s_ask_bps = std::numeric_limits<double>::quiet_NaN();
  double s_bps = std::numeric_limits<double>::quiet_NaN();
  double abs_s_bps = std::numeric_limits<double>::quiet_NaN();
  // bid/ask thresholds are applied to their own side-aware edge.
  double thr_bid_bps = std::numeric_limits<double>::quiet_NaN();
  double thr_ask_bps = std::numeric_limits<double>::quiet_NaN();
};

struct operation_data {
  bool first_create = true;
  bool cancel_flag = false;
  bool negative_flag = false;
  bool margin_flag = false;
  bool part_traded = false;
  bool unknown_order_flag = false;
  bool drift_delay = false;
  int64_t neg_fail_ts = 0;
  int64_t create_ts = 0;
  int64_t create_ts_const = 0;
  int64_t rl_ts = 0;
  int64_t cancel_ts = 0;
  int64_t init_ts = 0;
  int operate_times = 0;
  int callback = 0;
  int callback_cancel_times = 0;
  double margin = 0;
  int level = 0;
  double EFU = 0;
};

using SecurityPosition = nova::trade::SecurityPosition;
using OrderTp = nova::trade::NovaOrder;

struct OrderManager {
  InstrumentId inst_;
  SecurityPosition *position_;
  std::set<const OrderTp *> left_order_;
  std::unordered_map<uint32_t, data::operation_data> operation_flags;
  int loop_cnt = 0;
  bool send_cancelled = false;
  bool buy_flag = true;
  double last_price = 0.0;
  double last_net_position = 0.0;
  OrderManager(const InstrumentId &inst, SecurityPosition *posi)
      : inst_(inst), position_(posi) {
    if (position_ == nullptr)
      throw std::runtime_error("null posi");
  }
};

struct pnl_data {
  std::unordered_map<currency, double> begin_prices;
  std::unordered_map<currency, double> end_prices;
  std::unordered_map<currency, double> begin_balances;
  std::unordered_map<currency, double> end_balances;
  double begin_usd = 0;
  double begin_hedge_usdt = 0;
  double end_usd = 0;
  double pnl;
  double hedge_pnl_static;
  double pnl_static_hedge;
  double hedge_pnl_dynamic;
  double pnl_dynamic_hedge;
  double pnl_no_operation;
  bool update = false;
  std::unordered_map<currency, double> balances_default;
};

struct hedge_position_data {
  double size;
  double average_price = 0;
  double realized_profit = 0;
  my_trade_data trade;
};

// log_fp_only 回测时每 200ms 一拍收集，1 天最多 432000 拍；按 pair 存，分位数从
// pair_configs 按 pair 读
struct PairStatsComponent {
  static constexpr size_t kMaxVolTicks = 432000u; // 1 day @ 200ms
  std::array<double, kMaxVolTicks> s_bid_bps{};
  std::array<double, kMaxVolTicks> s_ask_bps{};
  std::array<double, kMaxVolTicks> s_bps{};
  std::array<double, kMaxVolTicks> depth5_volume_bid{};
  std::array<double, kMaxVolTicks> depth5_volume_ask{};
};

struct PairStats {
  static constexpr size_t kMaxVolTicks = 432000u;
  size_t tick_len = 0;
  std::unordered_map<UniInstID, PairStatsComponent> per_inst;
};

struct snapshot_data {
  int valid_level_begin = 0;
  int valid_level_end = 100;
  std::vector<int> valid_levels;
  std::array<std::array<double, 2>, 101> snapshot;
  int64_t timestamp;
};

struct depths_data {
  std::array<std::array<double, 2>, 100> bids;
  std::array<std::array<double, 2>, 100> asks;
  int64_t server_ts = 0;
  int64_t local_ts = 0;
  bool valid;
  int8_t ticker_len;
  double taker_fee = 0;
  double tick_size = 0;
  int64_t sequence_num = 0;
  uint16_t reserved;
  double ema_bidv = 0;
  double ema_askv = 0;
  int ema_cnt = 0;
  bool verbose = false;
  std::array<double, 2> wp_prev = {0, 0}; // 前一刻 wp，用于 EMA 平滑
  PairConfig PC;
};

struct ib_params {
  static constexpr double premium_multi = 3.333349945622288e-6;
  static constexpr int thre_premium = 299998;

  // 根据货币类型获取 beta 值
  static double get_beta(currency c) {
    static const std::unordered_map<currency, double> beta_map = {
        {currency::AUD, 0.9712039753732337},
        {currency::CAD, 0.9627280202005574},
        {currency::CHF, 0.9905430450517199},
        {currency::EUR, 0.9803466608391975},
        {currency::GBP, 0.966426101800912},
        {currency::PAXG, 0.986307134227719},
    };
    auto it = beta_map.find(c);
    return (it != beta_map.end()) ? it->second
                                  : 0.9803466608391975; // 默认使用 EUR 的值
  }

  // 根据货币类型获取 intercept 值
  static double get_intercept(currency c) {
    static const std::unordered_map<currency, double> intercept_map = {
        {currency::AUD, 6.070314953834796e-06},
        {currency::CAD, 9.846054367611409e-05},
        {currency::CHF, 7.993242517393398e-06},
        {currency::EUR, 1.3805460675771192e-05},
        {currency::GBP, 3.2718915256866906e-05},
        {currency::PAXG, -9.232051221952576e-05},
    };
    auto it = intercept_map.find(c);
    return (it != intercept_map.end())
               ? it->second
               : 1.3805460675771192e-5; // 默认使用 EUR 的值
  }

  // 根据货币字符串获取 beta 值（兼容现有代码）
  static double get_beta(const std::string &currency_str) {
    currency c = str_to_currency(currency_str);
    return get_beta(c);
  }

  // 根据货币字符串获取 intercept 值（兼容现有代码）
  static double get_intercept(const std::string &currency_str) {
    currency c = str_to_currency(currency_str);
    return get_intercept(c);
  }

  // 根据货币类型获取溢价上下边界 [下界, 上界]
  // 返回格式: [lower_bound, upper_bound]
  // 买的溢价不能大于 upper_bound, 卖的溢价不能小于 lower_bound
  static std::array<double, 2> get_premium_limits(currency c) {
    static const std::unordered_map<currency, std::array<double, 2>>
        limits_map = {
            {currency::AUD, {-0.003332, 0.001025}},
            {currency::CAD, {-0.001724, 0.002761}},
            {currency::CHF, {-0.001802, 0.001228}},
            {currency::EUR, {-0.00076, 0.000523}},
            {currency::GBP, {-0.000809, 0.000231}},
            {currency::PAXG, {-0.01545, -0.004096}},
        };
    auto it = limits_map.find(c);
    return (it != limits_map.end())
               ? it->second
               : std::array<double, 2>{-0.00076, 0.000523}; // 默认使用 EUR 的值
  }

  // 根据货币字符串获取溢价上下边界（兼容现有代码）
  static std::array<double, 2>
  get_premium_limits(const std::string &currency_str) {
    currency c = str_to_currency(currency_str);
    return get_premium_limits(c);
  }

  // 获取溢价下界（卖的溢价不能小于此值）
  static double get_premium_lower_bound(currency c) {
    return get_premium_limits(c)[0];
  }

  // 获取溢价上界（买的溢价不能大于此值）
  static double get_premium_upper_bound(currency c) {
    return get_premium_limits(c)[1];
  }

  // 根据货币字符串获取溢价下界（兼容现有代码）
  static double get_premium_lower_bound(const std::string &currency_str) {
    return get_premium_limits(currency_str)[0];
  }

  // 根据货币字符串获取溢价上界（兼容现有代码）
  static double get_premium_upper_bound(const std::string &currency_str) {
    return get_premium_limits(currency_str)[1];
  }

  // 限制溢价在边界范围内
  // 如果 premium 在 [lower_bound, upper_bound] 范围内，返回 premium
  // 如果 premium < lower_bound，返回 lower_bound
  // 如果 premium > upper_bound，返回 upper_bound
  static double clamp_premium(currency c, double premium) {
    const auto limits = get_premium_limits(c);
    const double lower_bound = limits[0];
    const double upper_bound = limits[1];
    if (premium < lower_bound) {
      return lower_bound;
    } else if (premium > upper_bound) {
      return upper_bound;
    } else {
      return premium;
    }
  }

  // 根据货币字符串限制溢价在边界范围内（兼容现有代码）
  static double clamp_premium(const std::string &currency_str, double premium) {
    currency c = str_to_currency(currency_str);
    return clamp_premium(c, premium);
  }
};

struct bbo_data {
  std::array<double, 2> bid;
  std::array<double, 2> ask;
  int64_t server_ts;
  int64_t local_ts;
  bool valid = false;
  int8_t ticker_len;
  double taker_fee = 0;
  double tick_size = 0;
  double bid_ema_slow = 0;
  double ask_ema_slow = 0;
  double bid_ema_fast = 0;
  double ask_ema_fast = 0;
  double premium_bid = -1;
  double premium_ask = -1;
  int premium_cnt = 0;
  double ema_bidv = 0;
  double ema_askv = 0;
  int ema_cnt = 0;
  int64_t premium_ts = 0;
  ib_params ib_param;
  bool verbose = false;
  std::array<double, 2> wp_prev = {0, 0}; // 前一刻 wp，用于 EMA 平滑
  PairConfig PC;
};

struct bar_data {
  double open;
  double high;
  double low;
  double close;
  int64_t server_ts;
  int64_t local_ts;
  bool valid = false;
  int8_t ticker_len;
  double taker_fee = 0;
  double tick_size = 0;
  double premium_bid = -1;
  double premium_ask = -1;
  int premium_cnt = 0;
};

struct ExtraInfo {
  double last_update_qty = 0;
};

struct FeatureManager {
  std::vector<std::vector<double>> feature_value_cache;
};

enum VolatilityMethod : int8_t {
  METHOD_FP_MAX_MIN = 0,
  METHOD_FP_MAX_MIN_SHARED = 1,
  METHOD_FP_DIFF = 2,
  METHOD_FP_DIFF_SHARED = 3,
  METHOD_UNKNOWN
};

inline data::VolatilityMethod get_vol_method(const std::string &s) {
  if (s == "fp_diff")
    return data::VolatilityMethod::METHOD_FP_DIFF;
  if (s == "fp_diff_shared")
    return data::VolatilityMethod::METHOD_FP_DIFF_SHARED;
  if (s == "fp_max_min")
    return data::VolatilityMethod::METHOD_FP_MAX_MIN;
  if (s == "fp_max_min_shared")
    return data::VolatilityMethod::METHOD_FP_MAX_MIN_SHARED;
  return data::VolatilityMethod::METHOD_UNKNOWN;
};

struct BatchOrderParam {
  NOVA_SIDE_TYPE side;
  double place_price;
  double place_quantity;
  double margin;
};

struct Setting {
  double balance_quantity;
  double max_percentage;
  double fp_adjust_1;
  double fp_adjust_2;
  double balance_adjust_1;
  double balance_adjust_2;
  double slop_1;
  double slop_2;
  double spread_1;
  double spread_2;
  double sell_open_slop;
  double buy_open_slop;
  double sell_close_slop;
  double buy_close_slop;
  double spread;
  double sell_close_extension;
  double buy_close_extension;
  double credit;
  double collateral_haircut;
  double customer_balance;
};

template <typename T>
inline auto LoadSettingMap(const ryml::ConstNodeRef &root,
                           const std::vector<T> &keys) {
  using KeyType = T;
  using ReturnMap = std::unordered_map<KeyType, data::Setting>;
  ReturnMap result;

  for (const auto &key : keys) {
    std::string yaml_key;
    if constexpr (std::is_same_v<T, std::string>) {
      yaml_key = key;
    } else if constexpr (std::is_same_v<T, data::currency>) {
      yaml_key = sum_util::StrUpper(data::get_currency_name(key));
    } else {
      static_assert(sizeof(T) == 0, "Unsupported key type in LoadSettingMap");
    }

    c4::csubstr ckey = c4::to_csubstr(yaml_key);
    if (!root.has_child(ckey)) {
      std::cout << "⚠️ YAML 中未找到 key: " << yaml_key << "\n";
      continue;
    }

    const auto node = root[ckey];
    if (!node.is_map()) {
      std::cout << "⚠️ 节点不是 map: " << yaml_key << "\n";
      continue;
    }

    data::Setting s;
    auto get = [&](const char *name, double &dst) {
      if (node.has_child(name))
        node[name] >> dst;
    };

    get("balance_quantity", s.balance_quantity);
    get("max_percentage", s.max_percentage);
    get("fp_adjust_1", s.fp_adjust_1);
    get("fp_adjust_2", s.fp_adjust_2);
    get("balance_adjust_1", s.balance_adjust_1);
    get("balance_adjust_2", s.balance_adjust_2);
    get("slop_1", s.slop_1);
    get("slop_2", s.slop_2);
    get("spread_1", s.spread_1);
    get("spread_2", s.spread_2);
    get("sell_open_slop", s.sell_open_slop);
    get("buy_open_slop", s.buy_open_slop);
    get("sell_close_slop", s.sell_close_slop);
    get("buy_close_slop", s.buy_close_slop);
    get("spread", s.spread);
    get("sell_close_extension", s.sell_close_extension);
    get("buy_close_extension", s.buy_close_extension);
    get("credit", s.credit);
    get("collateral_haircut", s.collateral_haircut);
    get("customer_balance", s.customer_balance);

    result.emplace(key, std::move(s));
  }

  return result;
}

inline std::ostream &operator<<(std::ostream &os, const Setting &s) {
  os << "{";
  os << "balance_quantity=" << s.balance_quantity << ", ";
  os << "max_percentage=" << s.max_percentage << ", ";
  os << "fp_adjust_1=" << s.fp_adjust_1 << ", ";
  os << "fp_adjust_2=" << s.fp_adjust_2 << ", ";
  os << "balance_adjust_1=" << s.balance_adjust_1 << ", ";
  os << "balance_adjust_2=" << s.balance_adjust_2 << ", ";
  os << "slop_1=" << s.slop_1 << ", ";
  os << "slop_2=" << s.slop_2 << ", ";
  os << "spread_1=" << s.spread_1 << ", ";
  os << "spread_2=" << s.spread_2 << ", ";
  os << "sell_open_slop=" << s.sell_open_slop << ", ";
  os << "buy_open_slop=" << s.buy_open_slop << ", ";
  os << "sell_close_slop=" << s.sell_close_slop << ", ";
  os << "buy_close_slop=" << s.buy_close_slop << ", ";
  os << "spread=" << s.spread << ", ";
  os << "sell_close_extension=" << s.sell_close_extension << ", ";
  os << "buy_close_extension=" << s.buy_close_extension << ", ";
  os << "credit=" << s.credit << ", ";
  os << "collateral_haircut=" << s.collateral_haircut << ", ";
  os << "customer_balance=" << s.customer_balance;
  os << "}";
  return os;
}

struct BalanceMonitor {
  double avg_cost = 0.0;
  double position = 0.0;
  double realized_pnl = 0.0;
  double unrealized_pnl = 0.0;
  bool flag_taker = false;
  uint32_t id_taker = 0;
  int64_t ts_taker = 0;
  int cnt_diff_down = 0;
  int cnt_diff_up = 0;
};

struct BalanceManager {
  double balance_live;
  double balance_query;
  double balance_default;
  double balance_neg_taker;
  double open_used = 0;
  double open_will_transfer = 0;
  double will_open_cost = 0;
  BalanceMonitor balance_monitor;
};

struct delay_data {
  double delay_sum = 0.0;
  double delay_mean = 0.0;
  int64_t delay_cnt = 0;
  double delay_max = 0.0;
  double delay_min = 1e9;
  bool verbose = false;

  // Static helper function to create delay_map key from exchange and inst_type
  // Format: (exchange << 8) | inst_type
  static uint16_t make_key(NOVA_EXCHANGE_TYPE exchange,
                           NOVA_COIN_INST_TYPE inst_type) {
    return (static_cast<uint16_t>(exchange) << 8) |
           static_cast<uint16_t>(inst_type);
  }

  // Static helper function to extract exchange from delay_map key
  static NOVA_EXCHANGE_TYPE get_exchange(uint16_t key) {
    return static_cast<NOVA_EXCHANGE_TYPE>(key >> 8);
  }

  // Static helper function to extract inst_type from delay_map key
  static NOVA_COIN_INST_TYPE get_inst_type(uint16_t key) {
    return static_cast<NOVA_COIN_INST_TYPE>(key & 0xFF);
  }
};

struct trades_data {
  static constexpr int64_t kFreqUs = 200000; // 200ms
  static constexpr int kZscoreWindow = 150;
  inline static std::vector<int> s_window_steps = {1, 25, 100, 300, 900, 3000};

  bool has_trade = false;
  int64_t current_bin_us = -1;
  int64_t last_trade_ts_ns = 0;
  double pending_signed_notional = 0.0;
  double pending_total_notional = 0.0;
  std::deque<std::pair<double, double>> closed_bins_queue;
  std::deque<std::pair<double, double>>
      bins; // [signed_notional, total_notional]
  std::vector<double> tfi_last;
  std::vector<std::deque<double>> ratio_hist;
  std::vector<double> ratio_sum;
  std::vector<double> ratio_sumsq;

  static constexpr std::size_t kMaxBins = 3600; // 覆盖到 600s 窗口

  inline trades_data() { ensure_scale_storage(); }

  inline static void configure_window_steps(const std::vector<int> &steps) {
    if (steps.empty())
      return;
    std::vector<int> cleaned;
    cleaned.reserve(steps.size());
    for (int s : steps) {
      cleaned.push_back(std::max(1, s));
    }
    s_window_steps = cleaned;
  }

  inline static const std::vector<int> &window_steps() {
    return s_window_steps;
  }

  inline void ensure_scale_storage() {
    const std::size_t n = window_steps().size();
    if (tfi_last.size() == n)
      return;
    tfi_last.assign(n, 0.0);
    ratio_hist.assign(n, std::deque<double>{});
    ratio_sum.assign(n, 0.0);
    ratio_sumsq.assign(n, 0.0);
  }

  inline static int side_sign(NOVA_SIDE_TYPE side) {
    if (side == NOVA_SIDE_BUY)
      return 1;
    if (side == NOVA_SIDE_SELL)
      return -1;
    return 0;
  }

  inline void push_bin(double signed_notional, double total_notional) {
    bins.emplace_back(signed_notional, total_notional);
    if (bins.size() > kMaxBins) {
      bins.pop_front();
    }
    update_tfi_last();
  }

  inline void update_tfi_last() {
    ensure_scale_storage();
    if (bins.empty()) {
      std::fill(tfi_last.begin(), tfi_last.end(), 0.0);
      return;
    }
    for (std::size_t i = 0; i < window_steps().size(); ++i) {
      const int w = window_steps()[i];
      double net_cum = 0.0;
      double vol_cum = 0.0;
      int cnt = 0;
      for (auto it = bins.rbegin(); it != bins.rend() && cnt < w; ++it, ++cnt) {
        net_cum += it->first;
        vol_cum += it->second;
      }
      const double ratio = (vol_cum > 1e-12) ? (net_cum / vol_cum) : 0.0;

      auto &rh = ratio_hist[i];
      rh.push_back(ratio);
      ratio_sum[i] += ratio;
      ratio_sumsq[i] += ratio * ratio;
      if (rh.size() > static_cast<std::size_t>(kZscoreWindow)) {
        const double old = rh.front();
        rh.pop_front();
        ratio_sum[i] -= old;
        ratio_sumsq[i] -= old * old;
      }
      const double n = static_cast<double>(rh.size());
      const double mean = (n > 0.0) ? (ratio_sum[i] / n) : 0.0;
      double var = (n > 0.0) ? (ratio_sumsq[i] / n - mean * mean) : 0.0;
      if (var < 1e-12)
        var = 1.0;
      const double std = std::sqrt(var);
      tfi_last[i] = ratio / std;
    }
  }

  inline void add_trade(int64_t ts_ns, NOVA_SIDE_TYPE side, double price,
                        double qty) {
    if (!std::isfinite(price) || !std::isfinite(qty) || price <= 0.0 ||
        qty <= 0.0)
      return;
    const int sign = side_sign(side);
    if (sign == 0)
      return;

    has_trade = true;
    last_trade_ts_ns = ts_ns;
    const int64_t ts_us = ts_ns / 1000;
    const int64_t bin_us = (ts_us / kFreqUs) * kFreqUs;
    const double notional = price * qty;

    if (current_bin_us < 0) {
      current_bin_us = bin_us;
      pending_signed_notional = sign * notional;
      pending_total_notional = notional;
      return;
    }

    if (bin_us == current_bin_us) {
      pending_signed_notional += sign * notional;
      pending_total_notional += notional;
      return;
    }

    // 只做累计，不在 trade 事件里更新 TFI；TFI 由 200ms 模块统一触发。
    while (current_bin_us < bin_us) {
      closed_bins_queue.emplace_back(pending_signed_notional,
                                     pending_total_notional);
      pending_signed_notional = 0.0;
      pending_total_notional = 0.0;
      current_bin_us += kFreqUs;
    }
    pending_signed_notional += sign * notional;
    pending_total_notional += notional;
  }

  inline void advance_to_200ms_tick(int64_t now_ts_ns) {
    if (!has_trade || current_bin_us < 0)
      return;
    const int64_t now_us = now_ts_ns / 1000;
    const int64_t target_bin_us = (now_us / kFreqUs) * kFreqUs;
    while (current_bin_us < target_bin_us) {
      closed_bins_queue.emplace_back(pending_signed_notional,
                                     pending_total_notional);
      pending_signed_notional = 0.0;
      pending_total_notional = 0.0;
      current_bin_us += kFreqUs;
    }
    while (!closed_bins_queue.empty()) {
      const auto b = closed_bins_queue.front();
      closed_bins_queue.pop_front();
      push_bin(b.first, b.second);
    }
  }

  inline std::vector<double> get_tfi(int64_t now_ts_ns,
                                     int64_t stale_ns) const {
    if (!has_trade || now_ts_ns - last_trade_ts_ns > stale_ns)
      return std::vector<double>(window_steps().size(), 0.0);
    return tfi_last;
  }

  inline std::size_t closed_queue_size() const {
    return closed_bins_queue.size();
  }
  inline std::size_t bins_size() const { return bins.size(); }
  inline std::pair<double, double> pending_notional() const {
    return {pending_signed_notional, pending_total_notional};
  }
  inline std::pair<double, double> last_closed_notional() const {
    if (bins.empty())
      return {0.0, 0.0};
    return bins.back();
  }
  inline double latest_ratio(int scale_idx) const {
    if (scale_idx < 0 || scale_idx >= static_cast<int>(window_steps().size()) ||
        bins.empty())
      return 0.0;
    const int w = window_steps()[scale_idx];
    double net_cum = 0.0;
    double vol_cum = 0.0;
    int cnt = 0;
    for (auto it = bins.rbegin(); it != bins.rend() && cnt < w; ++it, ++cnt) {
      net_cum += it->first;
      vol_cum += it->second;
    }
    return (vol_cum > 1e-12) ? (net_cum / vol_cum) : 0.0;
  }
  inline double zscore_std(int scale_idx) const {
    if (scale_idx < 0 || scale_idx >= static_cast<int>(window_steps().size()))
      return 1.0;
    const auto &rh = ratio_hist[scale_idx];
    const double n = static_cast<double>(rh.size());
    if (n <= 0.0)
      return 1.0;
    const double mean = ratio_sum[scale_idx] / n;
    double var = ratio_sumsq[scale_idx] / n - mean * mean;
    if (var < 1e-12)
      var = 1.0;
    return std::sqrt(var);
  }
  inline std::size_t num_scales() const { return window_steps().size(); }
};

struct InstrumentData {
  int64_t global_ts = 0;
  data::abnormal_status abnormal_status = data::abnormal_status::NORMAL;
  bool abnormal_cancel_all = false;
  InstrumentManager IM;
  std::unordered_map<UniInstID, depths_data> depth_map;
  std::unordered_map<UniInstID, bbo_data> bbo_map;
  std::unordered_map<UniInstID, bar_data> bar_map;
  std::unordered_map<UniInstID, vol_data> vol_map;
  std::unordered_map<UniInstID, OrderManager> order_map;
  std::unordered_map<UniInstID, insert_orders_data> insert_order_map;
  std::unordered_map<UniInstID, OrderManager> swap_order_map;
  std::unordered_map<UniInstID, trades_data> trade_map;
  std::vector<UniInstID> trading_ids;
  std::vector<UniInstID> turnover_ids;
  std::unordered_map<UniInstID, bool> turnover_map;
  std::unordered_map<data::currency, double> cost_map;
  std::unordered_map<uint16_t, delay_data>
      delay_map; // key = (exchange << 8) | inst_type
};

#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

struct TailFillModel {
  // 每段字段索引（与你 CSV 扩展后的列一致）
  // [0]=slope, [1]=intercept, [2]=end, [3]=ten_b, [4]=k, [5]=prefix_end
  std::vector<std::vector<double>> pp;
  double eps_k = 1e-12;
  bool extrapolate = true; // x>last_end 时是否用最后一段外推

  explicit TailFillModel(const std::vector<std::vector<double>> &prob_params,
                         double eps = 1e-12, bool extrap = true)
      : pp(std::move(prob_params)), eps_k(eps), extrapolate(extrap) {}

  inline int seg_idx(double x) const {
    // 返回第一个 end[i] >= x 的 i；若 x>last_end，返回 last
    const int n = (int)pp.size();
    if (n <= 0)
      return -1;
    if (x <= pp[0][2])
      return 0;
    if (x > pp[n - 1][2])
      return n - 1;

    // lower_bound on end
    int lo = 0, hi = n - 1;
    while (lo < hi) {
      int mid = (lo + hi) >> 1;
      if (pp[mid][2] >= x)
        hi = mid;
      else
        lo = mid + 1;
    }
    return lo;
  }

  static inline double int_seg(double ten_b, double k, double A, double B,
                               double eps_k) {
    // ∫_A^B ten_b*exp(k*x) dx, stable with expm1
    if (B <= A)
      return 0.0;
    if (std::abs(k) < eps_k)
      return ten_b * (B - A);
    const double base = ten_b * std::exp(k * A);
    return base * (std::expm1(k * (B - A)) / k);
  }

  inline double prefix(double x) const {
    // F(x)=∫_0^x c(u) du
    if (x <= 0.0)
      return 0.0;
    const int n = (int)pp.size();
    if (n <= 0)
      return 0.0;

    const double last_end = pp[n - 1][2];
    if (x > last_end) {
      if (!extrapolate)
        x = last_end;
    }

    const int i = seg_idx(x);
    if (i < 0)
      return 0.0;

    const double seg_start = (i == 0 ? 0.0 : pp[i - 1][2]);
    const double prefix_before = (i == 0 ? 0.0 : pp[i - 1][5]);

    const double ten_b = pp[i][3];
    const double k = pp[i][4];

    double res = prefix_before + int_seg(ten_b, k, seg_start, x, eps_k);

    // 若外推且 x 原本 > last_end，则再补一段(last_end, x) 的最后段积分
    if (extrapolate && x < 0.0) {
    } // no-op
    return res;
  }

  inline double integral_c_day(double A, double B) const {
    // ∫_A^B c(u) du (单位：USD/day)
    if (B <= A)
      return 0.0;
    if (B <= 0.0)
      return 0.0;
    if (A < 0.0)
      A = 0.0;
    return prefix(B) - prefix(A);
  }

  inline double expected_fill_usd(double depth, double quantity, double vp,
                                  double H_sec, double g_scale = 1.0) const {
    // depth, quantity 为 base；vp 为 USD/base
    // q_usd = depth*vp, L_usd = quantity*vp
    if (H_sec <= 0.0 || quantity <= 0.0 || vp <= 0.0)
      return 0.0;

    const double q_usd = depth * vp;
    const double L_usd = quantity * vp;

    const double day_fill =
        integral_c_day(q_usd, q_usd + L_usd) * g_scale; // USD/day
    return day_fill * (H_sec / 86400.0);                // USD in H
  }

  // 从 EFU 反推 quantity（使用二分法）
  inline double quantity_from_efu(double depth, double efu, double vp,
                                  double H_sec, double g_scale = 1.0,
                                  double tol = 1e-6, int max_iter = 100) const {
    // depth, quantity 为 base；vp 为 USD/base
    // q_usd = depth*vp, L_usd = quantity*vp
    if (H_sec <= 0.0 || efu <= 0.0 || vp <= 0.0 || depth <= 0.0)
      return 0.0;

    const double q_usd = depth * vp;
    const double target_day_fill = efu / (H_sec / 86400.0) / g_scale;

    // 二分法查找 L_usd，使得 integral_c_day(q_usd, q_usd + L_usd) =
    // target_day_fill L_usd 的下界为
    // 0，上界需要估计（可以使用一个较大的值，比如 depth 的 100 倍）
    double L_usd_low = 0.0;
    double L_usd_high = q_usd * 100.0; // 初始上界

    // 先检查上界是否足够大
    double high_integral = integral_c_day(q_usd, q_usd + L_usd_high);
    while (high_integral < target_day_fill && L_usd_high < q_usd * 10000.0) {
      L_usd_high *= 2.0;
      high_integral = integral_c_day(q_usd, q_usd + L_usd_high);
    }

    // 如果上界还不够大，返回 0
    if (high_integral < target_day_fill) {
      return 0.0;
    }

    // 二分法求解
    for (int iter = 0; iter < max_iter; ++iter) {
      double L_usd_mid = 0.5 * (L_usd_low + L_usd_high);
      double mid_integral = integral_c_day(q_usd, q_usd + L_usd_mid);

      if (std::abs(mid_integral - target_day_fill) < tol) {
        // 找到解，转换为 base quantity
        return L_usd_mid / vp;
      }

      if (mid_integral < target_day_fill) {
        L_usd_low = L_usd_mid;
      } else {
        L_usd_high = L_usd_mid;
      }

      // 检查收敛
      if (L_usd_high - L_usd_low < tol * q_usd) {
        return L_usd_mid / vp;
      }
    }

    // 达到最大迭代次数，返回中间值
    return 0.5 * (L_usd_low + L_usd_high) / vp;
  }

  inline double c_day(double x) const {
    // c(x)=ten_b*exp(k*x) （counts/day），用预计算列更快
    const int n = (int)pp.size();
    if (n <= 0)
      return 0.0;
    const int i = seg_idx(x);
    const double ten_b = pp[i][3];
    const double k = pp[i][4];
    return ten_b * std::exp(k * x);
  }

  inline double prob_at_least_once(double Q_usd, double H_sec,
                                   double g_scale = 1.0) const {
    // p = 1 - exp(-c(Q)*H/86400)
    if (H_sec <= 0.0)
      return 0.0;
    if (Q_usd < 0.0)
      Q_usd = 0.0;
    const double c = c_day(Q_usd) * g_scale;
    const double x = -(c * H_sec / 86400.0);
    return -std::expm1(x);
  }
};

struct order_cache {
  data::currency currency;
  double vp;
  double limit_quantity;
  InstrumentComponent *IC;
  snapshot_data points;
  depth_type side;
  std::vector<std::vector<double>> prob_params;
  TailFillModel model;
  double fp_cp;
  bool flag_ask;
  double balance_credit;
  double cost_limit;
  bool delta_exposure = false;
  order_cache(data::currency currency_, double vp_, double limit_quantity_,
              InstrumentComponent *IC_, snapshot_data points_, depth_type side_,
              std::vector<std::vector<double>> prob_params_, double fp_cp_,
              std::unordered_map<data::currency, double> balances_credit_,
              std::unordered_map<data::currency, double> costs_limit_)
      : currency(currency_), vp(vp_), limit_quantity(limit_quantity_), IC(IC_),
        points(points_), side(side_), prob_params(prob_params_),
        model(prob_params_), fp_cp(fp_cp_),
        balance_credit((side == depth_type::ASK)
                           ? balances_credit_.at(IC->base)
                           : balances_credit_.at(IC->quote)),
        cost_limit((side == depth_type::ASK) ? costs_limit_.at(IC->quote)
                                             : costs_limit_.at(IC->base)) {
    flag_ask = (side == depth_type::ASK);
  }
  order_cache() : model(prob_params) {}
};

struct inner_point_data {
  double quantity;
  double margin;
  depth_data DD;

  // 构造函数，直接接受 depth_data 的构造参数
  inner_point_data(double qty, double mgn, double price, double depth)
      : quantity(qty), margin(mgn), DD{price, depth} {
  } // 直接使用初始化列表构造 depth_data
};

struct inner_point_data_cache {
  std::unordered_map<UniInstID, std::vector<inner_point_data>> IPD_dict;
  std::unordered_map<data::currency, double> balances_credit;
  std::unordered_map<data::currency, double> costs_limit;
  std::unordered_map<UniInstID, order_cache> OC_dict;
};

struct volume_data {
  double volume = 0.0;
  double volume_buy = 0.0;
  double volume_sell = 0.0;
};

} // namespace data

#endif // DATA_H