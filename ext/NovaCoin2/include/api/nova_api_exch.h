#pragma once

#include "base_util.h"

#define DEFEXCH(exch, code, str)                                               \
  constexpr auto NOVA_EXCHANGE_##exch##_STR = str;
#include "nova_api_exch_list.h"
#undef DEFEXCH

enum NOVA_EXCHANGE_TYPE : uint8_t {
  NOVA_EXCHANGE_INIT = 0,
#define DEFEXCH(exch, code, str) NOVA_EXCHANGE_##exch = code,
#include "nova_api_exch_list.h"
#undef DEFEXCH
  NOVA_EXCHANGE_UNKNOWN = 255
};

inline NOVA_EXCHANGE_TYPE GetExchangeFromStr(const char *src) {
  if (!src)
    return NOVA_EXCHANGE_UNKNOWN;
#define DEFEXCH(exch, code, str)                                               \
  if (strcasecmp(src, str) == 0)                                               \
    return NOVA_EXCHANGE_##exch;
#include "nova_api_exch_list.h"
#undef DEFEXCH
  return NOVA_EXCHANGE_UNKNOWN;
}

inline NOVA_EXCHANGE_TYPE GetExchangeIdFromStr(const char *src) {
  return GetExchangeFromStr(src);
}

inline const char *QuoteExchangeStrFromId(uint8_t exch) {
#define DEFEXCH(eexch, code, str)                                              \
  if (exch == NOVA_EXCHANGE_##eexch)                                           \
    return str;
#include "nova_api_exch_list.h"
#undef DEFEXCH
  return "UNKNOWN";
}

inline const char *GetExchangeStrFromId(uint8_t exch) {
  return QuoteExchangeStrFromId(exch);
}