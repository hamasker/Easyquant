#pragma once

#include "base/base_util.h"

#include "nova_api_data_type.h"

BEGIN_NOVA_NAMESPACE(trade)

#define CONFIG_TRADE "Trade"

#define CONFIG_STRATEGY "Strategy"
#define CONFIG_INSTRUMENT "instrument"
#define CONFIG_LONG "long"
#define CONFIG_SHORT "short"

#define CONFIG_TRADE_ENGINE ".TradeEngine"
#define CONFIG_EXCHANGE ".exchange"

#define CONFIG_STRATEGY_PATH "Strategy.path"
#define CONFIG_STRATEGY_MANAGER "Strategy.manager"

#define CONFIG_AUTH_LIB_KEY "CoinAuthLib"
#define CONFIG_AUTH_CODE_KEY "CoinAuthCode"

static constexpr int32_t NOVA_TRADE_ERR_NO_INFORMATION = -1;
static constexpr int32_t NOVA_TRADE_ERR_API_REJECT = -2;

END_NOVA_NAMESPACE(trade)