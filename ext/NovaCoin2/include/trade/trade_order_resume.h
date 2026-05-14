#pragma once

#include "trade/trade_util.h"

#include "vector"

USE_NOVA_NAMESPACE(base)
USE_NOVA_NAMESPACE(quote)

BEGIN_NOVA_NAMESPACE(trade)

struct NovaOrderResumeRecord {
  union {
    uint64_t instrument;
    InstrumentId instrument_id;
  };
  NOVA_SIDE_TYPE side;
  NOVA_POSITION_EFFECT_TYPE position_effect;

  double price;

  int32_t quantity;
  int32_t qty_traded;
  NovaOrderId nova_id;
  int64_t broker_id;

  NovaOrderResumeRecord()
      : instrument(0), side(NOVA_SIDE_INIT),
        position_effect(NOVA_POSITION_EFFECT_INIT), price(0), quantity(0),
        qty_traded(0), nova_id(), broker_id(0) {}
};

struct NovaTradeResumeRecord {
  union {
    uint64_t instrument;
    InstrumentId instrument_id;
  };
  NOVA_SIDE_TYPE side;
  NOVA_POSITION_EFFECT_TYPE position_effect;

  double trade_price;
  int32_t trade_qty;

  NovaOrderId order_id;
  int64_t trade_id;

  NovaTradeResumeRecord()
      : instrument(0), side(NOVA_SIDE_INIT),
        position_effect(NOVA_POSITION_EFFECT_INIT), trade_price(0),
        trade_qty(0), order_id(), trade_id(0) {}
};

using NovaOrderResumeRecordVector = std::vector<NovaOrderResumeRecord *>;
using NovaTradeResumeRecordVector = std::vector<NovaTradeResumeRecord *>;

using NovaOrderResumeMgr = NovaOrderResumeRecordVector;
using NovaTradeResumeMgr = NovaTradeResumeRecordVector;

END_NOVA_NAMESPACE(trade)