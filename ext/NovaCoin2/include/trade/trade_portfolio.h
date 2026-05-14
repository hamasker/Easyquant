#pragma once

#include <unordered_map>
#include <unordered_set>

#include "base/base_util.h"

#include "trade/trade_struct.h"

BEGIN_NOVA_NAMESPACE(trade)

using SecurityAsset = std::unordered_map<uint64_t, SecurityPosition *>;

struct Portfolio {
  // FundAsset fund_asset;
  SecurityAsset security_asset;
};

END_NOVA_NAMESPACE(trade)