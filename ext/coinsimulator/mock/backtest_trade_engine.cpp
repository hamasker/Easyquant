#include "mock/backtest_trade_engine.h"
#include "base/base_log.h"

BEGIN_NOVA_NAMESPACE(trade)

BacktestTradeEngine::BacktestTradeEngine(NOVA_EXCHANGE_TYPE exch,
                                         NOVA_COIN_INST_TYPE inst)
    : MockTradeEngine(exch, inst) {
  is_mock_ = true;
}

void BacktestTradeEngine::DoSendOrder(const NovaOrderDetail *order) {
  // 回测模式：按 order 价格立即模拟成交
  if (!order) return;

  auto instrument_id = order->order.instrument_id;
  double qty = order->order.quantity;
  double price = order->order.price;

  INFO_FLOG("[Backtest] Simulated fill: {} qty={} @ price={}",
            instrument_id.GetSymbol(), qty, price);

  auto *acc_pos = GetOrCreateAccountPosition(instrument_id);
  if (acc_pos) {
    if (order->order.side == NOVA_SIDE_BUY) {
      acc_pos->long_position += qty;
    } else {
      acc_pos->short_position += qty;
    }
  }
}

void BacktestTradeEngine::DoCancelOrder(const NovaOrderDetail *order) {
  if (!order) return;
  INFO_FLOG("[Backtest] Cancel order: {}", order->order.instrument_id.GetSymbol());
}

END_NOVA_NAMESPACE(trade)
