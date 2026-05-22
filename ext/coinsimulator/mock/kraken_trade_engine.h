#pragma once

#include "mock/rest_trade_engine.h"

BEGIN_NOVA_NAMESPACE(trade)

/**
 * KrakenTradeEngine — Kraken 实盘交易引擎。
 */
class KrakenTradeEngine : public RestTradeEngine {
public:
  KrakenTradeEngine();

  const char *name() const override { return "kraken"; }

protected:
  void DoSendOrder(const NovaOrderDetail *order) override;
  void DoCancelOrder(const NovaOrderDetail *order) override;

private:
  // HMAC-SHA512 → base64
  static std::string HmacSha512B64(const std::string &key,
                                   const std::string &data);

  uint64_t nonce_ = 0;
};

END_NOVA_NAMESPACE(trade)

