#pragma once

#include "nova_api_data_type.h"
#include "nova_api_instrument.h"
#include <inttypes.h>

BEGIN_NOVA_NAMESPACE(quote)

#define FMT_I64 "%" PRId64
#define FMT_U64 "%" PRIu64
constexpr double DOUBLE_EPSILON = 1e-9;
constexpr double COIN_DOUBLE_EPSILON = DOUBLE_EPSILON;

using NovaPrice = double;
using NovaCoinPrice = double;

enum NOVA_COIN_QUOTE_TYPE : int8_t {
  NOVA_COIN_QUOTE_INIT = 0,
  NOVA_COIN_QUOTE_DEPTH = 1,
  NOVA_COIN_QUOTE_TRADE = 2,
  NOVA_COIN_QUOTE_BBO = 4,
  NOVA_COIN_QUOTE_DEPTH_LVN = 8,
  NOVA_COIN_QUOTE_BAR = 16,
  NOVA_COIN_QUOTE_VARIANT = 32,
  NOVA_COIN_QUOTE_ALL = 127
};

enum NOVA_COIN_QUOTE_VARIANT_TYPE : uint16_t {
  NOVA_COIN_QUOTE_VARIANT_INIT = 0
};

enum NOVA_COIN_QUOTE_OPTION_TYPE : uint8_t {
  NOVA_COIN_QUOTE_OPTION_INIT = 0,
};

struct NovaCoinPriceLevel {
  double price;
  double qty;
};

struct NovaCoinBar {
  InstrumentId instrument_id;
  union {
    int64_t timestamp;
    int64_t update_time;
    int64_t exch_ns;
  };
  union {
    int64_t local_ns;
    int64_t local_time;
  };

  double high;
  double low;
  double open;
  double close;
  uint64_t wap;
  uint64_t volume;
  int count;

  static NOVA_COIN_QUOTE_TYPE QuoteType() { return NOVA_COIN_QUOTE_BAR; }
  static const string TypeName() { return "Bar"; }
  static const string PrtHeader() {
    return "symbol,update_time,local_ns,"
           "high,low,open,close,wap,volume,count";
  };

  string Csv() const {
    char buff[1024];
    snprintf(buff, sizeof(buff),
             "%s," FMT_I64 "," FMT_I64 ",%.8f,%.8f,%.8f,%.8f," FMT_U64
             "," FMT_U64 ",%d",
             instrument_id.GetSymbol(), update_time, local_ns, high, low, open,
             close, wap, volume, count);
    return buff;
  }
};

struct NovaCoinBBO {
  InstrumentId instrument_id;
  union {
    int64_t timestamp;
    int64_t update_time;
    int64_t exch_ns;
  };
  union {
    int64_t local_ns;
    int64_t local_time;
  };

  double bid_price;
  double bid_qty;
  double ask_price;
  double ask_qty;

  static NOVA_COIN_QUOTE_TYPE QuoteType() { return NOVA_COIN_QUOTE_BBO; }
  static const string TypeName() { return "BBO"; }
  static const string PrtHeader() {
    return "symbol,update_time,local_ns,bid_price_bid_qty,ask_price,ask_qty";
  };
  string Csv() const {
    char buff[1024];
    snprintf(buff, sizeof(buff), "%s," FMT_U64 "," FMT_U64 ",%g,%g,%g,%g",
             instrument_id.GetSymbol(), update_time, local_ns, bid_price,
             bid_qty, ask_price, ask_qty);
    return buff;
  }
};

struct NovaCoinDepth {
  static constexpr auto MAX_PRICE_LEVEL = 25;
  InstrumentId instrument_id;

  union {
    int64_t timestamp;
    int64_t update_time;
    int64_t exch_ns;
  };
  union {
    int64_t local_ns;
    int64_t local_time;
  };

  NovaCoinPriceLevel bid[MAX_PRICE_LEVEL];
  NovaCoinPriceLevel ask[MAX_PRICE_LEVEL];

  double buy_amt;
  double sell_amt;
  double buy_qty;
  double sell_qty;
  uint16_t buy_order_count;
  uint16_t sell_order_count;

  uint16_t ob_level;
  uint16_t reserved;

  uint64_t sequence_num;
  uint64_t reserved1;

  [[deprecated("reserved")]] double avg_bid_price;
  [[deprecated("reserved")]] double avg_ask_price;

  [[deprecated("reserved")]] double weighted_bid_price;
  [[deprecated("reserved")]] double weighted_ask_price;

  uint8_t reserved0[8];

  static NOVA_COIN_QUOTE_TYPE QuoteType() { return NOVA_COIN_QUOTE_DEPTH; }
  static const string TypeName() { return "Depth"; }
  static const string PrtHeader() {
    static_assert(sizeof(NovaCoinDepth) == 118 * 8);
    static_assert(alignof(NovaCoinDepth) == 8);
    return "symbol,local_ns,"
           "bid1price,bid1qty,bid2price,bid2qty,bid3price,bid3qty,bid4price,"
           "bid4qty,bid5price,bid5qty,"
           "ask1price,ask1qty,ask2price,ask2qty,ask3price,ask3qty,ask4price,"
           "ask4qty,ask5price,ask5qty,"
           "buy_amt,sell_amt,buy_qty,sell_qty,"
           "buy_order_count,sell_order_count";
  };
  string Csv() const {
    char buff[1024];
    snprintf(buff, sizeof(buff),
             "%s," FMT_U64 "," FMT_U64 ","
             "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,"
             "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,"
             "%lf,%lf,%lf,%lf,"
             "%hu,%hu",
             instrument_id.GetSymbol(), update_time, local_ns, bid[0].price,
             bid[0].qty, bid[1].price, bid[1].qty, bid[2].price, bid[2].qty,
             bid[3].price, bid[3].qty, bid[4].price, bid[4].qty, ask[0].price,
             ask[0].qty, ask[1].price, ask[1].qty, ask[2].price, ask[2].qty,
             ask[3].price, ask[3].qty, ask[4].price, ask[4].qty, buy_amt,
             sell_amt, buy_qty, sell_qty, buy_order_count, sell_order_count);
    return buff;
  }
};

struct NovaCoinDepthLVN {
  static constexpr auto MAX_PRICE_LEVEL = 100;
  InstrumentId instrument_id;

  union {
    int64_t timestamp;
    int64_t update_time;
    int64_t exch_ns;
  };
  union {
    int64_t local_ns;
    int64_t local_time;
  };

  NovaCoinPriceLevel bid[MAX_PRICE_LEVEL];
  NovaCoinPriceLevel ask[MAX_PRICE_LEVEL];

  double buy_amt;
  double sell_amt;
  double buy_qty;
  double sell_qty;
  uint16_t buy_order_count;
  uint16_t sell_order_count;

  uint16_t ob_level;
  uint16_t reserved;

  uint64_t sequence_num;

  [[deprecated("reserved")]] double avg_bid_price;
  [[deprecated("reserved")]] double avg_ask_price;

  [[deprecated("reserved")]] double weighted_bid_price;
  [[deprecated("reserved")]] double weighted_ask_price;

  uint8_t reserved0[8];

  static NOVA_COIN_QUOTE_TYPE QuoteType() { return NOVA_COIN_QUOTE_DEPTH_LVN; }
  static const string TypeName() { return "DepthLVN"; }
  static const string PrtHeader() {
    static_assert(sizeof(NovaCoinDepth) == 118 * 8);
    static_assert(alignof(NovaCoinDepth) == 8);
    return "symbol,local_ns,"
           "bid1price,bid1qty,bid2price,bid2qty,bid3price,bid3qty,bid4price,"
           "bid4qty,bid5price,bid5qty,"
           "ask1price,ask1qty,ask2price,ask2qty,ask3price,ask3qty,ask4price,"
           "ask4qty,ask5price,ask5qty,"
           "buy_amt,sell_amt,buy_qty,sell_qty,"
           "buy_order_count,sell_order_count";
  };
  string Csv() const {
    char buff[1024];
    snprintf(buff, sizeof(buff),
             "%s," FMT_U64 "," FMT_U64 ","
             "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,"
             "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,"
             "%lf,%lf,%lf,%lf,"
             "%hu,%hu",
             instrument_id.GetSymbol(), update_time, local_ns, bid[0].price,
             bid[0].qty, bid[1].price, bid[1].qty, bid[2].price, bid[2].qty,
             bid[3].price, bid[3].qty, bid[4].price, bid[4].qty, ask[0].price,
             ask[0].qty, ask[1].price, ask[1].qty, ask[2].price, ask[2].qty,
             ask[3].price, ask[3].qty, ask[4].price, ask[4].qty, buy_amt,
             sell_amt, buy_qty, sell_qty, buy_order_count, sell_order_count);
    return buff;
  }
};

struct NovaCoinTrade {
  InstrumentId instrument_id;

  int64_t timestamp;
  union {
    int64_t local_ns;
    int64_t local_time;
  };

  double qty;
  double price;

  struct {
    uint64_t trade_id;
    uint64_t first_id;
    uint64_t last_id;
  } aggregate;

  NOVA_SIDE_TYPE side;
  uint8_t reserved0[6];

  void SetAggregate(uint64_t tradeID, uint64_t firstID, uint64_t lastID) {
    aggregate.trade_id = tradeID;
    aggregate.first_id = firstID;
    aggregate.last_id = lastID;
  }

  static NOVA_COIN_QUOTE_TYPE QuoteType() { return NOVA_COIN_QUOTE_TRADE; }
  static const string TypeName() { return "Trade"; }
  static const string PrtHeader() {
    return "symbol,timestamp,local_ns,qty,price,side,aggregate_trade_id,"
           "aggregate_first_id,aggregate_last_id";
  };
  string Csv() const {
    char buff[1024];
    snprintf(buff, sizeof(buff),
             "%s," FMT_U64 "," FMT_U64 ",%lf,%lf,%hhu," FMT_U64 "," FMT_U64
             "," FMT_U64 ",",
             instrument_id.GetSymbol(), timestamp, local_ns, qty, price, side,
             aggregate.trade_id, aggregate.first_id, aggregate.last_id);
    return buff;
  }
};

enum ContractType {
  CONTRACT_TYPE_FIX = 0,
  CONTRACT_TYPE_SPOT = 1,
};

struct InstrumentBaseInfo {
  InstrumentId instrument_id_;
  NOVA_EXCHANGE_TYPE exchange_;

  struct PriceLimit {
    double max_price;
    double min_price;
    double tick_size;
  } price_;
  struct QtyLimit {
    double max_qty;
    double min_qty;
    double step_size;
  } qty_;

  double min_notional_;

  struct OrderCountLimit {
    uint64_t max_num_orders;
    uint64_t max_algo_num_orders;
  } order_count_;

  struct Contract {
    ContractType type;
    double multi;
    double value;
  } contract_;

  struct Fee {
    double taker_fee;
    double maker_fee;
  } fee_;

  static InstrumentBaseInfo
  Create(const char *symbol, NOVA_EXCHANGE_TYPE exchange, PriceLimit price,
         QtyLimit qty, double min_notional, OrderCountLimit order_count) {
    InstrumentBaseInfo info;
    info.instrument_id_ = InstrumentId::Create(symbol, exchange);
    info.exchange_ = exchange;
    info.price_ = price;
    info.qty_ = qty;
    info.min_notional_ = min_notional;
    info.order_count_ = order_count;
    return info;
  }

  static InstrumentBaseInfo Create(const char *symbol, PriceLimit price,
                                   QtyLimit qty, double min_notional,
                                   OrderCountLimit order_count) {
    InstrumentBaseInfo info;
    info.instrument_id_ = InstrumentId::Create(symbol);
    info.exchange_ = info.instrument_id_.exchange;
    info.price_ = price;
    info.qty_ = qty;
    info.min_notional_ = min_notional;
    info.order_count_ = order_count;
    return info;
  }

  void Set(const char *symbol, NOVA_EXCHANGE_TYPE exchange, PriceLimit price,
           QtyLimit qty, double min_notional, OrderCountLimit order_count) {
    instrument_id_ = InstrumentId::Create(symbol, exchange);
    exchange_ = exchange;
    price_ = price;
    qty_ = qty;
    min_notional_ = min_notional;
    order_count_ = order_count;
  }

  void Set(const char *symbol, PriceLimit price, QtyLimit qty,
           double min_notional, OrderCountLimit order_count) {
    instrument_id_ = InstrumentId::Create(symbol);
    exchange_ = instrument_id_.exchange;
    price_ = price;
    qty_ = qty;
    min_notional_ = min_notional;
    order_count_ = order_count;
  }
};

END_NOVA_NAMESPACE(quote)