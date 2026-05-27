#pragma once
// 交易所 symbol 映射 — 统一命名 → 各所原生格式

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace symbol_mapping {

// ─── Kraken 特殊映射 ───
// Kraken 对大部分加密货币加 X/XX 前缀, 法币和稳定币不加
// 规则: 3字母加 X, 4字母加 XX (如 XBT=BTC, XXRP=XRP, XETH=ETH)
inline const std::unordered_map<std::string, std::string>& kraken_x_map() {
  static const std::unordered_map<std::string, std::string> m = {
    // Crypto (加 X/XX 前缀)
    {"btc", "XBT"},    {"eth", "XETH"},   {"ltc", "XLTC"},
    {"xrp", "XXRP"},   {"xlm", "XXLM"},   {"xmr", "XXMR"},
    {"etc", "XETC"},   {"zec", "XZEC"},   {"rep", "XREP"},
    {"mln", "XMLN"},   {"doge", "XXDG"},  {"dash", "DASH"},
    // 其他常见 crypto
    {"ada", "ADA"},    {"sol", "SOL"},    {"dot", "DOT"},
    {"link", "LINK"},  {"matic", "MATIC"}, {"uni", "UNI"},
    {"aave", "AAVE"},  {"atom", "ATOM"},  {"algo", "ALGO"},
    {"xtz", "XTZ"},    {"fil", "FIL"},    {"trx", "TRX"},
    {"avax", "AVAX"},  {"shib", "SHIB"},  {"ape", "APE"},
    {"sand", "SAND"},  {"mana", "MANA"},  {"grt", "GRT"},
    {"near", "NEAR"},  {"flow", "FLOW"},  {"icp", "ICP"},
    {"fet", "FET"},    {"render", "RENDER"}, {"ondo", "ONDO"},
    {"sui", "SUI"},    {"sei", "SEI"},    {"tia", "TIA"},
    {"bonk", "BONK"},  {"wif", "WIF"},    {"jup", "JUP"},
    {"pyth", "PYTH"},  {"pengu", "PENGU"}, {"trump", "TRUMP"},
    {"pepe", "PEPE"},  {"floki", "FLOKI"}, {"gala", "GALA"},
    {"ens", "ENS"},    {"ldo", "LDO"},    {"arb", "ARB"},
    {"op", "OP"},      {"strk", "STRK"},  {"ena", "ENA"},
    {"eigen", "EIGEN"}, {"pendle", "PENDLE"}, {"axs", "AXS"},
    {"sand", "SAND"},  {"mana", "MANA"},  {"chz", "CHZ"},
    {"bch", "BCH"},    {"bsv", "BSV"},
    // Stablecoins & Fiat (WS v2 用标准名, 不加 Z 前缀)
    {"usd", "USD"},    {"eur", "EUR"},    {"gbp", "GBP"},
    {"aud", "AUD"},    {"cad", "CAD"},    {"chf", "CHF"},
    {"usdt", "USDT"},  {"usdc", "USDC"},  {"dai", "DAI"},
    {"paxg", "PAXG"},  {"paxgold", "PAXG"},
    {"eurc", "EURC"},  {"pyusd", "PYUSD"},
    {"tbtc", "TBTC"},  {"wbtc", "WBTC"},
    {"ausd", "AUSD"},  {"usde", "USDE"},  {"usds", "USDS"},
    {"usd1", "USD1"},  {"usdq", "USDQ"},  {"usdr", "USDR"},
    {"usduc", "USDUC"}, {"usdd", "USDD"},
    {"xaub", "XAUT"},
  };
  return m;
}

// ─── 交易所特定纠错规则 ───
inline std::string fix_special(const std::string &base_or_quote,
                                const std::string &exchange) {
  std::string s = base_or_quote;
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  if (exchange == "krk" || exchange == "kraken") {
    auto it = kraken_x_map().find(s);
    if (it != kraken_x_map().end()) return it->second;
  }
  std::transform(s.begin(), s.end(), s.begin(), ::toupper);
  return s;
}

// ─── 组装所内 symbol ───
// 统一输入: ("btc", "usd")  → 各所输出:
//   Binance: "BTCUSDT"   (无缝)
//   Kraken:  "XBT/USD"   (XBT + 斜杠)
//   OKX:     "BTC-USDT"  (横杠)
//   Coinbase:"BTC-USD"   (横杠)
inline std::string make_pair(const std::string &base, const std::string &quote,
                             const std::string &exchange) {
  std::string b = fix_special(base, exchange);
  std::string q = fix_special(quote, exchange);
  if (exchange == "bn" || exchange == "binance" || exchange == "bn_swap")
    return b + q;                        // BTCUSDT
  if (exchange == "krk" || exchange == "kraken")
    return b + "/" + q;                  // XBT/USD
  if (exchange == "ok" || exchange == "okx")
    return b + "-" + q;                  // BTC-USDT
  if (exchange == "cb" || exchange == "coinbase")
    return b + "-" + q;                  // BTC-USD
  return b + q;
}

// ─── 从所内 symbol 反向提取 base/quote ───
inline std::pair<std::string, std::string>
split_pair(const std::string &raw, const std::string &exchange) {
  char sep = '_';  // default
  if (exchange == "krk" || exchange == "kraken") sep = '/';
  else if (exchange == "ok" || exchange == "okx") sep = '-';
  else if (exchange == "cb" || exchange == "coinbase") sep = '-';

  auto pos = raw.find(sep);
  if (pos == std::string::npos) {
    if (exchange == "bn" || exchange == "binance" || exchange == "bn_swap") {
      // BTCUSDT → 尝试常见 quote 切分
      for (auto &q : {"USDT", "USD", "BTC", "ETH", "BNB", "USDC", "BUSD"}) {
        size_t p = raw.rfind(q);
        size_t qlen = std::char_traits<char>::length(q);
        if (p != std::string::npos && p > 0 && p + qlen == raw.size())
          return {raw.substr(0, p), raw.substr(p)};
      }
    }
    return {raw, ""};
  }
  return {raw.substr(0, pos), raw.substr(pos + 1)};
}

// ─── 工具: 去掉分隔符小写化 (用于模糊匹配) ───
inline std::string normalize(const std::string &sym) {
  std::string s = sym;
  s.erase(std::remove(s.begin(), s.end(), '/'), s.end());
  s.erase(std::remove(s.begin(), s.end(), '-'), s.end());
  s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  // XBT ↔ BTC 互转
  size_t p = s.find("xbt");
  if (p != std::string::npos) s.replace(p, 3, "btc");
  return s;
}

} // namespace symbol_mapping
