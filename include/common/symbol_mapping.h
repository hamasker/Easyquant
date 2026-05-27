#pragma once
// 交易所 symbol 映射 — 统一命名 → 各所原生格式

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace symbol_mapping {

// ─── 交易所特定纠错规则 ───
// Kraken: BTC → XBT (历史原因, Kraken 用 XBT)
// 其他所: 统一用 BTC
inline std::string fix_special(const std::string &base_or_quote,
                                const std::string &exchange) {
  std::string s = base_or_quote;
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  if (exchange == "krk" || exchange == "kraken") {
    if (s == "btc") return "XBT";
  }
  // 其他交易所不需要特殊处理, 直接大写
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
