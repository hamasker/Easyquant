#pragma once

#include "nova_api_trade_struct.h"
#include "nova_base.h"

#include <unordered_map>

BEGIN_NOVA_NAMESPACE(trade)

class TradeEngine;

class AccountPosition {
public:
  AccountPosition(::nova::quote::InstrumentId ins_id) : instrument_id(ins_id) {
    Reset();
  }
  ~AccountPosition() = default;

public:
  void Reset() {
    long_position = 0;
    short_position = 0;
    long_frozen = 0;
    long_frozen = 0;
  }
  bool HashNoFrozen() const { return long_frozen + short_frozen == 0; }

  double LongAvailable() const { return long_frozen - short_frozen; }
  double ShortAvailable() const { return short_frozen - long_frozen; }

  void Update(double long_pos, double short_pos, double long_frz,
              double short_frz, uint64_t ns = 0) {
    long_position = long_pos;
    short_position = short_pos;
    long_frozen = long_frz;
    short_frozen = long_frz;
    timestamp = ns;
  }

public:
  ::nova::quote::InstrumentId instrument_id;

  double long_position;
  double short_position;
  double long_frozen;
  double short_frozen;
  double unrealized_pnl;
  uint64_t timestamp;
};

struct FundAsset {
  using MoneyType = double;
  MoneyType fund_ava_balance;
  MoneyType fund_frozen;
  MoneyType total_asset;
};

class SecurityPosition {
public:
  SecurityPosition(InstrumentId ins_id, AccountPosition *a, double long_blocked,
                   double short_blocked)
      : instrument(ins_id), long_position(0), short_position(0),
        long_blocked_(long_blocked), short_blocked_(short_blocked),
        acc_pos_(a) {
    UpdatePosition();
  }
  void UpdatePosition() {
    if (acc_pos_ == nullptr)
      return;
    long_position = acc_pos_->long_position - long_blocked_;
    short_position = acc_pos_->short_position - short_blocked_;
    slow_if(long_position < 0 || short_position < 0) {
      // ERROR_FLOG("Negative position, symbol: {}, accLong: {},accShort: {},
      // long: {}, short: {}",
      //            instrument.symbol, acc_pos_->long_position,
      //            acc_pos_->short_position, long_position, short_position);
    }
    NOVA_ASSERT(long_position >= 0 && short_position >= 0);
  }

  double net_qty() const { return long_pos() - short_pos(); }
  double abs_qty() const { return std::abs(long_position - short_position); }
  bool empty() const { return long_position == short_position; }

  double long_pos() const { return long_position - long_blocked_; }
  double short_pos() const { return short_position - short_blocked_; }

public:
  InstrumentId instrument;
  double long_position;
  double short_position;
  double long_blocked_;
  double short_blocked_;
  TradeEngine *trade_engine_ = nullptr;
  AccountPosition *acc_pos_ = nullptr;
};

static inline void ChangePositionToSingleSide(SecurityPosition *posi) {
  if (posi->acc_pos_) {
    auto *acc_pos = posi->acc_pos_;
    auto long_remain = acc_pos->long_position - acc_pos->long_frozen;
    long_remain =
        std::max(acc_pos->long_position - posi->long_blocked_, long_remain);
    auto short_remain = acc_pos->short_position - acc_pos->short_frozen;
    short_remain =
        std::max(acc_pos->short_position - posi->short_blocked_, short_remain);
    auto max_delta = std::min(long_remain, short_remain);
    if (max_delta > 0) {
      acc_pos->long_position -= max_delta;
      acc_pos->short_position -= max_delta;
      posi->UpdatePosition();
    }
  } else {
    auto long_remain = posi->long_position - posi->long_blocked_;
    auto short_remain = posi->short_position - posi->short_blocked_;
    auto max_delta = std::min(long_remain, short_remain);
    if (max_delta > 0) {
      posi->long_position -= max_delta;
      posi->short_position -= max_delta;
    }
  }
}

using AccountPositionManager =
    std::unordered_map<InstrumentId::Key, AccountPosition *>;
using FundAssetManager = std::unordered_map<std::string, FundAsset *>;

END_NOVA_NAMESPACE(trade)