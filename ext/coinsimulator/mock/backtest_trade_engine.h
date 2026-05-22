#pragma once

#include "mock/mock_trade_engine.h"

BEGIN_NOVA_NAMESPACE(trade)

/**
 * BacktestTradeEngine — 回测模式引擎。
 * 继承 MockTradeEngine，增加模拟撮合：
 *   - DoSendOrder 会立即模拟成交
 *   - 维护 simulated positions
 */
class BacktestTradeEngine : public MockTradeEngine {
public:
  BacktestTradeEngine(NOVA_EXCHANGE_TYPE exch = NOVA_EXCHANGE_BACKTEST,
                      NOVA_COIN_INST_TYPE inst = NOVA_COIN_INST_TYPE_SPOT);

  const char *name() const override { return "backtest"; }

protected:
  void DoSendOrder(const NovaOrderDetail *order) override;
  void DoCancelOrder(const NovaOrderDetail *order) override;
};

END_NOVA_NAMESPACE(trade)
