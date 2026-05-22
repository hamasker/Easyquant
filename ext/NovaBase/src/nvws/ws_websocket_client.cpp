#include "nvws/ws_websocket_client.h"
#include <pthread.h>
#include "base/base_base64.h"
#include "base/base_hash.h"
#include "base/base_os_socket.h"
#include "base/base_os_thread.h"
#include "nvws/ws_thread_pool.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#include <random>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

BEGIN_NOVA_NAMESPACE(ws)

// WebSocket协议相关常量
constexpr uint8_t WS_OPCODE_CONTINUATION = 0x0;
constexpr uint8_t WS_OPCODE_TEXT = 0x1;
constexpr uint8_t WS_OPCODE_BINARY = 0x2;
constexpr uint8_t WS_OPCODE_CLOSE = 0x8;
constexpr uint8_t WS_OPCODE_PING = 0x9;
constexpr uint8_t WS_OPCODE_PONG = 0xA;

constexpr uint8_t WS_FIN = 0x80;
constexpr uint8_t WS_MASK = 0x80;

// WebSocket客户端实现类
class WSClientApiImpl : public WSClientApi {
public:
  WSClientApiImpl(WSClientSpi *spi, bool tls, int group)
      : spi_(spi), use_tls_(tls), group_(group), sockfd_(-1), ssl_(nullptr),
        ssl_ctx_(nullptr), state_(WS_STATE_CLOSED), io_thread_(nullptr),
        io_stop_(false), proxy_port_(0) {
    // 读取代理设置
    const char *proxy_env = getenv("https_proxy");
    if (!proxy_env) proxy_env = getenv("HTTPS_PROXY");
    if (proxy_env) {
      auto proxy_url = std::string(proxy_env);
      auto scheme_end = proxy_url.find("://");
      if (scheme_end != std::string::npos)
        proxy_url = proxy_url.substr(scheme_end + 3);
      auto colon = proxy_url.find(':');
      if (colon != std::string::npos) {
        proxy_host_ = proxy_url.substr(0, colon);
        proxy_port_ = std::stoi(proxy_url.substr(colon + 1));
      }
    }
  }

  virtual ~WSClientApiImpl() {
    Stop();
    io_thread_ = nullptr;
  }

  WS_ERROR_INFO Initialize(const char *url, int32_t core = -1,
                           int32_t loop_ms = 10,
                           bool global_thread = false) override {
    (void)global_thread; // 未使用的参数

    WS_ERROR_INFO err;

    if (!url || strlen(url) == 0) {
      err.Set(WS_ERR, "Invalid URL");
      return err;
    }

    url_ = url;

    // 解析URL
    if (!ParseURL(url_, host_, port_, path_)) {
      err.Set(WS_ERR, "Failed to parse URL");
      return err;
    }

    // 初始化SSL环境
    if (use_tls_) {
      if (!SSL_env_init()) {
        err.Set(WS_ERR, "Failed to initialize SSL");
        return err;
      }
    }

    // 创建独立后台线程 (不使用共享 WSThreadPool, 每个连接独立线程)
    int32_t lms = loop_ms;
    int32_t bind_core = core;
    io_thread_ = new std::thread([this, lms, bind_core]() {
      // 绑核
      if (bind_core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(static_cast<size_t>(bind_core), &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
      }
      // 连接
      if (!ProcessConnect()) return;
      // 接收循环: SSL_read 为阻塞模式, 有数据时立即处理, 无数据时阻塞等待
      while (state_ == WS_STATE_OPEN && !io_stop_) {
        ProcessReceive();
      }
    });

    err.Set(WS_OK, "Success");
    return err;
  }

  void Stop() override {
    state_ = WS_STATE_CLOSING;
    io_stop_ = true;

    CloseConnection();

    if (io_thread_ && io_thread_->joinable()) {
      io_thread_->join();
      delete io_thread_;
      io_thread_ = nullptr;
    }

    state_ = WS_STATE_CLOSED;
  }

  void Restart(const char *uri) override {
    if (uri && strlen(uri) > 0) {
      url_ = uri;
      ParseURL(url_, host_, port_, path_);
    }

    Stop();
    io_stop_ = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 重新初始化
    Initialize(url_.c_str(), -1, 10, false);
  }

  WS_ERROR_INFO Send(const char *msg, size_t len) override {
    WS_ERROR_INFO err;
    if (!SendFrame(WS_OPCODE_TEXT, msg, len)) {
      err.Set(WS_ERR, "Failed to send message");
      last_error_ = err;
    } else {
      err.Set(WS_OK, "Message sent");
    }
    return err;
  }

  WS_ERROR_INFO Send(const std::string &str) override {
    return Send(str.c_str(), str.size());
  }

  WS_STATE_TYPE state() const override { return state_; }

  const WS_ERROR_INFO *local_close_info() const override {
    return &local_close_info_;
  }

  const WS_ERROR_INFO *remote_close_info() const override {
    return &remote_close_info_;
  }

  const WS_ERROR_INFO *last_error() const override { return &last_error_; }

  WS_ERROR_INFO Connect(const char *uri) override {
    WS_ERROR_INFO err;
    if (uri && strlen(uri) > 0) {
      url_ = uri;
      if (!ParseURL(url_, host_, port_, path_)) {
        err.Set(WS_ERR, "Failed to parse URL");
        last_error_ = err;
        return err;
      }
    }

    if (ProcessConnect()) {
      err.Set(WS_OK, "Connected");
    } else {
      err.Set(WS_ERR, "Connection failed");
      last_error_ = err;
    }
    return err;
  }

  bool Poll() override { return ProcessReceive(); }

private:
  // 发送Ping
  bool SendPing(const char *data = nullptr, size_t len = 0) {
    return SendFrame(WS_OPCODE_PING, data, len);
  }

private:
  bool ParseURL(const std::string &url, std::string &host, int &port,
                std::string &path) {
    // 解析 ws://host:port/path 或 wss://host:port/path
    size_t pos = 0;

    if (url.compare(0, 6, "wss://") == 0) {
      pos = 6;
      port = 443;
    } else if (url.compare(0, 5, "ws://") == 0) {
      pos = 5;
      port = 80;
    } else {
      return false;
    }

    size_t slash_pos = url.find('/', pos);
    std::string host_port;

    if (slash_pos != std::string::npos) {
      host_port = url.substr(pos, slash_pos - pos);
      path = url.substr(slash_pos);
    } else {
      host_port = url.substr(pos);
      path = "/";
    }

    // 解析host和port
    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
      host = host_port.substr(0, colon_pos);
      port = std::stoi(host_port.substr(colon_pos + 1));
    } else {
      host = host_port;
    }

    return true;
  }

  bool ProcessConnect() {
    if (state_ == WS_STATE_OPEN || state_ == WS_STATE_CONNECTING) {
      return true;
    }

    state_ = WS_STATE_CONNECTING;

    // 创建socket
    sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) {
      WS_ERROR_INFO err;
      err.Set(WS_ERR, "Failed to create socket");
      if (spi_)
        spi_->OnFail(err);
      state_ = WS_STATE_CLOSED;
      return true;
    }

    // 设置非阻塞模式
    int flags = fcntl(sockfd_, F_GETFL, 0);
    fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);

    // 代理模式: 连接代理而不是目标主机
    std::string real_host = host_;
    int real_port = port_;
    if (!proxy_host_.empty()) {
      real_host = proxy_host_;
      real_port = proxy_port_;
    }

    // 解析主机名
    struct hostent *server = gethostbyname(real_host.c_str());
    if (!server) {
      WS_ERROR_INFO err;
      err.Set(WS_ERR, "Failed to resolve hostname: %s", real_host.c_str());
      if (spi_)
        spi_->OnFail(err);
      close(sockfd_);
      sockfd_ = -1;
      state_ = WS_STATE_CLOSED;
      return true;
    }

    // 连接服务器
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(real_port);

    int ret =
        connect(sockfd_, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret < 0 && errno != EINPROGRESS) {
      WS_ERROR_INFO err;
      err.Set(WS_ERR, "Failed to connect: %s", strerror(errno));
      if (spi_)
        spi_->OnFail(err);
      close(sockfd_);
      sockfd_ = -1;
      state_ = WS_STATE_CLOSED;
      return true;
    }

    // 等待连接完成
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(sockfd_, &write_fds);
    struct timeval tv = {5, 0}; // 5秒超时

    ret = select(sockfd_ + 1, nullptr, &write_fds, nullptr, &tv);
    if (ret <= 0) {
      WS_ERROR_INFO err;
      err.Set(WS_ERR_TIMEOUT, "Connection timeout");
      if (spi_)
        spi_->OnFail(err);
      close(sockfd_);
      sockfd_ = -1;
      state_ = WS_STATE_CLOSED;
      return true;
    }

    // 代理 HTTP CONNECT 隧道
    if (!proxy_host_.empty()) {
      // 切回阻塞模式进行 CONNECT 握手
      int cur_flags = fcntl(sockfd_, F_GETFL, 0);
      if (cur_flags != -1) {
        fcntl(sockfd_, F_SETFL, cur_flags & ~O_NONBLOCK);
      }

      auto target = host_ + ":" + std::to_string(port_);
      std::string req = "CONNECT " + target + " HTTP/1.1\r\nHost: " + target +
                        "\r\n\r\n";
      if (send(sockfd_, req.c_str(), req.size(), 0) <= 0) {
        WS_ERROR_INFO err;
        err.Set(WS_ERR, "Failed to send CONNECT to proxy");
        if (spi_) spi_->OnFail(err);
        close(sockfd_);
        sockfd_ = -1;
        state_ = WS_STATE_CLOSED;
        return true;
      }

      char buf[4096];
      ssize_t n = recv(sockfd_, buf, sizeof(buf) - 1, 0);
      if (n <= 0) {
        WS_ERROR_INFO err;
        err.Set(WS_ERR, "No response from proxy");
        if (spi_) spi_->OnFail(err);
        close(sockfd_);
        sockfd_ = -1;
        state_ = WS_STATE_CLOSED;
        return true;
      }
      buf[n] = '\0';
      std::string resp(buf, n);
      if (resp.find("200") == std::string::npos) {
        WS_ERROR_INFO err;
        // 找第一行作为错误
        auto nl = resp.find('\r');
        err.Set(WS_ERR, "Proxy CONNECT failed: %s",
                resp.substr(0, nl).c_str());
        if (spi_) spi_->OnFail(err);
        close(sockfd_);
        sockfd_ = -1;
        state_ = WS_STATE_CLOSED;
        return true;
      }
    } else {
      // 无代理: 直接切回阻塞模式
      int cur_flags = fcntl(sockfd_, F_GETFL, 0);
      if (cur_flags != -1) {
        fcntl(sockfd_, F_SETFL, cur_flags & ~O_NONBLOCK);
      }
    }

    // 如果使用TLS，建立SSL连接
    if (use_tls_) {
      if (!EstablishSSL()) {
        WS_ERROR_INFO err;
        err.Set(WS_ERR, "Failed to establish SSL connection");
        if (spi_)
          spi_->OnFail(err);
        close(sockfd_);
        sockfd_ = -1;
        state_ = WS_STATE_CLOSED;
        return true;
      }
    }

    // 执行WebSocket握手
    if (!PerformHandshake()) {
      WS_ERROR_INFO err;
      err.Set(WS_ERR, "WebSocket handshake failed");
      if (spi_)
        spi_->OnFail(err);
      CloseConnection();
      state_ = WS_STATE_CLOSED;
      return true;
    }

    state_ = WS_STATE_OPEN;
    if (spi_)
      spi_->OnOpen(group_);

    return true;
  }

  bool EstablishSSL() {
    const SSL_METHOD *method = TLS_client_method();
    ssl_ctx_ = SSL_CTX_new(method);
    if (!ssl_ctx_) {
      return false;
    }

    SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_2_VERSION);
    if (SSL_CTX_set_default_verify_paths(ssl_ctx_) != 1) {
      SSL_CTX_free(ssl_ctx_);
      ssl_ctx_ = nullptr;
      return false;
    }
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, nullptr);

    ssl_ = SSL_new(ssl_ctx_);
    if (!ssl_) {
      SSL_CTX_free(ssl_ctx_);
      ssl_ctx_ = nullptr;
      return false;
    }

    SSL_set_fd(ssl_, sockfd_);

    if (SSL_set_tlsext_host_name(ssl_, host_.c_str()) != 1) {
      SSL_free(ssl_);
      SSL_CTX_free(ssl_ctx_);
      ssl_ = nullptr;
      ssl_ctx_ = nullptr;
      return false;
    }

    if (SSL_set1_host(ssl_, host_.c_str()) != 1) {
      SSL_free(ssl_);
      SSL_CTX_free(ssl_ctx_);
      ssl_ = nullptr;
      ssl_ctx_ = nullptr;
      return false;
    }

    int ret = SSL_connect(ssl_);
    if (ret != 1) {
      SSL_free(ssl_);
      SSL_CTX_free(ssl_ctx_);
      ssl_ = nullptr;
      ssl_ctx_ = nullptr;
      return false;
    }

    if (SSL_get_verify_result(ssl_) != X509_V_OK) {
      SSL_free(ssl_);
      SSL_CTX_free(ssl_ctx_);
      ssl_ = nullptr;
      ssl_ctx_ = nullptr;
      return false;
    }

    return true;
  }

  bool PerformHandshake() {
    // 生成WebSocket密钥
    unsigned char key[16];
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < 16; ++i) {
      key[i] = static_cast<unsigned char>(dis(gen));
    }

    char key_base64_buf[256];
    int len = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(key_base64_buf),
                              reinterpret_cast<const unsigned char *>(key), 16);
    std::string key_base64(key_base64_buf, len);

    // 构建握手请求
    std::string request = "GET " + path_ + " HTTP/1.1\r\n";
    request += "Host: " + host_ + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + key_base64 + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "\r\n";

    // 发送握手请求
    if (!SendRaw(request.c_str(), request.size())) {
      return false;
    }

    // 接收握手响应
    char buffer[4096];
    int n = RecvRaw(buffer, sizeof(buffer) - 1);
    if (n <= 0) {
      return false;
    }
    buffer[n] = '\0';

    // 验证握手响应 (大小写不敏感)
    std::string response(buffer);
    std::string lower_resp = response;
    std::transform(lower_resp.begin(), lower_resp.end(), lower_resp.begin(),
                   ::tolower);
    if (lower_resp.find("http/1.1 101") == std::string::npos ||
        lower_resp.find("upgrade: websocket") == std::string::npos) {
      return false;
    }

    return true;
  }

  bool ProcessReceive() {
    if (state_ != WS_STATE_OPEN) {
      return true;
    }

    // 读取帧头
    unsigned char header[2];
    int n = RecvRaw(reinterpret_cast<char *>(header), 2);
    if (n != 2) {
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return true; // 没有数据可读
      }
      // 连接关闭
      if (spi_)
        spi_->OnClose(false);
      state_ = WS_STATE_CLOSED;
      return false;
    }

    bool fin = (header[0] & WS_FIN) != 0;
    uint8_t opcode = header[0] & 0x0F;
    bool masked = (header[1] & WS_MASK) != 0;
    uint64_t payload_len = header[1] & 0x7F;

    // 读取扩展payload长度
    if (payload_len == 126) {
      unsigned char len[2];
      if (RecvRaw(reinterpret_cast<char *>(len), 2) != 2) {
        return true;
      }
      payload_len = (len[0] << 8) | len[1];
    } else if (payload_len == 127) {
      unsigned char len[8];
      if (RecvRaw(reinterpret_cast<char *>(len), 8) != 8) {
        return true;
      }
      payload_len = 0;
      for (int i = 0; i < 8; ++i) {
        payload_len = (payload_len << 8) | len[i];
      }
    }

    // 服务器发来的消息通常不应该被mask
    if (masked) {
      unsigned char mask[4];
      if (RecvRaw(reinterpret_cast<char *>(mask), 4) != 4) {
        return true;
      }
    }

    // 读取payload
    std::vector<char> payload(payload_len);
    if (payload_len > 0) {
      size_t total_read = 0;
      while (total_read < payload_len) {
        int n = RecvRaw(payload.data() + total_read, payload_len - total_read);
        if (n <= 0) {
          if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
          }
          return true;
        }
        total_read += n;
      }
    }

    // 处理不同类型的帧
    switch (opcode) {
    case WS_OPCODE_TEXT:
    case WS_OPCODE_BINARY:
      if (spi_ && fin) {
        spi_->OnMessage(payload.data(), payload_len, group_);
      }
      break;

    case WS_OPCODE_PING:
      // 响应Pong
      SendFrame(WS_OPCODE_PONG, payload.data(), payload_len);
      break;

    case WS_OPCODE_PONG:
      if (spi_) {
        spi_->OnPong(payload.data(), payload_len);
      }
      break;

    case WS_OPCODE_CLOSE:
      if (spi_)
        spi_->OnClose(false);
      state_ = WS_STATE_CLOSED;
      return false;

    default:
      break;
    }

    return true;
  }

  bool SendFrame(uint8_t opcode, const char *data, size_t len) {
    if (state_ != WS_STATE_OPEN) {
      return false;
    }

    std::vector<unsigned char> frame;

    // 第一个字节：FIN + opcode
    frame.push_back(WS_FIN | opcode);

    // 第二个字节：MASK + payload长度
    unsigned char mask_and_len = WS_MASK;

    if (len < 126) {
      mask_and_len |= static_cast<unsigned char>(len);
      frame.push_back(mask_and_len);
    } else if (len < 65536) {
      mask_and_len |= 126;
      frame.push_back(mask_and_len);
      frame.push_back((len >> 8) & 0xFF);
      frame.push_back(len & 0xFF);
    } else {
      mask_and_len |= 127;
      frame.push_back(mask_and_len);
      for (int i = 7; i >= 0; --i) {
        frame.push_back((len >> (i * 8)) & 0xFF);
      }
    }

    // 生成mask密钥
    unsigned char mask_key[4];
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < 4; ++i) {
      mask_key[i] = static_cast<unsigned char>(dis(gen));
      frame.push_back(mask_key[i]);
    }

    // 添加masked payload
    for (size_t i = 0; i < len; ++i) {
      frame.push_back(data[i] ^ mask_key[i % 4]);
    }

    return SendRaw(reinterpret_cast<const char *>(frame.data()), frame.size());
  }

  bool SendRaw(const char *data, size_t len) {
    if (use_tls_ && ssl_) {
      int n = SSL_write(ssl_, data, len);
      return n == static_cast<int>(len);
    } else {
      ssize_t n = send(sockfd_, data, len, 0);
      return n == static_cast<ssize_t>(len);
    }
  }

  int RecvRaw(char *buffer, size_t len) {
    if (use_tls_ && ssl_) {
      int ret = SSL_read(ssl_, buffer, len);
      if (ret <= 0) {
        int ssl_err = SSL_get_error(ssl_, ret);
        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
          errno = EAGAIN;
          return -1;
        }
      }
      return ret;
    } else {
      return recv(sockfd_, buffer, len, 0);
    }
  }

  void CloseConnection() {
    if (ssl_) {
      SSL_shutdown(ssl_);
      SSL_free(ssl_);
      ssl_ = nullptr;
    }

    if (ssl_ctx_) {
      SSL_CTX_free(ssl_ctx_);
      ssl_ctx_ = nullptr;
    }

    if (sockfd_ >= 0) {
      close(sockfd_);
      sockfd_ = -1;
    }
  }

private:
  WSClientSpi *spi_;
  bool use_tls_;
  int group_;

  std::string url_;
  std::string host_;
  int port_;
  std::string path_;

  int sockfd_;
  SSL *ssl_;
  SSL_CTX *ssl_ctx_;

  WS_STATE_TYPE state_;

  std::thread *io_thread_ = nullptr;
  std::atomic<bool> io_stop_{false};

  std::string proxy_host_;
  int proxy_port_;

  WS_ERROR_INFO last_error_;
  WS_ERROR_INFO local_close_info_;
  WS_ERROR_INFO remote_close_info_;
};

// 工厂方法实现
WSClientApi *WSClientApi::Create(WSClientSpi *spi, bool tls, int group) {
  return new WSClientApiImpl(spi, tls, group);
}

// FastWSClientApi 实现
struct FastWSClientApi::Ctx {
  WSClientApiImpl *client;
  WSClientSpi *spi;
  bool tls;
  int group;

  Ctx(WSClientSpi *s, bool t, int g)
      : client(nullptr), spi(s), tls(t), group(g) {}
};

FastWSClientApi::~FastWSClientApi() {
  if (impl_) {
    if (impl_->client) {
      delete impl_->client;
    }
    delete impl_;
    impl_ = nullptr;
  }
}

FastWSClientApi *FastWSClientApi::Create(WSClientSpi *spi, bool tls, int group,
                                         bool origin_version) {
  (void)origin_version; // 未使用的参数

  FastWSClientApi *api = new FastWSClientApi();
  api->impl_ = new Ctx(spi, tls, group);
  api->impl_->client = new WSClientApiImpl(spi, tls, group);
  return api;
}

WS_ERROR_INFO FastWSClientApi::Initialize(const char *url, int32_t message_core,
                                          int32_t loop_ms, bool global_thread) {
  if (!impl_ || !impl_->client) {
    WS_ERROR_INFO err;
    err.Set(WS_ERR, "Not initialized");
    return err;
  }

  return impl_->client->Initialize(url, message_core, loop_ms, global_thread);
}

void FastWSClientApi::Stop() {
  if (impl_ && impl_->client) {
    impl_->client->Stop();
  }
}

void FastWSClientApi::Restart(const char *uri) {
  if (impl_ && impl_->client) {
    impl_->client->Restart(uri);
  }
}

WS_ERROR_INFO FastWSClientApi::Send(const char *msg, size_t len) {
  if (!impl_ || !impl_->client) {
    WS_ERROR_INFO err;
    err.Set(WS_ERR, "Not initialized");
    return err;
  }

  return impl_->client->Send(msg, len);
}

WS_ERROR_INFO FastWSClientApi::Send(const std::string &str) {
  return Send(str.c_str(), str.size());
}

WS_STATE_TYPE FastWSClientApi::state() const {
  if (impl_ && impl_->client) {
    return impl_->client->state();
  }
  return WS_STATE_CLOSED;
}

const WS_ERROR_INFO *FastWSClientApi::local_close_info() const {
  if (impl_ && impl_->client) {
    return impl_->client->local_close_info();
  }
  return nullptr;
}

const WS_ERROR_INFO *FastWSClientApi::remote_close_info() const {
  if (impl_ && impl_->client) {
    return impl_->client->remote_close_info();
  }
  return nullptr;
}

const WS_ERROR_INFO *FastWSClientApi::last_error() const {
  if (impl_ && impl_->client) {
    return impl_->client->last_error();
  }
  return nullptr;
}

WS_ERROR_INFO FastWSClientApi::Connect(const char *uri) {
  if (!impl_ || !impl_->client) {
    WS_ERROR_INFO err;
    err.Set(WS_ERR, "Not initialized");
    return err;
  }

  return impl_->client->Connect(uri);
}

bool FastWSClientApi::Poll() {
  if (impl_ && impl_->client) {
    return impl_->client->Poll();
  }
  return false;
}

#if __cplusplus >= 202002L
FastWSClientApi::FastApiInterface::FastApiInterface(FastWSClientApi *api,
                                                    int core)
    : api_(api), async_(core >= 0) {
  (void)core;
}
#endif

END_NOVA_NAMESPACE(ws)
