#include "mock/mock_trade_engine.h"
#include "coinrunner_log.h"

#include "nova_trader_api.h"
#include "base/base_async_log.h"

#include <chrono>
#include <cmath>
#include <cstdio>

BEGIN_NOVA_NAMESPACE(trade)

MockTradeEngine::MockTradeEngine(NOVA_EXCHANGE_TYPE exch,
                                 NOVA_COIN_INST_TYPE inst) {
  is_mock_ = true;
  exchange_ = exch;
  inst_type_ = inst;

  report_queue_ = new ReportQueueTp();
}

MockTradeEngine::ReportQueueTp *MockTradeEngine::message_queue() {
  return report_queue_;
}

FundAssetManager *MockTradeEngine::fund_asset() {
  return &fund_assets_;
}

MockTradeEngine::AccountPositionManagerTp *
MockTradeEngine::AccountPositionInfo() {
  return &account_positions_;
}

const MockTradeEngine::OrderResumeMgr *MockTradeEngine::order_resume() const {
  return &order_resume_;
}

const MockTradeEngine::TradeResumeMgr *MockTradeEngine::trade_resume() const {
  return &trade_resume_;
}

bool MockTradeEngine::QueryAccountPosition(uint64_t, const Config *) {
  return true;
}

bool MockTradeEngine::QueryAccountFundAsset(uint64_t, const Config *) {
  return true;
}

NOVA_EXCHANGE_TYPE MockTradeEngine::account_exchange() const {
  return exchange_;
}

NOVA_COIN_INST_TYPE MockTradeEngine::target_inst_type() const {
  return inst_type_;
}

double MockTradeEngine::GetRateLimit(const std::string &) { return 225.0; }

// ========== BBO 更新 ==========

void MockTradeEngine::UpdateBBO(const InstrumentId &inst, double bid,
                                double ask, double bid_qty, double ask_qty) {
  auto key = inst.key();
  auto &bbo = bbo_map_[key];
  bbo.bid = bid;
  bbo.ask = ask;
  bbo.bid_qty = bid_qty;
  bbo.ask_qty = ask_qty;
  bbo.update_time = 0;

  // 新行情到达, 检查是否有挂单可以成交
  auto it = order_books_.find(key);
  if (it != order_books_.end()) {
    auto &orders = it->second;
    auto oit = orders.begin();
    while (oit != orders.end()) {
      if (TryMatch(*oit)) {
        oit = orders.erase(oit);
      } else {
        ++oit;
      }
    }
  }
}

void MockTradeEngine::MatchAll() {
  for (auto &[key, orders] : order_books_) {
    auto it = orders.begin();
    while (it != orders.end()) {
      if (TryMatch(*it)) {
        it = orders.erase(it);
      } else {
        ++it;
      }
    }
  }
}

// ========== 撮合逻辑 ==========

bool MockTradeEngine::TryMatch(PendingOrder &po) {
  auto &order = po.order;

  // 撮合延迟: 订单提交后需等待 match_delay_ms_ 毫秒
  if (match_delay_ms_ > 0 && po.accept_time > 0) {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    if (now_ms - po.accept_time < match_delay_ms_) return false;
  }

  auto key = order.order.instrument_id.key();

  auto bit = bbo_map_.find(key);
  if (bit == bbo_map_.end()) return false; // 还没有行情

  auto &bbo = bit->second;
  if (bbo.bid <= 0 || bbo.ask <= 0) return false;

  double fill_price = 0;
  double fill_qty = 0;

  // 自成交检查: 新单是否会和本地反向挂单冲突
  auto oit = order_books_[key].begin();
  while (oit != order_books_[key].end()) {
    auto &other = *oit;
    bool conflict = false;
    if (order.order.side == NOVA_SIDE_BUY &&
        other.order.order.side == NOVA_SIDE_SELL &&
        order.order.price >= other.order.order.price) {
      conflict = true;
    } else if (order.order.side == NOVA_SIDE_SELL &&
               other.order.order.side == NOVA_SIDE_BUY &&
               order.order.price <= other.order.order.price) {
      conflict = true;
    }
    if (conflict) {
      INFO_FLOG("[MockTrade] Self-trade rejected: new {} vs existing {} "
                "price={} vs {}",
                order.order.side == NOVA_SIDE_BUY ? "BUY" : "SELL",
                other.order.order.side == NOVA_SIDE_BUY ? "BUY" : "SELL",
                order.order.price, other.order.order.price);
      NotifyRejected(&order);
      return true;
    }
    ++oit;
  }

  // 外部 BBO 撮合
  if (order.order.side == NOVA_SIDE_BUY) {
    if (order.order.price >= bbo.ask && bbo.ask_qty > 0) {
      fill_price = bbo.ask;
      bool crossed = order.order.price > bbo.ask;
      double avail = crossed ? bbo.ask_qty
                             : bbo.ask_qty * (match_fill_ratio_ / 100.0);
      fill_qty = std::min(order.qty_left, avail);
    }
  } else {
    if (order.order.price <= bbo.bid && bbo.bid_qty > 0) {
      fill_price = bbo.bid;
      bool crossed = order.order.price < bbo.bid;
      double avail = crossed ? bbo.bid_qty
                             : bbo.bid_qty * (match_fill_ratio_ / 100.0);
      fill_qty = std::min(order.qty_left, avail);
    }
  }

  if (fill_qty <= 0) return false;

  // 成交
  order.qty_traded += fill_qty;
  order.qty_left -= fill_qty;
  order.avg_price = (order.avg_price * (order.qty_traded - fill_qty) +
                     fill_price * fill_qty) /
                    order.qty_traded;

  // 精简版手续费: taker_fee 0.1%
  order.acc_fee += fill_price * fill_qty * 0.001;

  // 更新仓位
  if (order.position) {
    if (order.order.side == NOVA_SIDE_BUY) {
      order.position->long_position += fill_qty;
    } else {
      order.position->short_position += fill_qty;
    }
  }

  if (order.qty_left <= 1e-12) {
    // 完全成交
    order.order_status = NOVA_ORDER_STATUS_FILLED;
    NotifyFilled(&order);
    return true; // 从订单簿移除
  } else {
    // 部分成交
    order.order_status = NOVA_ORDER_STATUS_PARTIALLY_FILLED;
    NotifyPartialFill(&order);
    return false;
  }
}

// ========== 策略回调 ==========

void MockTradeEngine::NotifyAccepted(NovaOrder *order) {
  if (!strategy_) return;
  order->order_status = NOVA_ORDER_STATUS_ACCEPTED;
  order->response_time = 0;
  strategy_->on_order_accepted(order, order->position);
  RecordOrder(*order, "ACCEPTED");
}

void MockTradeEngine::NotifyFilled(NovaOrder *order) {
  if (!strategy_) return;
  order->order_status = NOVA_ORDER_STATUS_FILLED;
  strategy_->on_order_update(order, order->position);
  strategy_->on_order_done(order, order->position, NOVA_ORDER_STATUS_FILLED);
  RecordOrder(*order, "FILLED");
}

void MockTradeEngine::NotifyPartialFill(NovaOrder *order) {
  if (!strategy_) return;
  strategy_->on_order_update(order, order->position);
}

void MockTradeEngine::NotifyCancelled(NovaOrder *order) {
  if (!strategy_) return;
  order->order_status = NOVA_ORDER_STATUS_CANCELLED;
  strategy_->on_order_cancelled(order, order->position, order->qty_left);
  RecordOrder(*order, "CANCELLED");
}

void MockTradeEngine::NotifyRejected(NovaOrder *order) {
  if (!strategy_) return;
  order->order_status = NOVA_ORDER_STATUS_REJECTED;
  INFO_FLOG("[MockTrade] Order #{} rejected: {} qty={} price={}",
            order->nova_id.sequence,
            order->order.side == NOVA_SIDE_BUY ? "BUY" : "SELL",
            order->order.quantity, order->order.price);
  strategy_->on_order_rejected(order, order->position, 0);
  RecordOrder(*order, "REJECTED");
}

void MockTradeEngine::RecordOrder(const NovaOrder &order, const char *event) {
  if (record_dir_.empty()) return;
  // format: timestamp,event,id,side,price,qty,qty_traded,qty_left,status
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  std::string path = record_dir_ + "/orders.csv";
  FILE *f = fopen(path.c_str(), "a");
  if (f) {
    fprintf(f, "%ld,%s,%u,%d,%.6f,%.6f,%.6f,%.6f,%d\n", now, event,
            order.nova_id.sequence, (int)order.order.side, order.order.price,
            order.order.quantity, order.qty_traded, order.qty_left,
            (int)order.order_status);
    fclose(f);
  }
}

// ========== 订单操作 ==========

void MockTradeEngine::DoSendOrder(const NovaOrderDetail *detail) {
  if (!detail || !strategy_) return;

  // 拒单检查
  if (detail->order.quantity <= 0 || detail->order.price <= 0 ||
      !detail->order.instrument_id.Valid()) {
    PendingOrder po;
    po.order.order = detail->order;
    po.order.position = detail->position;
    po.order.nova_id.sequence = next_order_id_++;
    NotifyRejected(&po.order);
    return;
  }

  PendingOrder po;
  po.order.order = detail->order;
  po.order.position = detail->position;
  po.order.qty_left = detail->order.quantity;
  po.order.qty_traded = 0;
  po.order.avg_price = 0;
  po.order.acc_fee = 0;
  po.order.nova_id.sequence = next_order_id_++;
  po.order.create_time = 0;
  po.accept_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  auto key = detail->order.instrument_id.key();

  // 接受订单
  NotifyAccepted(&po.order);

  // 立即尝试撮合
  if (TryMatch(po)) {
    // 已完全成交, 订单已从 book 移除
    INFO_FLOG("[MockTrade] Order #{} filled immediately: {} {}@{}",
              po.order.nova_id.sequence,
              po.order.order.side == NOVA_SIDE_BUY ? "BUY" : "SELL",
              po.order.qty_traded, po.order.avg_price);
    return;
  }

  // 未完全成交, 加入订单簿
  order_books_[key].push_back(std::move(po));
  auto &stored = order_books_[key].back();
  detail_to_order_[detail] = &stored;
  // 同步 nova_id 回原对象 (供 CancelOrder 查找)
  const_cast<NovaOrderDetail *>(detail)->nova_id.sequence =
      stored.order.nova_id.sequence;
  INFO_FLOG("[MockTrade] Order #{} accepted: {} qty={} price={} left={}",
            stored.order.nova_id.sequence,
            stored.order.order.side == NOVA_SIDE_BUY ? "BUY" : "SELL",
            stored.order.order.quantity, stored.order.order.price,
            stored.order.qty_left);
}

void MockTradeEngine::DoCancelOrder(const NovaOrderDetail *detail) {
  if (!detail) return;

  // 先按指针精确查找
  auto dit = detail_to_order_.find(detail);
  if (dit != detail_to_order_.end()) {
    auto *po = dit->second;
    auto key = detail->order.instrument_id.key();
    auto it = order_books_.find(key);
    if (it != order_books_.end()) {
      for (auto oit = it->second.begin(); oit != it->second.end(); ++oit) {
        if (&*oit == po) {
          NotifyCancelled(&oit->order);
          it->second.erase(oit);
          detail_to_order_.erase(dit);
          INFO_FLOG("[MockTrade] Order #{} cancelled",
                    detail->nova_id.sequence);
          return;
        }
      }
    }
  }

  // 不在订单簿中 → cancel failed
  if (strategy_) {
    auto *order = const_cast<NovaOrder *>(
        static_cast<const NovaOrder *>(detail));
    strategy_->on_order_cancel_failed(order, order->position, 1);
  }
}

void MockTradeEngine::DoBatchSendOrder(const NovaOrderDetail **orders,
                                       size_t len) {
  for (size_t i = 0; i < len; ++i) {
    DoSendOrder(orders[i]);
  }
}

void MockTradeEngine::DoBatchCancelOrder(const NovaOrderDetail **orders,
                                         size_t len) {
  for (size_t i = 0; i < len; ++i) {
    DoCancelOrder(orders[i]);
  }
}

bool MockTradeEngine::DoAmendOrder(const NovaOrderDetail *order) {
  (void)order;
  return false;
}

END_NOVA_NAMESPACE(trade)
