#pragma once

#include "nova_api_data_type.h"
#include "nova_api_quote_struct.h"

BEGIN_NOVA_NAMESPACE(trade)

USE_NOVA_NAMESPACE(quote)

union NovaOrderId {
  struct {
    uint32_t u32;
  };
  struct {
    uint32_t id;
  };
  struct {
    uint32_t sequence;
  };
};

struct Order {
  InstrumentId instrument_id;

  NOVA_EXCHANGE_TYPE exchange;
  NOVA_PRICE_TYPE price_type;
  NOVA_SIDE_TYPE side;
  NOVA_POSITION_EFFECT_TYPE position_effect;

  double quantity;
  double price;

  uint64_t user_data;
};

class SecurityPosition;

struct NovaOrder {
  Order order;
  SecurityPosition *position;
  double qty_left;
  double qty_traded;
  double avg_price;
  double acc_fee;
  double amended_price;
  double current_price;

  union {
    uint64_t event_timestamp;
    uint64_t event_ns;
  };
  NovaOrderId nova_id;
  NOVA_ORDER_STATUS order_status;
  bool recycled;
  char filler_[2];

  int64_t create_time;
  int64_t send_time;
  int64_t amend_time;
  int64_t response_time;

  int64_t send_exch_intime;
  int64_t send_exch_outtime;

  int64_t cancel_time;
  int64_t cancel_rsp_time;
  int64_t cancel_exch_intime;
  int64_t cancel_exch_outtime;
};

END_NOVA_NAMESPACE(trade)