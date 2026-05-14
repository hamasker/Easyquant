// // ... existing code ...
// // #include "data_currency_pair.h"
// #include "data_exchange.h"
// #include <algorithm>
// #include <array>
// // ... existing code ...
// namespace data {
// // ... existing code ...
// // Instrument 枚举，所有币对+交易所组合
// // 命名规则：币对_交易所缩写，如 BTC_USDT_BN
// // 只生成币对和主流交易所的组合

// enum class instrument : uint16_t {
//   UNKNOWN = 0,

//   // Binance 1+
//   BTC_USDT_BN = 1,
//   ETH_USDT_BN,
//   USD_CAD_BN,
//   USD_CHF_BN,
//   AUD_USD_BN,
//   USDT_USD_BN,
//   USDT_EUR_BN,
//   USDC_USD_BN,
//   USDC_EUR_BN,
//   USDC_USDT_BN,
//   USDT_GBP_BN,
//   USDT_CHF_BN,
//   USDT_CAD_BN,
//   USDT_AUD_BN,
//   USDC_GBP_BN,
//   USDC_AUD_BN,
//   EUR_CHF_BN,
//   EUR_GBP_BN,
//   DAI_USD_BN,
//   DAI_EUR_BN,
//   DAI_USDT_BN,
//   PAXG_USD_BN,
//   PAXG_EUR_BN,
//   BTC_USD_BN,
//   BTC_EUR_BN,
//   BTC_GBP_BN,
//   BTC_USDC_BN,
//   BTC_CAD_BN,
//   BTC_CHF_BN,
//   BTC_AUD_BN,
//   ETH_USD_BN,
//   ETH_EUR_BN,
//   ETH_BTC_BN,
//   ETH_GBP_BN,
//   ETH_USDC_BN,
//   ETH_CAD_BN,
//   ETH_CHF_BN,
//   ETH_AUD_BN,
//   SOL_USD_BN,
//   SOL_EUR_BN,
//   SOL_BTC_BN,
//   SOL_GBP_BN,
//   DOT_USD_BN,
//   DOT_EUR_BN,
//   DOT_BTC_BN,
//   DOT_USDT_BN,
//   DOT_GBP_BN,
//   MATIC_USD_BN,
//   MATIC_EUR_BN,
//   MATIC_BTC_BN,
//   EUR_BUSD_BN,
//   GBP_BUSD_BN,
//   BTC_BUSD_BN,
//   ETH_BUSD_BN,
//   BUSD_USDT_BN,
//   SOL_USDT_BN,
//   MATIC_USDT_BN,
//   USDT_USDC_BN,
//   USDT_DAI_BN,
//   DOGE_USD_BN,
//   LTC_USD_BN,
//   XRP_USD_BN,
//   ADA_USD_BN,
//   LINK_USD_BN,
//   DOGE_EUR_BN,
//   LTC_EUR_BN,
//   XRP_EUR_BN,
//   ADA_EUR_BN,
//   LINK_EUR_BN,
//   DOGE_USDT_BN,
//   LTC_USDT_BN,
//   XRP_USDT_BN,
//   ADA_USDT_BN,
//   LTC_BTC_BN,
//   XRP_BTC_BN,
//   ADA_BTC_BN,
//   LINK_BTC_BN,
//   XRP_GBP_BN,
//   LINK_USDT_BN,
//   BNB_BUSD_BN,

//   // OKX 301+
//   BTC_USDT_OK = 301,
//   ETH_USDT_OK,
//   USD_CAD_OK,
//   USD_CHF_OK,
//   AUD_USD_OK,
//   USDT_USD_OK,
//   USDT_EUR_OK,
//   USDC_USD_OK,
//   USDC_EUR_OK,
//   USDC_USDT_OK,
//   USDT_GBP_OK,
//   USDT_CHF_OK,
//   USDT_CAD_OK,
//   USDT_AUD_OK,
//   USDC_GBP_OK,
//   USDC_AUD_OK,
//   EUR_CHF_OK,
//   EUR_GBP_OK,
//   DAI_USD_OK,
//   DAI_EUR_OK,
//   DAI_USDT_OK,
//   PAXG_USD_OK,
//   PAXG_EUR_OK,
//   BTC_USD_OK,
//   BTC_EUR_OK,
//   BTC_GBP_OK,
//   BTC_USDC_OK,
//   BTC_CAD_OK,
//   BTC_CHF_OK,
//   BTC_AUD_OK,
//   ETH_USD_OK,
//   ETH_EUR_OK,
//   ETH_BTC_OK,
//   ETH_GBP_OK,
//   ETH_USDC_OK,
//   ETH_CAD_OK,
//   ETH_CHF_OK,
//   ETH_AUD_OK,
//   SOL_USD_OK,
//   SOL_EUR_OK,
//   SOL_BTC_OK,
//   SOL_GBP_OK,
//   DOT_USD_OK,
//   DOT_EUR_OK,
//   DOT_BTC_OK,
//   DOT_USDT_OK,
//   DOT_GBP_OK,
//   MATIC_USD_OK,
//   MATIC_EUR_OK,
//   MATIC_BTC_OK,
//   EUR_BUSD_OK,
//   GBP_BUSD_OK,
//   BTC_BUSD_OK,
//   ETH_BUSD_OK,
//   BUSD_USDT_OK,
//   SOL_USDT_OK,
//   MATIC_USDT_OK,
//   USDT_USDC_OK,
//   USDT_DAI_OK,
//   DOGE_USD_OK,
//   LTC_USD_OK,
//   XRP_USD_OK,
//   ADA_USD_OK,
//   LINK_USD_OK,
//   DOGE_EUR_OK,
//   LTC_EUR_OK,
//   XRP_EUR_OK,
//   ADA_EUR_OK,
//   LINK_EUR_OK,
//   DOGE_USDT_OK,
//   LTC_USDT_OK,
//   XRP_USDT_OK,
//   ADA_USDT_OK,
//   LTC_BTC_OK,
//   XRP_BTC_OK,
//   ADA_BTC_OK,
//   LINK_BTC_OK,
//   XRP_GBP_OK,
//   LINK_USDT_OK,
//   BNB_BUSD_OK,

//   // Coinbase 501+
//   BTC_USDT_CB = 501,
//   ETH_USDT_CB,
//   USD_CAD_CB,
//   USD_CHF_CB,
//   AUD_USD_CB,
//   USDT_USD_CB,
//   USDT_EUR_CB,
//   USDC_USD_CB,
//   USDC_EUR_CB,
//   USDC_USDT_CB,
//   USDT_GBP_CB,
//   USDT_CHF_CB,
//   USDT_CAD_CB,
//   USDT_AUD_CB,
//   USDC_GBP_CB,
//   USDC_AUD_CB,
//   EUR_CHF_CB,
//   EUR_GBP_CB,
//   DAI_USD_CB,
//   DAI_EUR_CB,
//   DAI_USDT_CB,
//   PAXG_USD_CB,
//   PAXG_EUR_CB,
//   BTC_USD_CB,
//   BTC_EUR_CB,
//   BTC_GBP_CB,
//   BTC_USDC_CB,
//   BTC_CAD_CB,
//   BTC_CHF_CB,
//   BTC_AUD_CB,
//   ETH_USD_CB,
//   ETH_EUR_CB,
//   ETH_BTC_CB,
//   ETH_GBP_CB,
//   ETH_USDC_CB,
//   ETH_CAD_CB,
//   ETH_CHF_CB,
//   ETH_AUD_CB,
//   SOL_USD_CB,
//   SOL_EUR_CB,
//   SOL_BTC_CB,
//   SOL_GBP_CB,
//   DOT_USD_CB,
//   DOT_EUR_CB,
//   DOT_BTC_CB,
//   DOT_USDT_CB,
//   DOT_GBP_CB,
//   MATIC_USD_CB,
//   MATIC_EUR_CB,
//   MATIC_BTC_CB,
//   EUR_BUSD_CB,
//   GBP_BUSD_CB,
//   BTC_BUSD_CB,
//   ETH_BUSD_CB,
//   BUSD_USDT_CB,
//   SOL_USDT_CB,
//   MATIC_USDT_CB,
//   USDT_USDC_CB,
//   USDT_DAI_CB,
//   DOGE_USD_CB,
//   LTC_USD_CB,
//   XRP_USD_CB,
//   ADA_USD_CB,
//   LINK_USD_CB,
//   DOGE_EUR_CB,
//   LTC_EUR_CB,
//   XRP_EUR_CB,
//   ADA_EUR_CB,
//   LINK_EUR_CB,
//   DOGE_USDT_CB,
//   LTC_USDT_CB,
//   XRP_USDT_CB,
//   ADA_USDT_CB,
//   LTC_BTC_CB,
//   XRP_BTC_CB,
//   ADA_BTC_CB,
//   LINK_BTC_CB,
//   XRP_GBP_CB,
//   LINK_USDT_CB,
//   BNB_BUSD_CB,

//   // Kraken 601+
//   BTC_USDT_KRK = 601,
//   ETH_USDT_KRK,
//   USD_CAD_KRK,
//   USD_CHF_KRK,
//   AUD_USD_KRK,
//   USDT_USD_KRK,
//   USDT_EUR_KRK,
//   USDC_USD_KRK,
//   USDC_EUR_KRK,
//   USDC_USDT_KRK,
//   USDT_GBP_KRK,
//   USDT_CHF_KRK,
//   USDT_CAD_KRK,
//   USDT_AUD_KRK,
//   USDC_GBP_KRK,
//   USDC_AUD_KRK,
//   EUR_CHF_KRK,
//   EUR_GBP_KRK,
//   DAI_USD_KRK,
//   DAI_EUR_KRK,
//   DAI_USDT_KRK,
//   PAXG_USD_KRK,
//   PAXG_EUR_KRK,
//   BTC_USD_KRK,
//   BTC_EUR_KRK,
//   BTC_GBP_KRK,
//   BTC_USDC_KRK,
//   BTC_CAD_KRK,
//   BTC_CHF_KRK,
//   BTC_AUD_KRK,
//   ETH_USD_KRK,
//   ETH_EUR_KRK,
//   ETH_BTC_KRK,
//   ETH_GBP_KRK,
//   ETH_USDC_KRK,
//   ETH_CAD_KRK,
//   ETH_CHF_KRK,
//   ETH_AUD_KRK,
//   SOL_USD_KRK,
//   SOL_EUR_KRK,
//   SOL_BTC_KRK,
//   SOL_GBP_KRK,
//   DOT_USD_KRK,
//   DOT_EUR_KRK,
//   DOT_BTC_KRK,
//   DOT_USDT_KRK,
//   DOT_GBP_KRK,
//   MATIC_USD_KRK,
//   MATIC_EUR_KRK,
//   MATIC_BTC_KRK,
//   EUR_BUSD_KRK,
//   GBP_BUSD_KRK,
//   BTC_BUSD_KRK,
//   ETH_BUSD_KRK,
//   BUSD_USDT_KRK,
//   SOL_USDT_KRK,
//   MATIC_USDT_KRK,
//   USDT_USDC_KRK,
//   USDT_DAI_KRK,
//   DOGE_USD_KRK,
//   LTC_USD_KRK,
//   XRP_USD_KRK,
//   ADA_USD_KRK,
//   LINK_USD_KRK,
//   DOGE_EUR_KRK,
//   LTC_EUR_KRK,
//   XRP_EUR_KRK,
//   ADA_EUR_KRK,
//   LINK_EUR_KRK,
//   DOGE_USDT_KRK,
//   LTC_USDT_KRK,
//   XRP_USDT_KRK,
//   ADA_USDT_KRK,
//   LTC_BTC_KRK,
//   XRP_BTC_KRK,
//   ADA_BTC_KRK,
//   LINK_BTC_KRK,
//   XRP_GBP_KRK,
//   LINK_USDT_KRK,
//   BNB_BUSD_KRK,

//   INSTRUMENT_MAX
// };

// // instrument -> 字符串，如 btc/usdt.bn
// inline std::string get_instrument_name(instrument inst) {
//   static const char *pair_names[] = {
//       "btc/usdt",  "eth/usdt",   "eth/btc",   "btc/usdc",  "eth/usdc",
//       "btc/usd",   "eth/usd",    "sol/usdt",  "doge/usdt", "ada/usdt",
//       "xrp/usdt",  "ltc/usdt",   "link/usdt", "bnb/busd",  "usdt/usdc",
//       "usdt/usd",  "usdc/usd",   "usdt/eur",  "usdc/eur",  "dai/usdt",
//       "dot/usdt",  "matic/usdt", "sol/usd",   "doge/usd",  "ada/usd",
//       "xrp/usd",   "ltc/usd",    "link/usd",  "btc/eur",   "eth/eur",
//       "sol/eur",   "doge/eur",   "ada/eur",   "xrp/eur",   "ltc/eur",
//       "link/eur",  "btc/gbp",    "eth/gbp",   "xrp/gbp",   "dot/usd",
//       "matic/usd", "btc/busd",   "eth/busd",  "busd/usdt", "usdt/gbp",
//       "usdt/chf",  "usdt/cad",   "usdt/aud",  "usdc/gbp",  "usdc/aud",
//       "eur/chf",   "eur/gbp",    "dai/usd",   "dai/eur",   "paxg/usd",
//       "paxg/eur",  "btc/cad",    "btc/chf",   "btc/aud",   "eth/cad",
//       "eth/chf",   "eth/aud",    "sol/btc",   "sol/gbp",   "dot/eur",
//       "dot/btc",   "dot/gbp",    "matic/eur", "matic/btc", "eur/busd",
//       "gbp/busd",  "ltc/btc",    "xrp/btc",   "ada/btc",   "link/btc"};
//   static const char *ex_abbr[] = {"bn", "ok", "cb", "krk"};
//   int idx = static_cast<int>(inst);
//   if (idx <= 0 || idx >= static_cast<int>(instrument::INSTRUMENT_MAX))
//     return "unknown";
//   int pair_count = sizeof(pair_names) / sizeof(pair_names[0]);
//   int ex_count = 4;
//   int pair_idx = (idx - 1) / ex_count;
//   int ex_idx = (idx - 1) % ex_count;
//   if (pair_idx >= pair_count || ex_idx >= ex_count)
//     return "unknown";
//   return std::string(pair_names[pair_idx]) + "." + ex_abbr[ex_idx];
// }

// // 由币对和交易所生成instrument
// inline instrument make_instrument(currency_pair cp, exchange ex) {
//   // 只支持主流交易所
//   int pair_count = 75; // 与pair_names一致
//   int ex_count = 4;
//   int cp_idx = static_cast<int>(cp) - 2; // 跳过UNKNOWN和EUR_USD
//   int ex_idx = -1;
//   switch (ex) {
//   case exchange::BINANCE:
//     ex_idx = 0;
//     break;
//   case exchange::OKX:
//     ex_idx = 1;
//     break;
//   case exchange::COINBASE:
//     ex_idx = 2;
//     break;
//   case exchange::KRAKEN:
//     ex_idx = 3;
//     break;
//   default:
//     return instrument::UNKNOWN;
//   }
//   if (cp_idx < 0 || cp_idx >= pair_count || ex_idx < 0)
//     return instrument::UNKNOWN;
//   return static_cast<instrument>(1 + cp_idx * ex_count + ex_idx);
// }

// // 字符串转instrument
// inline instrument str_to_instrument(const std::string &name) {
//   // 格式如 btc/usdt.bn
//   static const char *pair_names[] = {
//       "btc/usdt",  "eth/usdt",   "eth/btc",   "btc/usdc",  "eth/usdc",
//       "btc/usd",   "eth/usd",    "sol/usdt",  "doge/usdt", "ada/usdt",
//       "xrp/usdt",  "ltc/usdt",   "link/usdt", "bnb/busd",  "usdt/usdc",
//       "usdt/usd",  "usdc/usd",   "usdt/eur",  "usdc/eur",  "dai/usdt",
//       "dot/usdt",  "matic/usdt", "sol/usd",   "doge/usd",  "ada/usd",
//       "xrp/usd",   "ltc/usd",    "link/usd",  "btc/eur",   "eth/eur",
//       "sol/eur",   "doge/eur",   "ada/eur",   "xrp/eur",   "ltc/eur",
//       "link/eur",  "btc/gbp",    "eth/gbp",   "xrp/gbp",   "dot/usd",
//       "matic/usd", "btc/busd",   "eth/busd",  "busd/usdt", "usdt/gbp",
//       "usdt/chf",  "usdt/cad",   "usdt/aud",  "usdc/gbp",  "usdc/aud",
//       "eur/chf",   "eur/gbp",    "dai/usd",   "dai/eur",   "paxg/usd",
//       "paxg/eur",  "btc/cad",    "btc/chf",   "btc/aud",   "eth/cad",
//       "eth/chf",   "eth/aud",    "sol/btc",   "sol/gbp",   "dot/eur",
//       "dot/btc",   "dot/gbp",    "matic/eur", "matic/btc", "eur/busd",
//       "gbp/busd",  "ltc/btc",    "xrp/btc",   "ada/btc",   "link/btc"};
//   static const char *ex_abbr[] = {"bn", "ok", "cb", "krk"};
//   for (int i = 0; i < 75; ++i) {
//     for (int j = 0; j < 4; ++j) {
//       std::string inst_name = std::string(pair_names[i]) + "." + ex_abbr[j];
//       if (name == inst_name) {
//         return static_cast<instrument>(1 + i * 4 + j);
//       }
//     }
//   }
//   return instrument::UNKNOWN;
// }

// } // namespace data

// // #endif // DATA_INSTRUMENT_H