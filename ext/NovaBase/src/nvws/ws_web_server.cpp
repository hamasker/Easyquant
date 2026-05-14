#include "nvws/ws_web_server.h"
#include "base/base_os_socket.h"
#include "base/base_os_thread.h"
#include "nvws/ws_thread_pool.h"

#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <map>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sstream>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

BEGIN_NOVA_NAMESPACE(ws)

// HTTP 客户端会话
struct HttpSession {
  session_t id;
  int sockfd;
  SSL *ssl;
  std::string remote_ip;
  uint16_t remote_port;
  std::vector<char> recv_buffer;
  bool keep_alive;

  HttpSession(session_t sid, int fd)
      : id(sid), sockfd(fd), ssl(nullptr), remote_port(0), keep_alive(false) {}
};

// REST 服务器实现
class RestServerApiImpl : public RestServerApi {
public:
  RestServerApiImpl(RestServerSpi *spi, bool https)
      : spi_(spi), use_https_(https), listen_sockfd_(-1), ssl_ctx_(nullptr),
        running_(false), next_session_id_(1) {}

  virtual ~RestServerApiImpl() { Stop(); }

  WS_ERROR_INFO Initialize(const char *ip, uint16_t port, int32_t core,
                           int32_t loop_ms, bool global_thread) override {
    (void)global_thread;

    WS_ERROR_INFO err;

    // 初始化SSL
    if (use_https_) {
      if (!SSL_env_init()) {
        err.Set(WS_ERR, "Failed to initialize SSL");
        return err;
      }
    }

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

    // 获取线程池并添加任务
    auto *pool = WSThreadPool::Instance();
    pool->SetCore(core);
    pool->SetLoopMS(loop_ms);

    // 添加接受连接任务
    pool->AddTask([this]() -> bool { return this->ProcessAccept(); });

    // 添加接收请求任务
    pool->AddTask([this]() -> bool { return this->ProcessRequests(); });

    pool->Run();

    running_ = true;
    err.Set(WS_OK, "Server started on %s:%d", listen_ip_.c_str(), port);
    return err;
  }

  void Stop() override {
    running_ = false;

    // 关闭所有会话
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto &pair : sessions_) {
      CloseSession(pair.second);
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

  WS_ERROR_INFO SendResponse(session_t session, const std::string &status_code,
                             const Header &headers,
                             const std::string &body) override {
    return SendResponse(session, status_code, headers, body.c_str(),
                        body.size());
  }

  WS_ERROR_INFO SendResponse(session_t session, const std::string &status_code,
                             const Header &headers, const void *data,
                             size_t len) override {
    WS_ERROR_INFO err;

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session);
    if (it == sessions_.end()) {
      err.Set(WS_ERR_SESSION_NOT_FOUND, "Session not found");
      return err;
    }

    HttpSession *sess = it->second;

    // 构建HTTP响应
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << "\r\n";

    // 添加头部
    for (const auto &h : headers) {
      oss << h.first << ": " << h.second << "\r\n";
    }

    // 添加Content-Length
    if (len > 0) {
      oss << "Content-Length: " << len << "\r\n";
    }

    // 添加Connection头
    if (sess->keep_alive) {
      oss << "Connection: keep-alive\r\n";
    } else {
      oss << "Connection: close\r\n";
    }

    oss << "\r\n";

    std::string header = oss.str();

    // 发送头部
    if (!SendRaw(sess, header.c_str(), header.size())) {
      err.Set(WS_ERR, "Failed to send response header");
      return err;
    }

    // 发送body
    if (len > 0) {
      if (!SendRaw(sess, static_cast<const char *>(data), len)) {
        err.Set(WS_ERR, "Failed to send response body");
        return err;
      }
    }

    // 如果不是keep-alive，关闭连接
    if (!sess->keep_alive) {
      CloseSession(sess);
      sessions_.erase(it);
    }

    err.Set(WS_OK, "Response sent");
    return err;
  }

  WS_ERROR_INFO CloseSession(session_t session) override {
    WS_ERROR_INFO err;

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session);
    if (it == sessions_.end()) {
      err.Set(WS_ERR_SESSION_NOT_FOUND, "Session not found");
      return err;
    }

    CloseSession(it->second);
    sessions_.erase(it);

    err.Set(WS_OK, "Session closed");
    return err;
  }

  void RegisterRoute(REST_REQ_TYPE method, const std::string &path,
                     RouteHandler handler) override {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    std::string key = std::string(REST_REQ_METHOD[method]) + ":" + path;
    routes_[key] = handler;
  }

  void SetNotFoundHandler(RouteHandler handler) override {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    not_found_handler_ = handler;
  }

  bool Poll() override { return running_; }

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
        return true;
      }
      return true;
    }

    // 设置非阻塞
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    // 创建会话
    session_t session_id = next_session_id_++;
    HttpSession *sess = new HttpSession(session_id, client_fd);
    sess->remote_ip = inet_ntoa(client_addr.sin_addr);
    sess->remote_port = ntohs(client_addr.sin_port);

    // SSL处理
    if (use_https_ && ssl_ctx_) {
      sess->ssl = SSL_new(ssl_ctx_);
      if (sess->ssl) {
        SSL_set_fd(sess->ssl, client_fd);
        SSL_accept(sess->ssl);
      }
    }

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_[session_id] = sess;

    if (spi_) {
      spi_->OnConnectionOpen(session_id);
    }

    return true;
  }

  bool ProcessRequests() {
    if (!running_) {
      return true;
    }

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();) {
      HttpSession *sess = it->second;

      if (!ProcessRequest(sess)) {
        // 处理失败，关闭连接
        if (spi_) {
          spi_->OnConnectionClose(sess->id);
        }
        CloseSession(sess);
        it = sessions_.erase(it);
        continue;
      }

      ++it;
    }

    return true;
  }

  bool ProcessRequest(HttpSession *sess) {
    char buffer[4096];
    int n = RecvRaw(sess, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return true; // 暂时没有数据
      }
      return false; // 连接关闭
    }

    buffer[n] = '\0';
    sess->recv_buffer.insert(sess->recv_buffer.end(), buffer, buffer + n);

    // 查找完整的HTTP请求
    std::string request_str(sess->recv_buffer.begin(), sess->recv_buffer.end());
    size_t header_end = request_str.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      return true; // 继续接收
    }

    // 解析HTTP请求
    REST_REQ_TYPE method;
    std::string uri;
    RestServerSpi::RequestHeader headers;
    std::string body;

    if (!ParseRequest(request_str, method, uri, headers, body)) {
      return false;
    }

    // 检查Connection头
    for (const auto &h : headers) {
      if (h.first == "Connection" &&
          (h.second == "keep-alive" || h.second == "Keep-Alive")) {
        sess->keep_alive = true;
      }
    }

    // 调用回调
    if (spi_) {
      spi_->OnRequest(sess->id, method, uri, headers, body.c_str(),
                      body.size());
    }

    // 检查路由
    {
      std::lock_guard<std::mutex> lock(routes_mutex_);
      std::string key = std::string(REST_REQ_METHOD[method]) + ":" + uri;
      auto route_it = routes_.find(key);

      if (route_it != routes_.end()) {
        // 找到路由处理器
        Header hdr_map;
        for (const auto &h : headers) {
          hdr_map[h.first] = h.second;
        }
        route_it->second(sess->id, uri, hdr_map, body);
      } else if (not_found_handler_) {
        // 使用404处理器
        Header hdr_map;
        for (const auto &h : headers) {
          hdr_map[h.first] = h.second;
        }
        not_found_handler_(sess->id, uri, hdr_map, body);
      } else {
        // 默认404响应
        Header resp_headers;
        resp_headers["Content-Type"] = "text/plain";
        SendResponse(sess->id, "404 Not Found", resp_headers, "Not Found");
      }
    }

    // 清空缓冲区
    sess->recv_buffer.clear();

    return sess->keep_alive;
  }

  bool ParseRequest(const std::string &request, REST_REQ_TYPE &method,
                    std::string &uri, RestServerSpi::RequestHeader &headers,
                    std::string &body) {
    // 查找请求行结束
    size_t first_line_end = request.find("\r\n");
    if (first_line_end == std::string::npos) {
      return false;
    }

    // 解析请求行
    std::string request_line = request.substr(0, first_line_end);
    std::istringstream iss(request_line);
    std::string method_str, http_version;

    if (!(iss >> method_str >> uri >> http_version)) {
      return false;
    }

    // 解析方法
    if (method_str == "GET") {
      method = REST_REQ_GET;
    } else if (method_str == "POST") {
      method = REST_REQ_POST;
    } else if (method_str == "PUT") {
      method = REST_REQ_PUT;
    } else if (method_str == "DELETE") {
      method = REST_REQ_DELTE;
    } else {
      return false;
    }

    // 解析头部
    size_t header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      return false;
    }

    size_t pos = first_line_end + 2;
    while (pos < header_end) {
      size_t line_end = request.find("\r\n", pos);
      if (line_end == std::string::npos) {
        break;
      }

      std::string line = request.substr(pos, line_end - pos);
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // 去除前后空格
        size_t val_start = value.find_first_not_of(" \t");
        if (val_start != std::string::npos) {
          value = value.substr(val_start);
        }

        headers.insert({key, value});
      }

      pos = line_end + 2;
    }

    // 提取body
    if (header_end + 4 < request.size()) {
      body = request.substr(header_end + 4);
    }

    return true;
  }

  bool SendRaw(HttpSession *sess, const char *data, size_t len) {
    if (use_https_ && sess->ssl) {
      int n = SSL_write(sess->ssl, data, len);
      return n == static_cast<int>(len);
    } else {
      ssize_t n = send(sess->sockfd, data, len, 0);
      return n == static_cast<ssize_t>(len);
    }
  }

  int RecvRaw(HttpSession *sess, char *buffer, size_t len) {
    if (use_https_ && sess->ssl) {
      return SSL_read(sess->ssl, buffer, len);
    } else {
      return recv(sess->sockfd, buffer, len, 0);
    }
  }

  void CloseSession(HttpSession *sess) {
    if (sess->ssl) {
      SSL_shutdown(sess->ssl);
      SSL_free(sess->ssl);
      sess->ssl = nullptr;
    }

    if (sess->sockfd >= 0) {
      close(sess->sockfd);
      sess->sockfd = -1;
    }

    delete sess;
  }

private:
  RestServerSpi *spi_;
  bool use_https_;

  int listen_sockfd_;
  SSL_CTX *ssl_ctx_;
  std::string listen_ip_;
  uint16_t listen_port_;

  bool running_;
  session_t next_session_id_;

  std::map<session_t, HttpSession *> sessions_;
  std::mutex sessions_mutex_;

  std::map<std::string, RouteHandler> routes_;
  std::mutex routes_mutex_;
  RouteHandler not_found_handler_;
};

// 工厂方法
RestServerApi *RestServerApi::Create(RestServerSpi *spi, bool https) {
  return new RestServerApiImpl(spi, https);
}

END_NOVA_NAMESPACE(ws)
