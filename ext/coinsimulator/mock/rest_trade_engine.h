#pragma once

#include "mock/mock_trade_engine.h"

#include <string>
#include <unordered_map>

BEGIN_NOVA_NAMESPACE(trade)

/**
 * RestTradeEngine — REST API 交易引擎基类。
 *
 * 提供 HTTPS POST + HMAC 签名 + Base64 等通用工具。
 * 子类只需实现 DoSendOrder / DoCancelOrder 中的 API 调用即可。
 */
class RestTradeEngine : public MockTradeEngine {
public:
  RestTradeEngine(NOVA_EXCHANGE_TYPE exch, NOVA_COIN_INST_TYPE inst)
      : MockTradeEngine(exch, inst) {}

  bool LoadApiKeys(const nova::base::Config &cfg, const char *section);
  void SetApiHost(const std::string &host) { api_host_ = host; }
  void SetRateLimit(int max_rate, double decay_rate) {
    max_limit_rate_ = max_rate;
    decay_rate_per_sec_ = decay_rate;
    tokens_ = max_rate;
  }
  bool CheckRateLimit();

protected:
  std::string api_host_;

protected:
  // HTTPS POST
  std::string HttpsPost(const std::string &hostname, int port,
                        const std::string &path,
                        const std::string &post_data,
                        const std::unordered_map<std::string, std::string> &headers);

  // HMAC-SHA256 → hex
  static std::string HmacSha256Hex(const std::string &key, const std::string &data);

  // HMAC-SHA256 → base64
  static std::string HmacSha256B64(const std::string &key, const std::string &data);

  // Base64
  static std::string Base64Encode(const std::string &data);

  // URL-encode
  static std::string UrlEncode(const std::string &s);

  std::string api_key_;
  std::string api_secret_;
  std::string api_passphrase_;
  int max_limit_rate_ = 0;
  double decay_rate_per_sec_ = 0;
  double tokens_ = 0;
  int64_t last_check_ns_ = 0;
};

END_NOVA_NAMESPACE(trade)
