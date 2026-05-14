#pragma once

#include "base_util.h"

#define DEF_CURRENCY(_type, _code, _str) constexpr auto NOVA_COIN_CURRENCY_STR_##_type = _str;
#include "nova_api_currency_list.h"
#undef DEF_CURRENCY

enum NOVA_COIN_CURRENCY : uint8_t
{
    NOVA_COIN_CURRENCY_INIT = 0,
#define DEF_CURRENCY(_type, _code, _str) DEF_COIN_CURRENCY_##_type = _code,
#include "nova_api_currency_list.h"
#undef DEF_CURRENCY
    NOVA_COIN_CURRENCY_UNKNOWN = 255
};

inline NOVA_COIN_CURRENCY GetCoinCurrency(const char *str)
{
    if (!str)
        return NOVA_COIN_CURRENCY_INIT;
#define DEF_CURRENCY(_type, _code, _str) \
    if (strcasecmp(str, _str) == 0)      \
        return DEF_COIN_CURRENCY_##_type;
#include "nova_api_currency_list.h"
#undef DEF_CURRENCY
    return NOVA_COIN_CURRENCY_UNKNOWN;
}

inline const char *GetCoinCurrencyString(uint8_t currency)
{
#define DEF_CURRENCY(_type, _code, _str) \
    if (currency == _code)               \
        return _str;
#include "nova_api_currency_list.h"
#undef DEF_CURRENCY
    return "UNKNOWN";
}