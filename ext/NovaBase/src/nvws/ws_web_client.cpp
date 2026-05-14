#include "nvws/ws_web_client.h"
#include "base/base_os_socket.h"
#include "base/base_os_thread.h"
#include "nvws/ws_thread_pool.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <queue>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

BEGIN_NOVA_NAMESPACE(ws)

// HTTP请求结构
struct HTTPRequest {
  uint64_t ref;
  REST_REQ_TYPE method;
  std::string uri;
  std::map<std::string, std::string> headers;
  std::string body;
};

// REST客户端实现类
class RestClientApiImpl : public RestClientApi {
public:
  RestClientApiImpl(RestClientSpi *spi, bool https, bool http2)
      : spi_(spi), use_https_(https), use_http2_(http2), port_(0), sockfd_(-1),
        ssl_(nullptr), ssl_ctx_(nullptr), running_(false) {
    (void)use_http2_; // Reserved for future HTTP/2 support
  }

  virtual ~RestClientApiImpl() { Stop(); }

  WS_ERROR_INFO Initialize(const char *base_url, int32_t core = -1,
                           int32_t loop_ms = 10,
                           bool global_thread = false) override {
    (void)global_thread; // 未使用的参数

    WS_ERROR_INFO err;

    if (!base_url || strlen(base_url) == 0) {
      err.Set(WS_ERR, "Invalid base URL");
      return err;
    }

    // 解析URL
    if (!ParseURL(base_url)) {
      err.Set(WS_ERR, "Failed to parse URL");
      return err;
    }

    // 初始化SSL环境
    if (use_https_) {
      if (!SSL_env_init()) {
        err.Set(WS_ERR, "Failed to initialize SSL");
        return err;
      }

      const SSL_METHOD *method = TLS_client_method();
      ssl_ctx_ = SSL_CTX_new(method);
      if (!ssl_ctx_) {
        err.Set(WS_ERR, "Failed to create SSL context");
        return err;
      }
    }

    // 建立连接
    if (!Connect()) {
      err.Set(WS_ERR, "Failed to connect to server");
      return err;
    }

    // 获取线程池并添加任务
    auto *pool = WSThreadPool::Instance();
    pool->SetCore(core);
    pool->SetLoopMS(loop_ms);

    // 添加请求处理任务
    pool->AddTask([this]() -> bool { return this->ProcessRequests(); });

    pool->Run();

    running_ = true;
    err.Set(WS_OK, "Client initialized");
    return err;
  }

  void Stop() override {
    running_ = false;

    // 清空请求队列
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      while (!request_queue_.empty()) {
        request_queue_.pop();
      }
    }

    // 关闭连接
    Disconnect();

    // 清理SSL上下文
    if (ssl_ctx_) {
      SSL_CTX_free(ssl_ctx_);
      ssl_ctx_ = nullptr;
    }
  }

  WS_ERROR_INFO Request(uint64_t ref, REST_REQ_TYPE req_type,
                        const Message &uri, const Header &hdr,
                        const Message &body) override {
    WS_ERROR_INFO err;

    if (!running_) {
      err.Set(WS_ERR, "Client not running");
      return err;
    }

    HTTPRequest req;
    req.ref = ref;
    req.method = req_type;
    req.uri = uri;
    req.headers = hdr;
    req.body = body;

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      request_queue_.push(req);
    }

    err.Set(WS_OK, "Request queued");
    return err;
  }

private:
  bool ParseURL(const char *url) {
    std::string url_str(url);
    size_t pos = 0;

    // 检查协议
    if (url_str.compare(0, 8, "https://") == 0) {
      pos = 8;
      port_ = 443;
      use_https_ = true;
    } else if (url_str.compare(0, 7, "http://") == 0) {
      pos = 7;
      port_ = 80;
      use_https_ = false;
    } else {
      return false;
    }

    // 提取主机和路径
    size_t slash_pos = url_str.find('/', pos);
    std::string host_port;

    if (slash_pos != std::string::npos) {
      host_port = url_str.substr(pos, slash_pos - pos);
      base_path_ = url_str.substr(slash_pos);
    } else {
      host_port = url_str.substr(pos);
      base_path_ = "/";
    }

    // 解析主机和端口
    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
      host_ = host_port.substr(0, colon_pos);
      port_ = std::stoi(host_port.substr(colon_pos + 1));
    } else {
      host_ = host_port;
    }

    return true;
  }

  bool Connect() {
    // 创建socket
    sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) {
      return false;
    }

    // 设置非阻塞
    int flags = fcntl(sockfd_, F_GETFL, 0);
    fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);

    // 解析主机名
    struct hostent *server = gethostbyname(host_.c_str());
    if (!server) {
      close(sockfd_);
      sockfd_ = -1;
      return false;
    }

    // 连接服务器
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port_);

    int ret =
        connect(sockfd_, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret < 0 && errno != EINPROGRESS) {
      close(sockfd_);
      sockfd_ = -1;
      return false;
    }

    // 等待连接完成
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(sockfd_, &write_fds);
    struct timeval tv = {5, 0}; // 5秒超时

    ret = select(sockfd_ + 1, nullptr, &write_fds, nullptr, &tv);
    if (ret <= 0) {
      close(sockfd_);
      sockfd_ = -1;
      return false;
    }

    // 如果使用HTTPS，建立SSL连接
    if (use_https_ && ssl_ctx_) {
      ssl_ = SSL_new(ssl_ctx_);
      if (!ssl_) {
        close(sockfd_);
        sockfd_ = -1;
        return false;
      }

      SSL_set_fd(ssl_, sockfd_);

      ret = SSL_connect(ssl_);
      if (ret != 1) {
        SSL_free(ssl_);
        ssl_ = nullptr;
        close(sockfd_);
        sockfd_ = -1;
        return false;
      }
    }

    return true;
  }

  void Disconnect() {
    if (ssl_) {
      SSL_shutdown(ssl_);
      SSL_free(ssl_);
      ssl_ = nullptr;
    }

    if (sockfd_ >= 0) {
      close(sockfd_);
      sockfd_ = -1;
    }
  }

  bool ProcessRequests() {
    if (!running_) {
      return false;
    }

    HTTPRequest req;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (request_queue_.empty()) {
        return true; // 没有请求
      }
      req = request_queue_.front();
      request_queue_.pop();
    }

    // 发送请求
    if (!SendRequest(req)) {
      WS_ERROR_INFO err;
      err.Set(WS_ERR, "Failed to send request");
      if (spi_) {
        spi_->OnError(req.ref, &err);
      }
      return true;
    }

    if (spi_) {
      spi_->OnSendRequest(req.ref);
    }

    // 接收响应
    std::string response;
    if (!ReceiveResponse(response)) {
      WS_ERROR_INFO err;
      err.Set(WS_ERR, "Failed to receive response");
      if (spi_) {
        spi_->OnError(req.ref, &err);
      }
      return true;
    }

    // 解析响应
    std::multimap<std::string, std::string> headers;
    std::string status_code;
    std::string body;

    if (!ParseResponse(response, headers, status_code, body)) {
      WS_ERROR_INFO err;
      err.Set(WS_ERR, "Failed to parse response");
      if (spi_) {
        spi_->OnError(req.ref, &err);
      }
      return true;
    }

    // 回调
    if (spi_) {
      spi_->OnResponse(req.ref, body.c_str(), body.size(), headers,
                       status_code);
    }

    return true;
  }

  bool SendRequest(const HTTPRequest &req) {
    std::ostringstream oss;

    // 请求行
    oss << REST_REQ_METHOD[req.method] << " ";
    if (req.uri[0] == '/') {
      oss << req.uri;
    } else {
      oss << base_path_;
      if (base_path_[base_path_.size() - 1] != '/') {
        oss << "/";
      }
      oss << req.uri;
    }
    oss << " HTTP/1.1\r\n";

    // 添加Host头
    oss << "Host: " << host_ << "\r\n";

    // 添加自定义头
    for (const auto &hdr : req.headers) {
      oss << hdr.first << ": " << hdr.second << "\r\n";
    }

    // 添加Content-Length
    if (!req.body.empty()) {
      oss << "Content-Length: " << req.body.size() << "\r\n";
    }

    oss << "Connection: keep-alive\r\n";
    oss << "\r\n";

    // 添加body
    if (!req.body.empty()) {
      oss << req.body;
    }

    std::string request = oss.str();
    return SendRaw(request.c_str(), request.size());
  }

  bool ReceiveResponse(std::string &response) {
    char buffer[4096];
    response.clear();

    // 读取响应
    bool headers_complete = false;
    size_t content_length = 0;
    size_t body_start = 0;

    while (true) {
      int n = RecvRaw(buffer, sizeof(buffer) - 1);
      if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          // 短暂休眠后重试
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
        return false;
      }

      buffer[n] = '\0';
      response.append(buffer, n);

      // 检查是否收到完整的响应头
      if (!headers_complete) {
        size_t header_end = response.find("\r\n\r\n");
        if (header_end != std::string::npos) {
          headers_complete = true;
          body_start = header_end + 4;

          // 查找Content-Length
          size_t cl_pos = response.find("Content-Length:");
          if (cl_pos != std::string::npos && cl_pos < header_end) {
            size_t cl_start = response.find_first_not_of(" ", cl_pos + 15);
            size_t cl_end = response.find("\r\n", cl_start);
            if (cl_end != std::string::npos) {
              std::string cl_str = response.substr(cl_start, cl_end - cl_start);
              content_length = std::stoull(cl_str);
            }
          }
        }
      }

      // 检查是否收到完整的body
      if (headers_complete) {
        if (content_length > 0) {
          if (response.size() >= body_start + content_length) {
            break;
          }
        } else {
          // 没有Content-Length，使用chunked或连接关闭判断
          // 简化处理：假设已接收完成
          break;
        }
      }
    }

    return true;
  }

  bool ParseResponse(const std::string &response,
                     std::multimap<std::string, std::string> &headers,
                     std::string &status_code, std::string &body) {
    // 查找头部和body的分隔符
    size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      return false;
    }

    std::string header_part = response.substr(0, header_end);
    body = response.substr(header_end + 4);

    // 解析状态行
    size_t first_line_end = header_part.find("\r\n");
    if (first_line_end == std::string::npos) {
      return false;
    }

    std::string status_line = header_part.substr(0, first_line_end);

    // 提取状态码
    size_t space1 = status_line.find(' ');
    if (space1 != std::string::npos) {
      size_t space2 = status_line.find(' ', space1 + 1);
      if (space2 != std::string::npos) {
        status_code = status_line.substr(space1 + 1, space2 - space1 - 1);
      }
    }

    // 解析头部
    size_t pos = first_line_end + 2;
    while (pos < header_part.size()) {
      size_t line_end = header_part.find("\r\n", pos);
      if (line_end == std::string::npos) {
        break;
      }

      std::string line = header_part.substr(pos, line_end - pos);
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

    return true;
  }

  bool SendRaw(const char *data, size_t len) {
    if (use_https_ && ssl_) {
      int n = SSL_write(ssl_, data, len);
      return n == static_cast<int>(len);
    } else {
      ssize_t n = send(sockfd_, data, len, 0);
      return n == static_cast<ssize_t>(len);
    }
  }

  int RecvRaw(char *buffer, size_t len) {
    if (use_https_ && ssl_) {
      return SSL_read(ssl_, buffer, len);
    } else {
      return recv(sockfd_, buffer, len, 0);
    }
  }

private:
  RestClientSpi *spi_;
  bool use_https_;
  bool use_http2_;

  std::string host_;
  std::string base_path_;
  int port_;

  int sockfd_;
  SSL *ssl_;
  SSL_CTX *ssl_ctx_;

  bool running_;

  std::queue<HTTPRequest> request_queue_;
  std::mutex queue_mutex_;
};

// 工厂方法实现
RestClientApi *RestClientApi::Create(RestClientSpi *spi, bool https,
                                     bool http2) {
  return new RestClientApiImpl(spi, https, http2);
}

// FastRestClientApi 简单实现（占位）
class FastRestClientApiImpl : public FastRestClientApi {
public:
  static FastRestClientApi *Create(RestClientSpi *, bool, bool) {
    return new FastRestClientApiImpl();
  }

  void RegisterHeartBeat(REST_REQ_TYPE, const Message &, const Header &,
                         const Message &, int32_t, int32_t) {}

  WS_ERROR_INFO Initialize(const char *, int32_t, int32_t, bool, int, int) {
    WS_ERROR_INFO err;
    err.Set(WS_ERR, "FastRestClientApi not implemented");
    return err;
  }

  void Stop() {}

  WS_ERROR_INFO Request(uint64_t, REST_REQ_TYPE, const Message &,
                        const Header &, const Message &) {
    WS_ERROR_INFO err;
    err.Set(WS_ERR, "FastRestClientApi not implemented");
    return err;
  }
};

FastRestClientApi *FastRestClientApi::Create(RestClientSpi *spi, bool https,
                                             bool http2) {
  return FastRestClientApiImpl::Create(spi, https, http2);
}

END_NOVA_NAMESPACE(ws)
