#include "mock/kraken_trade_engine.h"
#include "coinrunner_log.h"

#include "nova_api_instrument.h"
#include "nlohmann_json/json.hpp"

#include <chrono>
#include <cstring>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/err.h>

BEGIN_NOVA_NAMESPACE(trade)

namespace {
std::string GetPair(const InstrumentId &inst_id) {
  std::string sym = inst_id.GetSymbol();
  auto dot = sym.rfind('.');
  return dot != std::string::npos ? sym.substr(0, dot) : sym;
}
} // namespace

// ========== KrakenTradeEngine ==========

KrakenTradeEngine::KrakenTradeEngine()
    : RestTradeEngine(NOVA_EXCHANGE_KRAKE, NOVA_COIN_INST_TYPE_SPOT) {
  nonce_ = std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
               .count();
}

std::string KrakenTradeEngine::HmacSha512B64(const std::string &key,
                                             const std::string &data) {
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  HMAC(EVP_sha512(), key.c_str(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char *>(data.data()), data.size(), result,
       &len);
  return Base64Encode(std::string(reinterpret_cast<char *>(result), len));
}

// ========== DoSendOrder ==========

void KrakenTradeEngine::DoSendOrder(const NovaOrderDetail *detail) {
  if (!detail || api_key_.empty()) { MockTradeEngine::DoSendOrder(detail); return; }
  if (!CheckRateLimit()) { MockTradeEngine::DoSendOrder(detail); return; }

  std::string pair = GetPair(detail->order.instrument_id);
  std::string side = detail->order.side == NOVA_SIDE_BUY ? "buy" : "sell";

  uint64_t nonce = ++nonce_;
  std::string post_data =
      "nonce=" + std::to_string(nonce) +
      "&pair=" + pair +
      "&type=" + side +
      "&ordertype=limit" +
      "&price=" + std::to_string(detail->order.price) +
      "&volume=" + std::to_string(detail->order.quantity);

  std::string path = "/0/private/AddOrder";
  std::string sign = HmacSha512B64(api_secret_, path + post_data);

  std::string resp =
      HttpsPost(api_host_.empty() ? "api.kraken.com" : api_host_.c_str(), 443, path, post_data,
                {{"API-Key", api_key_}, {"API-Sign", sign}});

  INFO_FLOG("[Kraken] Order: {} {} qty={} price={} resp={}", pair, side,
            detail->order.quantity, detail->order.price, resp);

  MockTradeEngine::DoSendOrder(detail);
}

// ========== DoCancelOrder ==========

void KrakenTradeEngine::DoCancelOrder(const NovaOrderDetail *detail) {
  if (!detail || api_key_.empty()) { MockTradeEngine::DoCancelOrder(detail); return; }

  uint64_t nonce = ++nonce_;
  std::string post_data =
      "nonce=" + std::to_string(nonce) +
      "&txid=" + std::to_string(detail->nova_id.sequence);

  std::string path = "/0/private/CancelOrder";
  std::string sign = HmacSha512B64(api_secret_, path + post_data);

  std::string resp =
      HttpsPost(api_host_.empty() ? "api.kraken.com" : api_host_.c_str(), 443, path, post_data,
                {{"API-Key", api_key_}, {"API-Sign", sign}});

  INFO_FLOG("[Kraken] Cancel: id={} resp={}", detail->nova_id.sequence, resp);

  MockTradeEngine::DoCancelOrder(detail);
}

END_NOVA_NAMESPACE(trade)
