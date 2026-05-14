#pragma once

#include "base_common.h"
#include "nova_api_currency.h"
#include "nova_api_data_type.h"
#include "nova_api_exch.h"
#include "nova_api_inst_type.h"
#include "symbol_translator.h"

#include "nova_base.h"
#include <unordered_map>

BEGIN_NOVA_NAMESPACE(quote)

struct InstrumentId {
  char symbol[28];
  NOVA_COIN_CURRENCY currency;
  NOVA_COIN_INST_TYPE inst_type;
  NOVA_EXCHANGE_TYPE exchange;
  int8_t ticker_len;

  static constexpr int MAX_TICKER_LEN = 20;

  using Key = string;
  Key key() const { return symbol; }

  static InstrumentId Invalid() {
    return {"", NOVA_COIN_CURRENCY_INIT, NOVA_COIN_INST_TYPE_INIT,
            NOVA_EXCHANGE_INIT, 0};
  }

  static InstrumentId Create(const string &sym) {
    auto p = sym.rfind('.');
    slow_if(p == string::npos) {
      // ERROR_FLOG("[InstrumentId.Create]Invalid symbol {}", sym);
      return Invalid();
    }
    const char *exch_p = sym.c_str() + p + 1;
    NOVA_EXCHANGE_TYPE exch = GetExchangeFromStr(exch_p);
    slow_if(exch == NOVA_EXCHANGE_UNKNOWN) {
      // ERROR_FLOG("[InstrumentId.Create]Invalid exch, symbol: {}, exch: {}",
      // sym, exch_p);
      return Invalid();
    }
    return Create(sym.substr(0, p), exch);
  }

  static InstrumentId Create(const string &sym, NOVA_EXCHANGE_TYPE p_exch) {
    if (sym.rfind('.') != string::npos) {
      auto ret = Create(sym);
      if (ret.exchange != p_exch) {
        // ERROR_FLOG("[InstrumentId.Create]Invalid exch, symbol: {}, exch: {}",
        // sym, (int)p_exch);
        return Invalid();
      }
      return ret;
    }

    slow_if(p_exch == NOVA_EXCHANGE_INIT || p_exch == NOVA_EXCHANGE_UNKNOWN) {
      // ERROR_FLOG("");
      return Invalid();
    }
    auto p = sym.rfind('_');
    if (p == std::string::npos) {
      // ERROR_FLOG("");
      return Invalid();
    }
    const char *inst_p = sym.c_str() + p + 1;
    NOVA_COIN_INST_TYPE inst_type = GetCoinInstType(inst_p);
    NOVA_COIN_CURRENCY currency = NOVA_COIN_CURRENCY_UNKNOWN;
    if (inst_type == NOVA_COIN_INST_TYPE_UNKNOWN) {
      currency = GetCoinCurrency(inst_p);
      if (currency != NOVA_COIN_CURRENCY_UNKNOWN) {
        inst_type = NOVA_COIN_INST_TYPE_SPOT;
      }
    } else {
      auto q = sym.rfind('_', p - 1);
      slow_if(q == std::string::npos) {
        // ERROR_FLOG("");
        return Invalid();
      }
      std::string currency_str = sym.substr(q + 1, p - q - 1);
      currency = GetCoinCurrency(currency_str.c_str());
      p = q;
    }
    slow_if((inst_type == NOVA_COIN_INST_TYPE_UNKNOWN ||
             currency == NOVA_COIN_CURRENCY_UNKNOWN)) {
      // ERROR_FLOG("");
      return Invalid();
    }
    // return Create(sym.substr(0, p), p_exch); // todo
    return Create(sym.substr(0, p), currency, inst_type, p_exch);
  }

  static InstrumentId Create(const string &ticker,
                             NOVA_COIN_CURRENCY p_currency,
                             NOVA_COIN_INST_TYPE p_inst_type,
                             NOVA_EXCHANGE_TYPE p_exch) {
    slow_if(p_exch == NOVA_EXCHANGE_INIT || p_exch == NOVA_EXCHANGE_UNKNOWN ||
            p_inst_type == NOVA_COIN_INST_TYPE_INIT ||
            p_inst_type == NOVA_COIN_INST_TYPE_UNKNOWN ||
            p_currency == NOVA_COIN_CURRENCY_INIT ||
            p_currency == NOVA_COIN_CURRENCY_UNKNOWN) {
      // ERROR_FLOG("");
      return Invalid();
    }
    slow_if(ticker.length() > MAX_TICKER_LEN) {
      // ERROR_FLOG("");
      return Invalid();
    }
    auto ticker_len = ticker.length();
    std::string ticker_lower = ticker;
    std::transform(ticker.begin(), ticker.end(), ticker_lower.begin(),
                   [](const char c) { return std::tolower(c); });
    ticker_lower.push_back('_');
    ticker_lower.append(GetCoinCurrencyString(p_currency));
    if (p_inst_type != NOVA_COIN_INST_TYPE_SPOT) {
      ticker_lower.push_back('_');
      ticker_lower.append(GetCoinInstTypeString(p_inst_type));
    }
    ticker_lower.push_back('.');
    ticker_lower.append(GetExchangeStrFromId(p_exch));
    slow_if(ticker_lower.length() > sizeof(symbol) - 1) {
      static const std::unordered_map<std::string, std::string>
          _symbol_abbr_map = {
#define ABBR_MAP(a, b) {a, b},
#include "nova_api_instrument_abbr_list.h"
#undef ABBR_MAP
          };
      auto iter = _symbol_abbr_map.find(ticker_lower);
      if (iter == _symbol_abbr_map.cend()) {
        ERROR_FLOG("Invalid symbol: {}", ticker_lower);
        return Invalid();
      }
      ticker_lower = iter->second;
      if (ticker_lower.length() > sizeof(symbol) - 1) {
        ERROR_FLOG("Invalid symbol: {}", ticker_lower);
        return Invalid();
      }
    }
    InstrumentId ret{};
    memset(ret.symbol, 0x0, sizeof(ret.symbol));
    strncpy(ret.symbol, ticker_lower.c_str(), sizeof(ret.symbol));
    ret.currency = p_currency;
    ret.inst_type = p_inst_type;
    ret.exchange = p_exch;
    ret.ticker_len = ticker_len;
    return ret;
  }

  bool Valid() const {
    return ticker_len > 0 && currency != NOVA_COIN_CURRENCY_INIT &&
           inst_type != NOVA_COIN_INST_TYPE_INIT &&
           exchange != NOVA_EXCHANGE_INIT;
  }

  const char *GetSymbol() const { return symbol; }
};

END_NOVA_NAMESPACE(quote)