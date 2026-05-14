#ifndef DATA_EXCHANGE_H
#define DATA_EXCHANGE_H

#include <cstdint>
#include <string>

namespace data {

/*
 * 交易所
 */
enum class exchange : int8_t {
  UNKNOWN = 0,
  BINANCE,
  BINANCE_SWAP,
  COINBASE,
  KRAKEN,
  KRAKEN_SWAP,
  BITSTAMP,
  OKX,
  OKX_SWAP,
  GATEIO,
  GATEIO_SWAP,
  IB,
  BYBIT,
  KUCOIN,
  HYPERLIQUID,
  RPC,
  EXCHANGE_MAX // 用于计数
};

static const char *exchange_names[] = {
    "unknown",  "binance",     "binance_swap", "coinbase",
    "kraken",   "kraken_swap", "bitstamp",     "okx",
    "okx_swap", "gateio",      "gateio_swap",  "ib",
    "bybit",    "kucoin",      "hyperliquid",  "rpc"};

static const char *exchange_abbrs[] = {
    "unknown",  // UNKNOWN
    "bn",       // BINANCE
    "bns",      // BINANCE_SWAP
    "cb",       // COINBASE
    "krk",      // KRAKEN
    "krks",     // KRAKEN_SWAP
    "bt",       // BITSTAMP
    "ok",       // OKX
    "oks",      // OKX_SWAP
    "gt",       // GATEIO
    "gts",      // GATEIO_SWAP
    "idealpro", // IB
    "bybit",    // BYBIT
    "ku",       // KUCOIN
    "hl",       // HYPERLIQUID
    "rpc"       // RPC
};

inline std::string get_exchange_name(exchange e) {
  int idx = static_cast<int>(e);
  if (idx < 0 || idx >= static_cast<int>(exchange::EXCHANGE_MAX))
    return "unknown";
  return exchange_names[idx];
}

inline std::string get_exchange_abbr_name(exchange e) {
  int idx = static_cast<int>(e);
  if (idx < 0 || idx >= static_cast<int>(exchange::EXCHANGE_MAX))
    return "unknown";
  return exchange_abbrs[idx];
}

inline exchange str_to_exchange(const std::string &name) {
  for (int i = 0; i < static_cast<int>(exchange::EXCHANGE_MAX); ++i) {
    if (name == exchange_names[i])
      return static_cast<exchange>(i);
  }
  return exchange::UNKNOWN;
}

inline exchange abbr_str_to_exchange(const std::string &abbr) {
  for (int i = 0; i < static_cast<int>(exchange::EXCHANGE_MAX); ++i) {
    if (abbr == exchange_abbrs[i])
      return static_cast<exchange>(i);
  }
  return exchange::UNKNOWN;
}

} // namespace data

#endif // DATA_EXCHANGE_H
