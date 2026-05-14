#include "nvws/ws_websocket_server.h"
#include "base/base_base64.h"
#include "base/base_hash.h"
#include "base/base_os_socket.h"
#include "base/base_os_thread.h"
#include "nvws/ws_thread_pool.h"

#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <random>
#include <string>
#include <sys/select.h>
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

constexpr const char *WS_MAGIC_STRING = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// 客户端会话信息
struct ClientSession {
  session_t id;
  int sockfd;
  SSL *ssl;
  bool is_handshake_done;
  std::string remote_ip;
  uint16_t remote_port;
  std::vector<char> recv_buffer;

  ClientSession(session_t sid, int fd)
      : id(sid), sockfd(fd), ssl(nullptr), is_handshake_done(false),
        remote_port(0) {}
};

// WebSocket服务器实现类
class WSServerApiImpl : public WSServerApi {
public:
  WSServerApiImpl(WSServerSpi *spi, bool tls, const char *type)
      : spi_(spi), use_tls_(tls), server_type_(type ? type : ""),
        listen_sockfd_(-1), ssl_ctx_(nullptr), running_(false),
        next_session_id_(1) {}

  virtual ~WSServerApiImpl() { Stop(); }

  WS_ERROR_INFO Initialize(const char *ip, uint16_t port, int32_t core = -1,
                           int32_t loop_ms = 10,
                           bool global_thread = false) override {
    (void)global_thread; // 未使用的参数

    WS_ERROR_INFO err;

    // 初始化SSL环境
    if (use_tls_) {
      if (!SSL_env_init()) {
        err.Set(WS_ERR, "Failed to initialize SSL");
        return err;
      }
    }

    // 启动监听
    err = Listen(ip, port);
    if (err.code != WS_OK) {
      return err;
    }

    // 获取线程池并添加任务
    auto *pool = WSThreadPool::Instance();
    pool->SetCore(core);
    pool->SetLoopMS(loop_ms);

    // 添加接受连接任务
    pool->AddTask([this]() -> bool { return this->ProcessAccept(); });

    // 添加接收数据任务
    pool->AddTask([this]() -> bool { return this->ProcessReceive(); });

    pool->Run();

    running_ = true;
    err.Set(WS_OK, "Server initialized");
    return err;
  }

  void Stop() override {
    running_ = false;

    // 关闭所有客户端连接
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto &pair : sessions_) {
      CloseClientSession(pair.second);
    }
    sessions_.clear();

    // 关闭监听socket
    if (listen_sockfd_ >= 0) {
      close(listen_sockfd_);
      listen_sockfd_ = -1;
    }

    // 清理SSL上下文
    if (ssl_ctx_) {
      SSL_CTX_free(ssl_ctx_);
      ssl_ctx_ = nullptr;
    }
  }

  bool is_running() const override { return running_; }

  WS_ERROR_INFO Send(session_t session, int64_t ref,
                     const std::string &msg) override {
    return Send(session, ref, msg.c_str(), msg.size());
  }

  WS_ERROR_INFO Send(session_t session, int64_t ref, const void *msg,
                     size_t len) override {
    (void)ref; // 未使用
    WS_ERROR_INFO err;

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session);
    if (it == sessions_.end()) {
      err.Set(WS_ERR_SESSION_NOT_FOUND, "Session not found");
      return err;
    }

    ClientSession *client = it->second;
    if (!SendFrame(client, WS_OPCODE_BINARY, static_cast<const char *>(msg),
                   len)) {
      err.Set(WS_ERR, "Failed to send message");
      return err;
    }

    err.Set(WS_OK, "Message sent");
    if (spi_) {
      spi_->OnSend(session, ref);
    }
    return err;
  }

  WS_ERROR_INFO CloseSession(session_t session, int64_t ref,
                             const char *reason = "manual") override {
    (void)ref;
    WS_ERROR_INFO err;

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session);
    if (it == sessions_.end()) {
      err.Set(WS_ERR_SESSION_NOT_FOUND, "Session not found");
      return err;
    }

    ClientSession *client = it->second;
    SendFrame(client, WS_OPCODE_CLOSE, reason, strlen(reason));
    CloseClientSession(client);
    sessions_.erase(it);

    err.Set(WS_OK, "Session closed");
    return err;
  }

  WS_ERROR_INFO GetRemoteEndpoint(EndPoint &out, session_t session) override {
    WS_ERROR_INFO err;
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session);
    if (it == sessions_.end()) {
      err.Set(WS_ERR_SESSION_NOT_FOUND, "Session not found");
      return err;
    }

    out.address = it->second->remote_ip;
    out.prot = it->second->remote_port;
    err.Set(WS_OK, "Success");
    return err;
  }

  WS_ERROR_INFO GetLocalEndpoint(EndPoint &out, session_t session) override {
    (void)session;
    out.address = listen_ip_;
    out.prot = listen_port_;
    WS_ERROR_INFO err;
    err.Set(WS_OK, "Success");
    return err;
  }

  WS_ERROR_INFO Listen(const char *ip, uint16_t port) override {
    WS_ERROR_INFO err;

    // 创建监听socket
    listen_sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sockfd_ < 0) {
      err.Set(WS_ERR, "Failed to create socket");
      return err;
    }

    // 设置socket选项
    int opt = 1;
    setsockopt(listen_sockfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 设置非阻塞
    int flags = fcntl(listen_sockfd_, F_GETFL, 0);
    fcntl(listen_sockfd_, F_SETFL, flags | O_NONBLOCK);

    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = ip ? inet_addr(ip) : INADDR_ANY;

    if (bind(listen_sockfd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      close(listen_sockfd_);
      listen_sockfd_ = -1;
      err.Set(WS_ERR, "Failed to bind: %s", strerror(errno));
      return err;
    }

    // 开始监听
    if (listen(listen_sockfd_, 128) < 0) {
      close(listen_sockfd_);
      listen_sockfd_ = -1;
      err.Set(WS_ERR, "Failed to listen");
      return err;
    }

    listen_ip_ = ip ? ip : "0.0.0.0";
    listen_port_ = port;

    err.Set(WS_OK, "Listening on %s:%d", listen_ip_.c_str(), port);
    return err;
  }

  bool Poll() override {
    // 简单的轮询实现
    return running_;
  }

private:
  bool ProcessAccept() {
    if (!running_ || listen_sockfd_ < 0) {
      return true;
    }

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_fd =
        accept(listen_sockfd_, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return true; // 没有新连接
      }
      return true;
    }

    // 设置非阻塞
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    // 创建会话
    session_t session_id = next_session_id_++;
    ClientSession *client = new ClientSession(session_id, client_fd);
    client->remote_ip = inet_ntoa(client_addr.sin_addr);
    client->remote_port = ntohs(client_addr.sin_port);

    // 如果使用TLS，建立SSL连接
    if (use_tls_ && ssl_ctx_) {
      client->ssl = SSL_new(ssl_ctx_);
      if (client->ssl) {
        SSL_set_fd(client->ssl, client_fd);
        SSL_accept(client->ssl); // 异步接受SSL握手
      }
    }

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_[session_id] = client;

    return true;
  }

  bool ProcessReceive() {
    if (!running_) {
      return true;
    }

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();) {
      ClientSession *client = it->second;

      // 如果还没有完成握手，先处理握手
      if (!client->is_handshake_done) {
        if (!ProcessHandshake(client)) {
          // 握手失败，关闭连接
          CloseClientSession(client);
          it = sessions_.erase(it);
          continue;
        }

        if (client->is_handshake_done && spi_) {
          spi_->OnOpen(client->id);
        }
      } else {
        // 处理数据帧
        if (!ProcessFrame(client)) {
          // 处理失败，关闭连接
          WS_ERROR_INFO err;
          err.Set(WS_ERR, "Connection closed");
          if (spi_) {
            spi_->OnClose(client->id, err);
          }
          CloseClientSession(client);
          it = sessions_.erase(it);
          continue;
        }
      }

      ++it;
    }

    return true;
  }

  bool ProcessHandshake(ClientSession *client) {
    char buffer[4096];
    int n = RecvRaw(client, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return true; // 暂时没有数据
      }
      return false;
    }

    buffer[n] = '\0';
    std::string request(buffer);

    // 查找 Sec-WebSocket-Key
    size_t key_pos = request.find("Sec-WebSocket-Key:");
    if (key_pos == std::string::npos) {
      return true; // 继续等待完整的握手数据
    }

    size_t key_start = request.find_first_not_of(" \t", key_pos + 18);
    size_t key_end = request.find("\r\n", key_start);
    if (key_end == std::string::npos) {
      return false;
    }

    std::string client_key = request.substr(key_start, key_end - key_start);

    // 计算响应密钥
    std::string accept_key = client_key + WS_MAGIC_STRING;
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char *>(accept_key.c_str()),
         accept_key.size(), hash);

    USE_NOVA_NAMESPACE(base)
    char accept_base64[256];
    int len = Base64Encode(accept_base64, reinterpret_cast<const char *>(hash),
                           SHA_DIGEST_LENGTH);
    std::string accept_key_base64(accept_base64, len);

    // 构建握手响应
    std::string response = "HTTP/1.1 101 Switching Protocols\r\n";
    response += "Upgrade: websocket\r\n";
    response += "Connection: Upgrade\r\n";
    response += "Sec-WebSocket-Accept: " + accept_key_base64 + "\r\n";
    response += "\r\n";

    // 发送握手响应
    if (!SendRaw(client, response.c_str(), response.size())) {
      return false;
    }

    client->is_handshake_done = true;
    return true;
  }

  bool ProcessFrame(ClientSession *client) {
    // 读取帧头
    unsigned char header[2];
    int n = RecvRaw(client, reinterpret_cast<char *>(header), 2);
    if (n != 2) {
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return true; // 没有数据
      }
      return false;
    }

    bool fin = (header[0] & WS_FIN) != 0;
    uint8_t opcode = header[0] & 0x0F;
    bool masked = (header[1] & WS_MASK) != 0;
    uint64_t payload_len = header[1] & 0x7F;

    // 读取扩展payload长度
    if (payload_len == 126) {
      unsigned char len[2];
      if (RecvRaw(client, reinterpret_cast<char *>(len), 2) != 2) {
        return true;
      }
      payload_len = (len[0] << 8) | len[1];
    } else if (payload_len == 127) {
      unsigned char len[8];
      if (RecvRaw(client, reinterpret_cast<char *>(len), 8) != 8) {
        return true;
      }
      payload_len = 0;
      for (int i = 0; i < 8; ++i) {
        payload_len = (payload_len << 8) | len[i];
      }
    }

    // 读取mask密钥
    unsigned char mask_key[4] = {0};
    if (masked) {
      if (RecvRaw(client, reinterpret_cast<char *>(mask_key), 4) != 4) {
        return true;
      }
    }

    // 读取payload
    std::vector<char> payload(payload_len);
    if (payload_len > 0) {
      size_t total_read = 0;
      while (total_read < payload_len) {
        int n = RecvRaw(client, payload.data() + total_read,
                        payload_len - total_read);
        if (n <= 0) {
          if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
          }
          return false;
        }
        total_read += n;
      }

      // 解除mask
      if (masked) {
        for (size_t i = 0; i < payload_len; ++i) {
          payload[i] ^= mask_key[i % 4];
        }
      }
    }

    // 处理不同类型的帧
    switch (opcode) {
    case WS_OPCODE_TEXT:
    case WS_OPCODE_BINARY:
      if (spi_ && fin) {
        spi_->OnMessage(client->id, payload.data(), payload_len);
      }
      break;

    case WS_OPCODE_PING:
      // 响应Pong
      SendFrame(client, WS_OPCODE_PONG, payload.data(), payload_len);
      break;

    case WS_OPCODE_CLOSE:
      return false;

    default:
      break;
    }

    return true;
  }

  bool SendFrame(ClientSession *client, uint8_t opcode, const char *data,
                 size_t len) {
    std::vector<unsigned char> frame;

    // 第一个字节：FIN + opcode
    frame.push_back(WS_FIN | opcode);

    // 第二个字节：payload长度（服务器不需要mask）
    if (len < 126) {
      frame.push_back(static_cast<unsigned char>(len));
    } else if (len < 65536) {
      frame.push_back(126);
      frame.push_back((len >> 8) & 0xFF);
      frame.push_back(len & 0xFF);
    } else {
      frame.push_back(127);
      for (int i = 7; i >= 0; --i) {
        frame.push_back((len >> (i * 8)) & 0xFF);
      }
    }

    // 添加payload（不需要mask）
    for (size_t i = 0; i < len; ++i) {
      frame.push_back(data[i]);
    }

    return SendRaw(client, reinterpret_cast<const char *>(frame.data()),
                   frame.size());
  }

  bool SendRaw(ClientSession *client, const char *data, size_t len) {
    if (use_tls_ && client->ssl) {
      int n = SSL_write(client->ssl, data, len);
      return n == static_cast<int>(len);
    } else {
      ssize_t n = send(client->sockfd, data, len, 0);
      return n == static_cast<ssize_t>(len);
    }
  }

  int RecvRaw(ClientSession *client, char *buffer, size_t len) {
    if (use_tls_ && client->ssl) {
      return SSL_read(client->ssl, buffer, len);
    } else {
      return recv(client->sockfd, buffer, len, 0);
    }
  }

  void CloseClientSession(ClientSession *client) {
    if (client->ssl) {
      SSL_shutdown(client->ssl);
      SSL_free(client->ssl);
      client->ssl = nullptr;
    }

    if (client->sockfd >= 0) {
      close(client->sockfd);
      client->sockfd = -1;
    }

    delete client;
  }

private:
  WSServerSpi *spi_;
  bool use_tls_;
  std::string server_type_;

  int listen_sockfd_;
  SSL_CTX *ssl_ctx_;
  std::string listen_ip_;
  uint16_t listen_port_;

  bool running_;
  session_t next_session_id_;

  std::map<session_t, ClientSession *> sessions_;
  std::mutex sessions_mutex_;
};

// 工厂方法实现
WSServerApi *WSServerApi::Create(WSServerSpi *spi, bool tls, const char *type) {
  return new WSServerApiImpl(spi, tls, type);
}

END_NOVA_NAMESPACE(ws)
