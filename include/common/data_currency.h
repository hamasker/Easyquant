#ifndef DATA_CURRENCY_H
#define DATA_CURRENCY_H

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace data {

enum class currency : int16_t {
  UNKNOWN = 0,
  USD,
  USDT,
  USDC,
  BUSD,
  BNB,
  DAI,
  EUR,
  GBP,
  CHF,
  CAD,
  AUD,
  PAXG,
  BTC,
  ETH,
  SOL,
  DOT,
  MATIC,
  DOGE,
  LTC,
  XRP,
  ADA,
  LINK,
  XMR,
  CURRENCY_MAX // 用于计数
};

static const char *currency_names[] = {
    "unknown", "usd",   "usdt", "usdc", "busd", "bnb", "dai",  "eur",
    "gbp",     "chf",   "cad",  "aud",  "paxg", "btc", "eth",  "sol",
    "dot",     "matic", "doge", "ltc",  "xrp",  "ada", "link", "xmr"};

// 稳定币
static const std::unordered_set<currency> stable_currencies = {
    currency::USDT, currency::USDC, currency::BUSD, currency::DAI};

// 外汇货币
static const std::unordered_set<currency> forex_currencies = {
    currency::USD, currency::EUR, currency::GBP, currency::CHF,
    currency::CAD, currency::AUD, currency::PAXG};

// 数字货币
static const std::unordered_set<currency> digital_currencies = {
    currency::BTC,
    currency::ETH,
    currency::BNB,
    currency::SOL,
    currency::DOT,
    currency::MATIC,
    currency::DOGE,
    currency::LTC,
    currency::XRP,
    currency::ADA,
    currency::LINK,
    currency::XMR
    // ...如有遗漏可补充
};

inline bool is_stable_currency(currency c) {
  return stable_currencies.count(c) > 0;
}
inline bool is_forex_currency(currency c) {
  return forex_currencies.count(c) > 0;
}
inline bool is_digital_currency(currency c) {
  return digital_currencies.count(c) > 0;
}

inline std::string get_currency_name(currency c) {
  int idx = static_cast<int>(c);
  if (idx < 0 || idx >= static_cast<int>(currency::CURRENCY_MAX))
    return "unknown";
  return currency_names[idx];
}

inline std::vector<std::string>
get_currency_name(const std::vector<currency> &currencies) {
  std::vector<std::string> names;
  names.reserve(currencies.size());

  for (const auto &c : currencies) {
    names.push_back(get_currency_name(c));
  }

  return names;
}

inline currency str_to_currency(const std::string &name) {
  for (int i = 0; i < static_cast<int>(currency::CURRENCY_MAX); ++i) {
    if (name == currency_names[i])
      return static_cast<currency>(i);
  }
  return currency::UNKNOWN;
}

inline std::vector<currency>
str_to_currency(const std::vector<std::string> &names) {
  std::vector<currency> currencies;
  currencies.reserve(names.size());

  for (const auto &name : names) {
    currencies.push_back(str_to_currency(name));
  }

  return currencies;
}

} // namespace data

#endif // DATA_CURRENCY_H