#include "common/volume_pairs.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <regex>
#include <utility>

#include "nlohmann_json/json.hpp"
#include <iostream>

namespace {

struct VolumeSource {
  const char *name;
  const char *exch_abbr;
  const char *url;
  std::function<std::pair<std::string, double>(const nlohmann::json &)> parse;
};

static const VolumeSource kVolumeSources[] = {
    {"Binance", "bn",
     "curl -s --max-time 5 "
     "'https://api.binance.com/api/v3/ticker/24hr?type=MINI' 2>/dev/null",
     [](const nlohmann::json &d) -> std::pair<std::string, double> {
       std::string sym = d.value("symbol", "");
       if (sym.size() < 4 || sym.substr(sym.size() - 4) != "USDT")
         return {"", 0};
       return {sym, std::stod(d.value("quoteVolume", "0"))};
     }},
    {"OKX", "ok",
     "curl -s --max-time 10 --connect-timeout 5 "
     "'https://www.okx.com/api/v5/market/tickers?instType=SPOT' 2>/dev/null",
     [](const nlohmann::json &d) -> std::pair<std::string, double> {
       std::string id = d.value("instId", "");
       // instId 格式: "BTC-USDT", "KNC-USDT" 等, 用 - 分隔
       auto dash = id.find('-');
       if (dash == std::string::npos) return {"", 0};
       std::string quote = id.substr(dash + 1);
       if (quote != "USDT") return {"", 0};
       std::string base = id.substr(0, dash);
       double vol = 0.0;
       if (d.contains("volCcy24h")) {
         if (d["volCcy24h"].is_string())
           vol = std::stod(d["volCcy24h"].get<std::string>());
         else if (d["volCcy24h"].is_number())
           vol = d["volCcy24h"].get<double>();
       }
       return {base + "_" + quote, vol};
     }},
    {"Gate.io", "gt",
     "curl -s --max-time 5 'https://api.gateio.ws/api/v4/spot/tickers' "
     "2>/dev/null",
     [](const nlohmann::json &d) -> std::pair<std::string, double> {
       std::string pair = d.value("currency_pair", "");
       if (pair.size() < 5 || pair.substr(pair.size() - 5) != "_USDT")
         return {"", 0};
       return {pair, std::stod(d.value("quote_volume", "0"))};
     }},
    {"Coinbase", "cb",
     "curl -s --max-time 5 "
     "'https://api.exchange.coinbase.com/products/volume-summary' "
     "2>/dev/null",
     [](const nlohmann::json &d) -> std::pair<std::string, double> {
       // d = {"id":"BTC-USD","base_currency":"BTC","quote_currency":"USD",...}
       double vol = 0.0;
       if (d.contains("spot_volume_24hour")) {
         auto &v = d["spot_volume_24hour"];
         if (v.is_string()) vol = std::stod(v.get<std::string>());
         else if (v.is_number()) vol = v.get<double>();
       }
       if (vol <= 0) return {"", 0};
       std::string base = d.value("base_currency", "");
       std::string quote = d.value("quote_currency", "");
       if (base.empty() || quote.empty()) return {"", 0};
       return {base + "_" + quote, vol};
     }},
};

std::string http_get(const char *shell_cmd) {
  std::string raw;
  char buf[4096];
  auto *pipe = popen(shell_cmd, "r");
  if (!pipe) return raw;
  while (fgets(buf, sizeof(buf), pipe)) raw += buf;
  pclose(pipe);
  return raw;
}

std::vector<std::string> fetch_top_n(const VolumeSource &src, int n) {
  std::vector<std::string> result;
  std::string raw = http_get(src.url);
  if (raw.empty()) return result;

  try {
    nlohmann::json data;
    try {
      data = nlohmann::json::parse(raw);
    } catch (const nlohmann::json::parse_error &) {
      data = nlohmann::json::parse(raw, nullptr, false);
      if (data.is_discarded())
        throw std::runtime_error("parse failed (strict & non-strict)");
    }

    const auto *arr = &data;
    if (data.is_object() && data.contains("data") && data["data"].is_array())
      arr = &data["data"];
    if (!arr->is_array()) return result;

    // Coinbase 嵌套数组展平: [[{...},{...}],[{...}]] → [{...},{...},{...}]
    nlohmann::json flat = nlohmann::json::array();
    if (!arr->empty() && (*arr)[0].is_array()) {
      for (auto &group : *arr)
        for (auto &item : group)
          flat.push_back(item);
      arr = &flat;
    }

    std::vector<std::pair<std::string, double>> ranked;
    for (auto &d : *arr) {
      if (!d.is_object()) continue;
      auto [sym, vol] = src.parse(d);
      if (!sym.empty() && vol > 0) ranked.emplace_back(sym, vol);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](auto &a, auto &b) { return a.second > b.second; });
    for (int i = 0; i < std::min((int)ranked.size(), n); ++i)
      result.push_back(ranked[i].first);
    std::cout << "[TradeVol] " << src.name << ": top " << result.size()
              << " / " << ranked.size() << " pairs" << std::endl;
  } catch (const std::exception &e) {
    // Gate.io 偶有损坏 JSON → 正则提取字段
    try {
      std::vector<std::pair<std::string, double>> ranked;
      std::regex re(
          "\"currency_pair\"\\s*:\\s*\"([^\"]+)\"[^}]*\"quote_volume\"\\s*:"
          "\\s*\"([^\"]+)\"");
      for (auto it = std::sregex_iterator(raw.begin(), raw.end(), re);
           it != std::sregex_iterator(); ++it) {
        std::string pair = (*it)[1].str();
        if (pair.size() < 5 || pair.substr(pair.size() - 5) != "_USDT")
          continue;
        double vol = std::stod((*it)[2].str());
        if (vol > 0) ranked.emplace_back(pair, vol);
      }
      std::sort(ranked.begin(), ranked.end(),
                [](auto &a, auto &b) { return a.second > b.second; });
      for (int i = 0; i < std::min((int)ranked.size(), n); ++i)
        result.push_back(ranked[i].first);
      std::cout << "[TradeVol] " << src.name << ": top " << result.size()
                << " / " << ranked.size() << " pairs (regex)" << std::endl;
    } catch (...) {
      std::cerr << "[TradeVol] " << src.name
                << " parse failed: " << e.what() << std::endl;
    }
  }
  return result;
}

} // namespace

std::vector<std::string> fetch_all_top_pairs(int per_exch) {
  std::vector<std::string> all;
  for (auto &src : kVolumeSources) {
    auto tops = fetch_top_n(src, per_exch);
    for (auto &sym : tops) {
      std::string lower = sym;
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      if (lower.find('_') == std::string::npos) {
        for (const char *q : {"usdt"}) {
          if (lower.size() > 4 && lower.substr(lower.size() - 4) == q) {
            lower = lower.substr(0, lower.size() - 4) + "_" + q;
            break;
          }
        }
      }
      all.push_back(lower + "." + src.exch_abbr);
    }
  }
  return all;
}
