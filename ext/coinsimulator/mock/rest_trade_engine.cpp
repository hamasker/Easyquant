#include "mock/rest_trade_engine.h"
#include "coinrunner_log.h"

#include "base/base_async_log.h"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <netdb.h>
#include <sstream>
#include <unistd.h>

BEGIN_NOVA_NAMESPACE(trade)

// ========== 工具函数 ==========

std::string RestTradeEngine::Base64Encode(const std::string &data) {
  char buf[256];
  int len = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(buf),
                            reinterpret_cast<const unsigned char *>(data.data()),
                            static_cast<int>(data.size()));
  return std::string(buf, len);
}

std::string RestTradeEngine::HmacSha256Hex(const std::string &key,
                                           const std::string &data) {
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  HMAC(EVP_sha256(), key.c_str(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char *>(data.data()), data.size(), result,
       &len);
  std::ostringstream oss;
  for (unsigned int i = 0; i < len; ++i)
    oss << std::hex << std::setw(2) << std::setfill('0') << (int)result[i];
  return oss.str();
}

std::string RestTradeEngine::HmacSha256B64(const std::string &key,
                                           const std::string &data) {
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  HMAC(EVP_sha256(), key.c_str(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char *>(data.data()), data.size(), result,
       &len);
  return Base64Encode(std::string(reinterpret_cast<char *>(result), len));
}

std::string RestTradeEngine::UrlEncode(const std::string &s) {
  std::ostringstream oss;
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      oss << c;
    else
      oss << '%' << std::hex << std::setw(2) << std::setfill('0') << (int)c;
  }
  return oss.str();
}

std::string RestTradeEngine::HttpsPost(
    const std::string &hostname, int port, const std::string &path,
    const std::string &post_data,
    const std::unordered_map<std::string, std::string> &headers) {
  struct hostent *server = gethostbyname(hostname.c_str());
  if (!server) return "";

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return "";

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
  addr.sin_port = htons(port);

  struct timeval tv = {5, 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(sock);
    return "";
  }

  SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
  SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
  SSL *ssl = SSL_new(ctx);
  SSL_set_fd(ssl, sock);
  SSL_set_tlsext_host_name(ssl, hostname.c_str());
  if (SSL_connect(ssl) <= 0) {
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(sock);
    return "";
  }

  std::string req = "POST " + path + " HTTP/1.1\r\nHost: " + hostname + "\r\n";
  for (auto &[k, v] : headers) req += k + ": " + v + "\r\n";
  req += "Content-Length: " + std::to_string(post_data.size()) + "\r\n";
  req += "Connection: close\r\n\r\n";
  req += post_data;

  SSL_write(ssl, req.c_str(), static_cast<int>(req.size()));

  std::string resp;
  char buf[4096];
  int n;
  while ((n = SSL_read(ssl, buf, sizeof(buf))) > 0) resp.append(buf, n);

  SSL_shutdown(ssl);
  SSL_free(ssl);
  SSL_CTX_free(ctx);
  close(sock);

  auto body_start = resp.find("\r\n\r\n");
  return body_start != std::string::npos ? resp.substr(body_start + 4) : "";
}

// ========== LoadApiKeys ==========

bool RestTradeEngine::LoadApiKeys(const nova::base::Config &cfg,
                                  const char *section) {
  std::string prefix = std::string("Quote.") + section + ".";
  cfg.GetItemValue(prefix + "api_key", api_key_);
  cfg.GetItemValue(prefix + "api_secret", api_secret_);
  cfg.GetItemValue(prefix + "api_passphrase", api_passphrase_);
  if (api_key_.empty() || api_secret_.empty()) return false;
  INFO_FLOG("[RestTrade] {} API keys loaded", section);
  return true;
}

bool RestTradeEngine::CheckRateLimit() {
  if (max_limit_rate_ <= 0) return true; // no limit
  auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  if (last_check_ns_ > 0) {
    double elapsed_s = (now - last_check_ns_) / 1e9;
    tokens_ += elapsed_s * decay_rate_per_sec_;
    if (tokens_ > max_limit_rate_) tokens_ = max_limit_rate_;
  }
  last_check_ns_ = now;
  if (tokens_ >= 1.0) {
    tokens_ -= 1.0;
    return true;
  }
  return false;
}

END_NOVA_NAMESPACE(trade)
