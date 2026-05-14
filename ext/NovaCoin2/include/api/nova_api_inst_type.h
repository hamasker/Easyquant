#pragma once

#include "base_util.h"

#define DEF_INST_TYPE(_type, _code, _str) constexpr auto NOVA_COIN_INST_TYPE_STR_##_type = _str;
#include "nova_api_inst_type_list.h"
#undef DEF_INST_TYPE

enum NOVA_COIN_INST_TYPE : uint8_t
{
    NOVA_COIN_INST_TYPE_INIT = 0,
#define DEF_INST_TYPE(_type, _code, _str) NOVA_COIN_INST_TYPE_##_type = _code,
#include "nova_api_inst_type_list.h"
#undef DEF_INST_TYPE
    NOVA_COIN_INST_TYPE_UNKNOWN = 255
};

inline NOVA_COIN_INST_TYPE GetCoinInstType(const char *str)
{
    if (!str)
        return NOVA_COIN_INST_TYPE_INIT;
#define DEF_INST_TYPE(_type, _code, _str) \
    if (strcasecmp(str, _str) == 0)       \
        return NOVA_COIN_INST_TYPE_##_type;
#include "nova_api_inst_type_list.h"
#undef DEF_INST_TYPE
    return NOVA_COIN_INST_TYPE_UNKNOWN;
};

inline const char *GetCoinInstTypeString(uint8_t inst_type)
{
#define DEF_INST_TYPE(_type, _code, _str)         \
    if (inst_type == NOVA_COIN_INST_TYPE_##_type) \
        return _str;
#include "nova_api_inst_type_list.h"
#undef DEF_INST_TYPE
    return "UNKNOWN";
};