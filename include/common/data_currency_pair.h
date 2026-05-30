#ifndef DATA_CURRENCY_PAIR_H
#define DATA_CURRENCY_PAIR_H

#include "data_currency.h"
#include "data_exchange.h"
#include "util_tools/sum_util.h"

namespace data {

enum class currency_pair : int16_t {
  UNKNOWN = 0,
  EUR_USD,
  GBP_USD,
  BTC_USDT,
  ETH_USDT,
  USD_CAD,
  USD_CHF,
  AUD_USD,
  USDT_USD,
  USDT_EUR,
  USDC_USD,
  USDC_EUR,
  USDC_USDT,
  USDT_GBP,
  USDT_CHF,
  USDT_CAD,
  USDT_AUD,
  USDC_GBP,
  USDC_AUD,
  EUR_CHF,
  EUR_GBP,
  DAI_USD,
  DAI_EUR,
  DAI_USDT,
  PAXG_USD,
  PAXG_EUR,
  BTC_USD,
  BTC_EUR,
  BTC_GBP,
  BTC_USDC,
  BTC_CAD,
  BTC_CHF,
  BTC_AUD,
  ETH_USD,
  ETH_EUR,
  ETH_BTC,
  ETH_GBP,
  ETH_USDC,
  ETH_CAD,
  ETH_CHF,
  ETH_AUD,
  SOL_USD,
  SOL_EUR,
  SOL_BTC,
  SOL_GBP,
  DOT_USD,
  DOT_EUR,
  DOT_BTC,
  DOT_USDT,
  DOT_GBP,
  MATIC_USD,
  MATIC_EUR,
  MATIC_BTC,
  EUR_BUSD,
  GBP_BUSD,
  BTC_BUSD,
  ETH_BUSD,
  BUSD_USDT,
  SOL_USDT,
  MATIC_USDT,
  USDT_USDC,
  USDT_DAI,
  DOGE_USD,
  LTC_USD,
  XRP_USD,
  ADA_USD,
  LINK_USD,
  DOGE_EUR,
  LTC_EUR,
  XRP_EUR,
  ADA_EUR,
  LINK_EUR,
  DOGE_USDT,
  LTC_USDT,
  XRP_USDT,
  ADA_USDT,
  LTC_BTC,
  XRP_BTC,
  ADA_BTC,
  LINK_BTC,
  XRP_GBP,
  LINK_USDT,
  BNB_BUSD,
  CURRENCY_PAIR_MAX // 用于计数
};

static const char *currency_pair_names[] = {
    "unknown",   "eur/usd",   "gbp/usd",   "btc/usdt", "eth/usdt",
    "usd/cad",   "usd/chf",   "aud/usd",   "usdt/usd", "usdt/eur",
    "usdc/usd",  "usdc/eur",  "usdc/usdt", "usdt/gbp", "usdt/chf",
    "usdt/cad",  "usdt/aud",  "usdc/gbp",  "usdc/aud", "eur/chf",
    "eur/gbp",   "dai/usd",   "dai/eur",   "dai/usdt", "paxg/usd",
    "paxg/eur",  "btc/usd",   "btc/eur",   "btc/gbp",  "btc/usdc",
    "btc/cad",   "btc/chf",   "btc/aud",   "eth/usd",  "eth/eur",
    "eth/btc",   "eth/gbp",   "eth/usdc",  "eth/cad",  "eth/chf",
    "eth/aud",   "sol/usd",   "sol/eur",   "sol/btc",  "sol/gbp",
    "dot/usd",   "dot/eur",   "dot/btc",   "dot/usdt", "dot/gbp",
    "matic/usd", "matic/eur", "matic/btc", "eur/busd", "gbp/busd",
    "btc/busd",  "eth/busd",  "busd/usdt", "sol/usdt", "matic/usdt",
    "usdt/usdc", "usdt/dai",  "doge/usd",  "ltc/usd",  "xrp/usd",
    "ada/usd",   "link/usd",  "doge/eur",  "ltc/eur",  "xrp/eur",
    "ada/eur",   "link/eur",  "doge/usdt", "ltc/usdt", "xrp/usdt",
    "ada/usdt",  "ltc/btc",   "xrp/btc",   "ada/btc",  "link/btc",
    "xrp/gbp",   "link/usdt", "bnb/busd"};

inline std::string get_currency_pair_name(currency_pair cp) {
  int idx = static_cast<int>(cp);
  if (idx < 0 || idx >= static_cast<int>(currency_pair::CURRENCY_PAIR_MAX))
    return "unknown";
  return currency_pair_names[idx];
}

inline currency get_currency_pair_base(currency_pair cp) {
  switch (cp) {
  case currency_pair::EUR_USD:
    return currency::EUR;
  case currency_pair::GBP_USD:
    return currency::GBP;
  case currency_pair::BTC_USDT:
    return currency::BTC;
  case currency_pair::ETH_USDT:
    return currency::ETH;
  case currency_pair::USD_CAD:
    return currency::USD;
  case currency_pair::USD_CHF:
    return currency::USD;
  case currency_pair::AUD_USD:
    return currency::AUD;
  case currency_pair::USDT_USD:
    return currency::USDT;
  case currency_pair::USDT_EUR:
    return currency::USDT;
  case currency_pair::USDC_USD:
    return currency::USDC;
  case currency_pair::USDC_EUR:
    return currency::USDC;
  case currency_pair::USDC_USDT:
    return currency::USDC;
  case currency_pair::USDT_GBP:
    return currency::USDT;
  case currency_pair::USDT_CHF:
    return currency::USDT;
  case currency_pair::USDT_CAD:
    return currency::USDT;
  case currency_pair::USDT_AUD:
    return currency::USDT;
  case currency_pair::USDC_GBP:
    return currency::USDC;
  case currency_pair::USDC_AUD:
    return currency::USDC;
  case currency_pair::EUR_CHF:
    return currency::EUR;
  case currency_pair::EUR_GBP:
    return currency::EUR;
  case currency_pair::DAI_USD:
    return currency::DAI;
  case currency_pair::DAI_EUR:
    return currency::DAI;
  case currency_pair::DAI_USDT:
    return currency::DAI;
  case currency_pair::PAXG_USD:
    return currency::PAXG;
  case currency_pair::PAXG_EUR:
    return currency::PAXG;
  case currency_pair::BTC_USD:
    return currency::BTC;
  case currency_pair::BTC_EUR:
    return currency::BTC;
  case currency_pair::BTC_GBP:
    return currency::BTC;
  case currency_pair::BTC_USDC:
    return currency::BTC;
  case currency_pair::BTC_CAD:
    return currency::BTC;
  case currency_pair::BTC_CHF:
    return currency::BTC;
  case currency_pair::BTC_AUD:
    return currency::BTC;
  case currency_pair::ETH_USD:
    return currency::ETH;
  case currency_pair::ETH_EUR:
    return currency::ETH;
  case currency_pair::ETH_BTC:
    return currency::ETH;
  case currency_pair::ETH_GBP:
    return currency::ETH;
  case currency_pair::ETH_USDC:
    return currency::ETH;
  case currency_pair::ETH_CAD:
    return currency::ETH;
  case currency_pair::ETH_CHF:
    return currency::ETH;
  case currency_pair::ETH_AUD:
    return currency::ETH;
  case currency_pair::SOL_USD:
    return currency::SOL;
  case currency_pair::SOL_EUR:
    return currency::SOL;
  case currency_pair::SOL_BTC:
    return currency::SOL;
  case currency_pair::SOL_GBP:
    return currency::SOL;
  case currency_pair::DOT_USD:
    return currency::DOT;
  case currency_pair::DOT_EUR:
    return currency::DOT;
  case currency_pair::DOT_BTC:
    return currency::DOT;
  case currency_pair::DOT_USDT:
    return currency::DOT;
  case currency_pair::DOT_GBP:
    return currency::DOT;
  case currency_pair::MATIC_USD:
    return currency::MATIC;
  case currency_pair::MATIC_EUR:
    return currency::MATIC;
  case currency_pair::MATIC_BTC:
    return currency::MATIC;
  case currency_pair::EUR_BUSD:
    return currency::EUR;
  case currency_pair::GBP_BUSD:
    return currency::GBP;
  case currency_pair::BTC_BUSD:
    return currency::BTC;
  case currency_pair::ETH_BUSD:
    return currency::ETH;
  case currency_pair::BUSD_USDT:
    return currency::BUSD;
  case currency_pair::SOL_USDT:
    return currency::SOL;
  case currency_pair::MATIC_USDT:
    return currency::MATIC;
  case currency_pair::USDT_USDC:
    return currency::USDT;
  case currency_pair::USDT_DAI:
    return currency::USDT;
  case currency_pair::DOGE_USD:
    return currency::DOGE;
  case currency_pair::LTC_USD:
    return currency::LTC;
  case currency_pair::XRP_USD:
    return currency::XRP;
  case currency_pair::ADA_USD:
    return currency::ADA;
  case currency_pair::LINK_USD:
    return currency::LINK;
  case currency_pair::DOGE_EUR:
    return currency::DOGE;
  case currency_pair::LTC_EUR:
    return currency::LTC;
  case currency_pair::XRP_EUR:
    return currency::XRP;
  case currency_pair::ADA_EUR:
    return currency::ADA;
  case currency_pair::LINK_EUR:
    return currency::LINK;
  case currency_pair::DOGE_USDT:
    return currency::DOGE;
  case currency_pair::LTC_USDT:
    return currency::LTC;
  case currency_pair::XRP_USDT:
    return currency::XRP;
  case currency_pair::ADA_USDT:
    return currency::ADA;
  case currency_pair::LTC_BTC:
    return currency::LTC;
  case currency_pair::XRP_BTC:
    return currency::XRP;
  case currency_pair::ADA_BTC:
    return currency::ADA;
  case currency_pair::LINK_BTC:
    return currency::LINK;
  case currency_pair::XRP_GBP:
    return currency::XRP;
  case currency_pair::LINK_USDT:
    return currency::LINK;
  case currency_pair::BNB_BUSD:
    return currency::BNB;
  default:
    return currency::UNKNOWN;
  }
}

inline currency get_currency_pair_quote(currency_pair cp) {
  switch (cp) {
  case currency_pair::EUR_USD:
    return currency::USD;
  case currency_pair::GBP_USD:
    return currency::USD;
  case currency_pair::BTC_USDT:
    return currency::USDT;
  case currency_pair::ETH_USDT:
    return currency::USDT;
  case currency_pair::USD_CAD:
    return currency::CAD;
  case currency_pair::USD_CHF:
    return currency::CHF;
  case currency_pair::AUD_USD:
    return currency::USD;
  case currency_pair::USDT_USD:
    return currency::USD;
  case currency_pair::USDT_EUR:
    return currency::EUR;
  case currency_pair::USDC_USD:
    return currency::USD;
  case currency_pair::USDC_EUR:
    return currency::EUR;
  case currency_pair::USDC_USDT:
    return currency::USDT;
  case currency_pair::USDT_GBP:
    return currency::GBP;
  case currency_pair::USDT_CHF:
    return currency::CHF;
  case currency_pair::USDT_CAD:
    return currency::CAD;
  case currency_pair::USDT_AUD:
    return currency::AUD;
  case currency_pair::USDC_GBP:
    return currency::GBP;
  case currency_pair::USDC_AUD:
    return currency::AUD;
  case currency_pair::EUR_CHF:
    return currency::CHF;
  case currency_pair::EUR_GBP:
    return currency::GBP;
  case currency_pair::DAI_USD:
    return currency::USD;
  case currency_pair::DAI_EUR:
    return currency::EUR;
  case currency_pair::DAI_USDT:
    return currency::USDT;
  case currency_pair::PAXG_USD:
    return currency::USD;
  case currency_pair::PAXG_EUR:
    return currency::EUR;
  case currency_pair::BTC_USD:
    return currency::USD;
  case currency_pair::BTC_EUR:
    return currency::EUR;
  case currency_pair::BTC_GBP:
    return currency::GBP;
  case currency_pair::BTC_USDC:
    return currency::USDC;
  case currency_pair::BTC_CAD:
    return currency::CAD;
  case currency_pair::BTC_CHF:
    return currency::CHF;
  case currency_pair::BTC_AUD:
    return currency::AUD;
  case currency_pair::ETH_USD:
    return currency::USD;
  case currency_pair::ETH_EUR:
    return currency::EUR;
  case currency_pair::ETH_BTC:
    return currency::BTC;
  case currency_pair::ETH_GBP:
    return currency::GBP;
  case currency_pair::ETH_USDC:
    return currency::USDC;
  case currency_pair::ETH_CAD:
    return currency::CAD;
  case currency_pair::ETH_CHF:
    return currency::CHF;
  case currency_pair::ETH_AUD:
    return currency::AUD;
  case currency_pair::SOL_USD:
    return currency::USD;
  case currency_pair::SOL_EUR:
    return currency::EUR;
  case currency_pair::SOL_BTC:
    return currency::BTC;
  case currency_pair::SOL_GBP:
    return currency::GBP;
  case currency_pair::DOT_USD:
    return currency::USD;
  case currency_pair::DOT_EUR:
    return currency::EUR;
  case currency_pair::DOT_BTC:
    return currency::BTC;
  case currency_pair::DOT_USDT:
    return currency::USDT;
  case currency_pair::DOT_GBP:
    return currency::GBP;
  case currency_pair::MATIC_USD:
    return currency::USD;
  case currency_pair::MATIC_EUR:
    return currency::EUR;
  case currency_pair::MATIC_BTC:
    return currency::BTC;
  case currency_pair::EUR_BUSD:
    return currency::BUSD;
  case currency_pair::GBP_BUSD:
    return currency::BUSD;
  case currency_pair::BTC_BUSD:
    return currency::BUSD;
  case currency_pair::ETH_BUSD:
    return currency::BUSD;
  case currency_pair::BUSD_USDT:
    return currency::USDT;
  case currency_pair::SOL_USDT:
    return currency::USDT;
  case currency_pair::MATIC_USDT:
    return currency::USDT;
  case currency_pair::USDT_USDC:
    return currency::USDC;
  case currency_pair::USDT_DAI:
    return currency::DAI;
  case currency_pair::DOGE_USD:
    return currency::USD;
  case currency_pair::LTC_USD:
    return currency::USD;
  case currency_pair::XRP_USD:
    return currency::USD;
  case currency_pair::ADA_USD:
    return currency::USD;
  case currency_pair::LINK_USD:
    return currency::USD;
  case currency_pair::DOGE_EUR:
    return currency::EUR;
  case currency_pair::LTC_EUR:
    return currency::EUR;
  case currency_pair::XRP_EUR:
    return currency::EUR;
  case currency_pair::ADA_EUR:
    return currency::EUR;
  case currency_pair::LINK_EUR:
    return currency::EUR;
  case currency_pair::DOGE_USDT:
    return currency::USDT;
  case currency_pair::LTC_USDT:
    return currency::USDT;
  case currency_pair::XRP_USDT:
    return currency::USDT;
  case currency_pair::ADA_USDT:
    return currency::USDT;
  case currency_pair::LTC_BTC:
    return currency::BTC;
  case currency_pair::XRP_BTC:
    return currency::BTC;
  case currency_pair::ADA_BTC:
    return currency::BTC;
  case currency_pair::LINK_BTC:
    return currency::BTC;
  case currency_pair::XRP_GBP:
    return currency::GBP;
  case currency_pair::LINK_USDT:
    return currency::USDT;
  case currency_pair::BNB_BUSD:
    return currency::BUSD;
  default:
    return currency::UNKNOWN;
  }
}

inline currency_pair str_to_currency_pair(const std::string &name) {
  for (int i = 0; i < static_cast<int>(currency_pair::CURRENCY_PAIR_MAX); ++i) {
    if (name == currency_pair_names[i])
      return static_cast<currency_pair>(i);
  }
  return currency_pair::UNKNOWN;
}

inline currency_pair str_to_currency_pair(const std::string &name, exchange e) {
  std::string s;
  switch (e) {
  case exchange::BINANCE:
    s = name;
    // BTCBUSD
    sum_util::StrLower(s);
    return str_to_currency_pair(s);
  case exchange::BINANCE_SWAP:
    s = name;
    // BTCBUSD
    sum_util::StrLower(s);
    return str_to_currency_pair(s);
  case exchange::BYBIT:
    s = name;
    // BTCBUSD
    sum_util::StrLower(s);
    return str_to_currency_pair(s);
  case exchange::KRAKEN:
    s = name;
    // XBT/USDT → btc/usdt
    sum_util::StrLower(s);
    sum_util::StrReplace(s, "xbt", "btc");
    sum_util::StrReplace(s, "xdg", "doge");
    // keep the "/" — currency_pair_names uses "base/quote" format
    return str_to_currency_pair(s);
  default:
    return str_to_currency_pair(name);
  }
}

} // namespace data

#endif // DATA_CURRENCY_PAIR_H