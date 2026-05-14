#pragma once

#include "base_common.h"

#include "nova_api_struct.h"
#include "quote_data_type.h"

USE_NOVA_NAMESPACE(base)

BEGIN_NOVA_NAMESPACE(quote)

struct QuoteMetaItem {
  InstrumentId Inst;
  NOVA_COIN_QUOTE_TYPE Type;
};

struct QuoteBaseConsts {
  static constexpr size_t QUEUE_CAPACITY = 4096;
  static constexpr size_t META_QUEUE_CAPACITY = 1024 * 128;
  static constexpr size_t DEPTH_QUEUE_CAPACITY = QUEUE_CAPACITY;
  static constexpr size_t DEPTH_LVN_QUEUE_CAPACITY = DEPTH_QUEUE_CAPACITY;
  static constexpr size_t TRADE_QUEUE_CAPACITY = QUEUE_CAPACITY * 16;
  static constexpr size_t BBO_QUEUE_CAPACITY = QUEUE_CAPACITY * 16;
  static constexpr size_t BAR_QUEUE_CAPACITY = QUEUE_CAPACITY;

  static constexpr size_t VAR_QUEUE_BYTES = 1024 * 1024;

  using Depth = NovaCoinDepth;
  using DepthLVN = NovaCoinDepthLVN;
  using Trade = NovaCoinTrade;
  using BBO = NovaCoinBBO;
  using Bar = NovaCoinBar;
  // using Variant = NovaCoinVariant;

  static constexpr auto NOVA_QUOTE_FMT = "NOVA_QUOTE_%s%s/%s/%s";
  static constexpr auto META_QUEUE_NAME = "META";
};

END_NOVA_NAMESPACE(quote)
