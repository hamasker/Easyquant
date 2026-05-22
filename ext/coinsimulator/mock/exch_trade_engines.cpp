#include "mock/exch_trade_engines.h"
#include "coinrunner_log.h"

#include "nova_api_instrument.h"
#include "nlohmann_json/json.hpp"

#include <chrono>

BEGIN_NOVA_NAMESPACE(trade)

namespace {

// tool: extract pair name without exchange suffix
std::string GetPair(const InstrumentId &inst_id) {
  std::string sym = inst_id.GetSymbol();
  auto dot = sym.rfind('.');
  return dot != std::string::npos ? sym.substr(0, dot) : sym;
}

// timestamp in ms
uint64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

} // namespace

// ============================================================
// Binance 现货
// ============================================================

void BinanceTradeEngine::DoSendOrder(const NovaOrderDetail *detail) {
  if (!detail || api_key_.empty()) { MockTradeEngine::DoSendOrder(detail); return; }
  if (!CheckRateLimit()) { MockTradeEngine::DoSendOrder(detail); return; }

  std::string sym = GetPair(detail->order.instrument_id);
  std::transform(sym.begin(), sym.end(), sym.begin(), ::toupper);
  std::string side = detail->order.side == NOVA_SIDE_BUY ? "BUY" : "SELL";

  uint64_t ts = NowMs();
  std::string params = "symbol=" + sym + "&side=" + side +
                       "&type=LIMIT&timeInForce=GTC" +
                       "&quantity=" + std::to_string(detail->order.quantity) +
                       "&price=" + std::to_string(detail->order.price) +
                       "&timestamp=" + std::to_string(ts);
  std::string sign = HmacSha256Hex(api_secret_, params);
  params += "&signature=" + sign;

  std::string resp =
      HttpsPost(api_host_.empty() ? "api.binance.com" : api_host_.c_str(), 443, "/api/v3/order", params,
                {{"X-MBX-APIKEY", api_key_},
                 {"Content-Type", "application/x-www-form-urlencoded"}});

  INFO_FLOG("[Binance] Order: {} {} qty={} price={} resp={}", sym, side,
            detail->order.quantity, detail->order.price, resp);

  MockTradeEngine::DoSendOrder(detail);
}

void BinanceTradeEngine::DoCancelOrder(const NovaOrderDetail *detail) {
  if (!detail || api_key_.empty()) { MockTradeEngine::DoCancelOrder(detail); return; }

  std::string sym = GetPair(detail->order.instrument_id);
  std::transform(sym.begin(), sym.end(), sym.begin(), ::toupper);

  // Binance cancel needs orderId from exchange, fallback to nova_id
  uint64_t ts = NowMs();
  std::string params = "symbol=" + sym +
                       "&origClientOrderId=" + std::to_string(detail->nova_id.sequence) +
                       "&timestamp=" + std::to_string(ts);
  std::string sign = HmacSha256Hex(api_secret_, params);
  params += "&signature=" + sign;

  std::string resp =
      HttpsPost(api_host_.empty() ? "api.binance.com" : api_host_.c_str(), 443, "/api/v3/order", params,
                {{"X-MBX-APIKEY", api_key_},
                 {"Content-Type", "application/x-www-form-urlencoded"}});

  INFO_FLOG("[Binance] Cancel: {} resp={}", sym, resp);
  MockTradeEngine::DoCancelOrder(detail);
}

// ============================================================
// Binance 永续
// ============================================================

void BinanceSwapTradeEngine::DoSendOrder(const NovaOrderDetail *detail) {
  if (!detail || api_key_.empty()) { MockTradeEngine::DoSendOrder(detail); return; }
  if (!CheckRateLimit()) { MockTradeEngine::DoSendOrder(detail); return; }

  std::string sym = GetPair(detail->order.instrument_id);
  std::transform(sym.begin(), sym.end(), sym.begin(), ::toupper);
  std::string side = detail->order.side == NOVA_SIDE_BUY ? "BUY" : "SELL";

  uint64_t ts = NowMs();
  std::string params = "symbol=" + sym + "&side=" + side +
                       "&type=LIMIT&timeInForce=GTC" +
                       "&quantity=" + std::to_string(detail->order.quantity) +
                       "&price=" + std::to_string(detail->order.price) +
                       "&timestamp=" + std::to_string(ts);
  std::string sign = HmacSha256Hex(api_secret_, params);
  params += "&signature=" + sign;

  std::string resp =
      HttpsPost(api_host_.empty() ? "fapi.binance.com" : api_host_.c_str(), 443, "/fapi/v1/order", params,
                {{"X-MBX-APIKEY", api_key_},
                 {"Content-Type", "application/x-www-form-urlencoded"}});

  INFO_FLOG("[BinanceSwap] Order: {} {} qty={} price={} resp={}", sym, side,
            detail->order.quantity, detail->order.price, resp);

  MockTradeEngine::DoSendOrder(detail);
}

void BinanceSwapTradeEngine::DoCancelOrder(const NovaOrderDetail *detail) {
  if (!detail || api_key_.empty()) { MockTradeEngine::DoCancelOrder(detail); return; }

  std::string sym = GetPair(detail->order.instrument_id);
  std::transform(sym.begin(), sym.end(), sym.begin(), ::toupper);

  uint64_t ts = NowMs();
  std::string params = "symbol=" + sym +
                       "&origClientOrderId=" + std::to_string(detail->nova_id.sequence) +
                       "&timestamp=" + std::to_string(ts);
  std::string sign = HmacSha256Hex(api_secret_, params);
  params += "&signature=" + sign;

  std::string resp =
      HttpsPost(api_host_.empty() ? "fapi.binance.com" : api_host_.c_str(), 443, "/fapi/v1/order", params,
                {{"X-MBX-APIKEY", api_key_},
                 {"Content-Type", "application/x-www-form-urlencoded"}});

  INFO_FLOG("[BinanceSwap] Cancel: {} resp={}", sym, resp);
  MockTradeEngine::DoCancelOrder(detail);
}

// ============================================================
// OKX 现货
// ============================================================

void OKXTradeEngine::DoSendOrder(const NovaOrderDetail *detail) {
  if (!detail || api_key_.empty()) { MockTradeEngine::DoSendOrder(detail); return; }
  if (!CheckRateLimit()) { MockTradeEngine::DoSendOrder(detail); return; }

  std::string inst_id = GetPair(detail->order.instrument_id);
  std::transform(inst_id.begin(), inst_id.end(), inst_id.begin(), ::toupper);
  std::string side = detail->order.side == NOVA_SIDE_BUY ? "buy" : "sell";

  std::string ts_str = std::to_string(NowMs() / 1000) + "." +
                       std::to_string(NowMs() % 1000);
  nlohmann::json body = {
      {"instId", inst_id},
      {"tdMode", "cash"},
      {"side", side},
      {"ordType", "limit"},
      {"sz", std::to_string(detail->order.quantity)},
      {"px", std::to_string(detail->order.price)}};
  std::string body_str = body.dump();

  // OKX 签名: Base64(HMAC-SHA256(ts + method + path + body))
  std::string sign_payload = ts_str + "POST" + "/api/v5/trade/order" + body_str;
  std::string sign = Base64Encode(HmacSha256B64(api_secret_, sign_payload));

  std::string resp = HttpsPost(
      api_host_.empty() ? "www.okx.com" : api_host_.c_str(), 443, "/api/v5/trade/order", body_str,
      {{"OK-ACCESS-KEY", api_key_},
       {"OK-ACCESS-SIGN", sign},
       {"OK-ACCESS-TIMESTAMP", ts_str},
       {"OK-ACCESS-PASSPHRASE", api_passphrase_},
       {"Content-Type", "application/json"}});

  INFO_FLOG("[OKX] Order: {} {} qty={} price={} resp={}", inst_id, side,
            detail->order.quantity, detail->order.price, resp);

  MockTradeEngine::DoSendOrder(detail);
}

void OKXTradeEngine::DoCancelOrder(const NovaOrderDetail *detail) {
  if (!detail || api_key_.empty()) { MockTradeEngine::DoCancelOrder(detail); return; }

  std::string inst_id = GetPair(detail->order.instrument_id);
  std::transform(inst_id.begin(), inst_id.end(), inst_id.begin(), ::toupper);

  std::string ts_str =
      std::to_string(NowMs() / 1000);
  nlohmann::json body = {{"instId", inst_id},
                         {"clOrdId", std::to_string(detail->nova_id.sequence)}};
  std::string body_str = body.dump();

  std::string sign_payload = ts_str + "POST" + "/api/v5/trade/cancel-order" + body_str;
  std::string sign = Base64Encode(HmacSha256B64(api_secret_, sign_payload));

  std::string resp = HttpsPost(
      api_host_.empty() ? "www.okx.com" : api_host_.c_str(), 443, "/api/v5/trade/cancel-order", body_str,
      {{"OK-ACCESS-KEY", api_key_},
       {"OK-ACCESS-SIGN", sign},
       {"OK-ACCESS-TIMESTAMP", ts_str},
       {"OK-ACCESS-PASSPHRASE", api_passphrase_},
       {"Content-Type", "application/json"}});

  INFO_FLOG("[OKX] Cancel: {} resp={}", inst_id, resp);
  MockTradeEngine::DoCancelOrder(detail);
}

// ============================================================
// Coinbase 现货
// ============================================================

void CoinbaseTradeEngine::DoSendOrder(const NovaOrderDetail *detail) {
  if (!detail || api_key_.empty()) { MockTradeEngine::DoSendOrder(detail); return; }
  if (!CheckRateLimit()) { MockTradeEngine::DoSendOrder(detail); return; }

  std::string prod_id = GetPair(detail->order.instrument_id);
  std::transform(prod_id.begin(), prod_id.end(), prod_id.begin(), ::toupper);
  std::string side = detail->order.side == NOVA_SIDE_BUY ? "buy" : "sell";

  uint64_t ts = NowMs() / 1000;
  nlohmann::json body = {{"type", "limit"},
                         {"side", side},
                         {"product_id", prod_id},
                         {"price", std::to_string(detail->order.price)},
                         {"size", std::to_string(detail->order.quantity)},
                         {"time_in_force", "GTC"}};
  std::string body_str = body.dump();

  // Coinbase 签名: HMAC-SHA256(ts + method + path + body) → hex
  std::string sign_payload = std::to_string(ts) + "POST" + "/api/v3/brokerage/orders" + body_str;
  std::string sign = HmacSha256Hex(api_secret_, sign_payload);

  std::string resp = HttpsPost(
      api_host_.empty() ? "api.coinbase.com" : api_host_.c_str(), 443, "/api/v3/brokerage/orders", body_str,
      {{"CB-ACCESS-KEY", api_key_},
       {"CB-ACCESS-SIGN", sign},
       {"CB-ACCESS-TIMESTAMP", std::to_string(ts)},
       {"CB-ACCESS-PASSPHRASE", api_passphrase_},
       {"Content-Type", "application/json"}});

  INFO_FLOG("[Coinbase] Order: {} {} qty={} price={} resp={}", prod_id, side,
            detail->order.quantity, detail->order.price, resp);

  MockTradeEngine::DoSendOrder(detail);
}

void CoinbaseTradeEngine::DoCancelOrder(const NovaOrderDetail *detail) {
  if (!detail || api_key_.empty()) { MockTradeEngine::DoCancelOrder(detail); return; }

  uint64_t ts = NowMs() / 1000;
  nlohmann::json body = {{"order_id", std::to_string(detail->nova_id.sequence)}};
  std::string body_str = body.dump();

  std::string sign_payload = std::to_string(ts) + "POST" + "/api/v3/brokerage/orders/batch_cancel" + body_str;
  std::string sign = HmacSha256Hex(api_secret_, sign_payload);

  std::string resp = HttpsPost(
      api_host_.empty() ? "api.coinbase.com" : api_host_.c_str(), 443, "/api/v3/brokerage/orders/batch_cancel", body_str,
      {{"CB-ACCESS-KEY", api_key_},
       {"CB-ACCESS-SIGN", sign},
       {"CB-ACCESS-TIMESTAMP", std::to_string(ts)},
       {"CB-ACCESS-PASSPHRASE", api_passphrase_},
       {"Content-Type", "application/json"}});

  INFO_FLOG("[Coinbase] Cancel: id={} resp={}", detail->nova_id.sequence, resp);
  MockTradeEngine::DoCancelOrder(detail);
}

END_NOVA_NAMESPACE(trade)
