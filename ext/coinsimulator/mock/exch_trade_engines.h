#pragma once

#include "mock/rest_trade_engine.h"

BEGIN_NOVA_NAMESPACE(trade)

// ========== Binance 现货 ==========
class BinanceTradeEngine : public RestTradeEngine {
public:
  BinanceTradeEngine() : RestTradeEngine(NOVA_EXCHANGE_BINANCE, NOVA_COIN_INST_TYPE_SPOT) {}
  const char *name() const override { return "binance"; }

protected:
  void DoSendOrder(const NovaOrderDetail *order) override;
  void DoCancelOrder(const NovaOrderDetail *order) override;
};

// ========== Binance 永续 ==========
class BinanceSwapTradeEngine : public RestTradeEngine {
public:
  BinanceSwapTradeEngine() : RestTradeEngine(NOVA_EXCHANGE_BINANCE, NOVA_COIN_INST_TYPE_SWAP) {}
  const char *name() const override { return "binance_swap"; }

protected:
  void DoSendOrder(const NovaOrderDetail *order) override;
  void DoCancelOrder(const NovaOrderDetail *order) override;
};

// ========== OKX 现货 ==========
class OKXTradeEngine : public RestTradeEngine {
public:
  OKXTradeEngine() : RestTradeEngine(NOVA_EXCHANGE_OK, NOVA_COIN_INST_TYPE_SPOT) {}
  const char *name() const override { return "okx"; }

protected:
  void DoSendOrder(const NovaOrderDetail *order) override;
  void DoCancelOrder(const NovaOrderDetail *order) override;
};

// ========== Coinbase 现货 ==========
class CoinbaseTradeEngine : public RestTradeEngine {
public:
  CoinbaseTradeEngine() : RestTradeEngine(NOVA_EXCHANGE_COINBASE, NOVA_COIN_INST_TYPE_SPOT) {}
  const char *name() const override { return "coinbase"; }

protected:
  void DoSendOrder(const NovaOrderDetail *order) override;
  void DoCancelOrder(const NovaOrderDetail *order) override;
};

END_NOVA_NAMESPACE(trade)
