#pragma once

#include "base/base_os_time.h"
#include "base/base_server.h"
#include "base/base_util.h"

#include "nova_api_data_type.h"
#include "quote/quote_data_type.h"
#include "quote/quote_struct.h"
#include "trade/trade_data_type.h"

#include "nova_api_struct.h"
#include "nova_api_trade_struct.h"
#include "nova_api_util.h"

USE_NOVA_NAMESPACE(quote)

BEGIN_NOVA_NAMESPACE(trade)

class Strategy;
class TradeEngine;

enum OrderEventStatusNeedFill {
  ORDER_EVENT_STATUS_NEED_FILL_INIT = 0,
  ORDER_EVENT_STATUS_NEED_FILL_ABORT = 1,
};

struct NovaOrderDetail : public NovaOrder {
  Strategy *strategy;
  TradeEngine *trade_engine;

public:
  OrderEventStatusNeedFill OnAccept(uint64_t ts, uint64_t exch_intime,
                                    uint64_t exch_outtime);

  OrderEventStatusNeedFill OnReject(uint64_t ts, uint64_t exch_intime,
                                    uint64_t exch_outtime);

  OrderEventStatusNeedFill OnExecution(double total_traded, double price,
                                       double p_acc_fee, uint64_t ts);

  OrderEventStatusNeedFill OnCancel(double total_traded, double cancelled,
                                    uint64_t ts, uint64_t exch_intime,
                                    uint64_t exch_outtime);

  OrderEventStatusNeedFill OnAmended(int32_t reason, uint64_t ts,
                                     uint64_t exch_intime,
                                     uint64_t exch_outtime);
};

struct NovaOrderEvent {
  NOVA_ORDER_EVENT_STATUS event_status;
  uint64_t timestamp;
  uint64_t exch_intime;
  uint64_t exch_outtime;
  NovaOrderDetail *order;
  double qty_traded;
  double qty_cancelled;
  double avg_price;
  double acc_fee;
  int32_t reason;

public:
  void OnAccept(NovaOrderDetail *o, uint64_t ts = 0, uint64_t exch_in = 0,
                uint64_t exch_out = 0) {
    event_status = NOVA_ORDER_EVENT_STATUS_ACCEPTED;
    timestamp = ts;
    exch_intime = exch_in;
    exch_outtime = exch_out;
    order = o;
    qty_traded = 0;
    qty_cancelled = 0;
  }

  void OnReject(NovaOrderDetail *o, uint64_t ts = 0, int32_t reason = 0,
                uint64_t exch_in = 0, uint64_t exch_out = 0) {
    event_status = NOVA_ORDER_EVENT_STATUS_REJECTED;
    timestamp = ts;
    exch_intime = exch_in;
    exch_outtime = exch_out;
    order = o;
    qty_traded = 0;
    qty_cancelled = 0;
    this->reason = reason;
  }

  void OnUpdate(NovaOrderDetail *o, double qty_trd, double qty_cxl,
                double price, double acc_fee, uint64_t ts = 0,
                uint64_t exch_in = 0, uint64_t exch_out = 0) {
    event_status = NOVA_ORDER_EVENT_STATUS_FILLED;
    timestamp = ts;
    exch_intime = exch_in;
    exch_outtime = exch_out;
    order = o;
    qty_traded = qty_trd;
    qty_cancelled = qty_cxl;
    avg_price = price;
    this->acc_fee = acc_fee;
  }

  void OnCancel(NovaOrderDetail *o, double qty_trd, double qty_cxl,
                uint64_t ts = 0, uint64_t exch_in = 0, uint64_t exch_out = 0) {
    NOVA_ASSERT(qty_cxl > 0);
    event_status = NOVA_ORDER_EVENT_STATUS_CANCELLED;
    timestamp = ts;
    exch_intime = exch_in;
    exch_outtime = exch_out;
    order = o;
    qty_traded = qty_trd;
    qty_cancelled = qty_cxl;
  }

  void OnCancelFailed(NovaOrderDetail *o, uint64_t ts = 0, int32_t reason = 0,
                      uint64_t exch_in = 0, uint64_t exch_out = 0) {
    event_status = NOVA_ORDER_EVENT_STATUS_CANCELL_FAILED;
    timestamp = ts;
    exch_intime = exch_in;
    exch_outtime = exch_out;
    order = o;
    this->reason = reason;
  }

  void OnAmended(NovaOrderDetail *o, int32_t reason, uint64_t ts = 0,
                 uint64_t exch_in = 0, uint64_t exch_out = 0) {
    if (reason == 0)
      event_status = NOVA_ORDER_EVENT_STATUS_AMENDED;
    else
      event_status = NOVA_ORDER_EVENT_STATUS_AMEND_FAILED;
    timestamp = ts;
    exch_intime = exch_in;
    exch_outtime = exch_out;
    order = o;
    this->reason = reason;
  }
};

union NovaRequestId {
  uint32_t u32;
  struct {
    uint32_t sequence : 24;
    uint32_t client_id : 8;
  };
  static NovaRequestId Create(NovaOrderId ord_id, uint8_t client_id) {
    NovaRequestId ret;
    ret.sequence = ord_id.sequence;
    ret.client_id = client_id;
    return ret;
  }
};

struct TradeBaseConsts {
  static constexpr size_t QUEUE_CAPACITY = 10240;
  static constexpr size_t MAX_STRATEGY_SIZE = 16;
};

END_NOVA_NAMESPACE(trade)
