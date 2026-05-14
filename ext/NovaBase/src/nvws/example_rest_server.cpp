#include "nvws/ws_web_server.h"
#include <chrono>
#include <iostream>
#include <thread>

USE_NOVA_NAMESPACE(ws)

// 示例REST服务器回调类
class MyRestServer : public RestServerSpi {
public:
  void OnRequest(session_t session, REST_REQ_TYPE method,
                 const std::string &uri, const RequestHeader &headers,
                 const char *body, size_t body_len) override {
    std::cout << "\n=== 收到请求 ===" << std::endl;
    std::cout << "会话ID: " << session << std::endl;
    std::cout << "方法: " << REST_REQ_METHOD[method] << std::endl;
    std::cout << "URI: " << uri << std::endl;
    std::cout << "头部:" << std::endl;
    for (const auto &h : headers) {
      std::cout << "  " << h.first << ": " << h.second << std::endl;
    }
    if (body_len > 0) {
      std::cout << "Body (" << body_len << " 字节): ";
      std::cout.write(body, body_len);
      std::cout << std::endl;
    }
    std::cout << "==================" << std::endl;
  }

  void OnError(session_t session, WS_ERROR_INFO err) override {
    std::cout << "会话 " << session << " 错误: " << err.msg << std::endl;
  }

  void OnConnectionOpen(session_t session) override {
    std::cout << "新连接: " << session << std::endl;
  }

  void OnConnectionClose(session_t session) override {
    std::cout << "连接关闭: " << session << std::endl;
  }

  void SetServer(RestServerApi *server) { server_ = server; }

private:
  RestServerApi *server_ = nullptr;
};

int main(int argc, char *argv[]) {
  const char *ip = "0.0.0.0";
  uint16_t port = 8080;

  if (argc > 1) {
    port = std::atoi(argv[1]);
  }

  std::cout << "启动REST服务器..." << std::endl;
  std::cout << "监听: " << ip << ":" << port << std::endl;

  // 创建REST服务器
  MyRestServer server_spi;
  RestServerApi *server = RestServerApi::Create(&server_spi, false);
  server_spi.SetServer(server);

  // 注册路由
  server->RegisterRoute(
      REST_REQ_GET, "/",
      [server](session_t session, const std::string &uri,
               const RestServerApi::Header &hdr, const std::string &body) {
        (void)uri;
        (void)hdr;
        (void)body;

        RestServerApi::Header resp_headers;
        resp_headers["Content-Type"] = "text/html; charset=utf-8";

        std::string html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>NovaBase REST Server</title>
</head>
<body>
    <h1>欢迎使用 NovaBase REST 服务器</h1>
    <p>服务器正在运行！</p>
    <ul>
        <li><a href="/api/hello">GET /api/hello</a></li>
        <li><a href="/api/status">GET /api/status</a></li>
    </ul>
</body>
</html>
)";

        server->SendResponse(session, "200 OK", resp_headers, html);
      });

  server->RegisterRoute(
      REST_REQ_GET, "/api/hello",
      [server](session_t session, const std::string &uri,
               const RestServerApi::Header &hdr, const std::string &body) {
        (void)uri;
        (void)hdr;
        (void)body;

        RestServerApi::Header resp_headers;
        resp_headers["Content-Type"] = "application/json";

        std::string json =
            R"({"message": "Hello from NovaBase!", "status": "success"})";
        server->SendResponse(session, "200 OK", resp_headers, json);
      });

  server->RegisterRoute(
      REST_REQ_GET, "/api/status",
      [server](session_t session, const std::string &uri,
               const RestServerApi::Header &hdr, const std::string &body) {
        (void)uri;
        (void)hdr;
        (void)body;

        RestServerApi::Header resp_headers;
        resp_headers["Content-Type"] = "application/json";

        std::string json =
            R"({"server": "NovaBase REST Server", "version": "1.0", "status": "running"})";
        server->SendResponse(session, "200 OK", resp_headers, json);
      });

  server->RegisterRoute(
      REST_REQ_POST, "/api/echo",
      [server](session_t session, const std::string &uri,
               const RestServerApi::Header &hdr, const std::string &body) {
        (void)uri;
        (void)hdr;

        RestServerApi::Header resp_headers;
        resp_headers["Content-Type"] = "text/plain";

        server->SendResponse(session, "200 OK", resp_headers, body);
      });

  // 设置404处理器
  server->SetNotFoundHandler([server](session_t session, const std::string &uri,
                                      const RestServerApi::Header &hdr,
                                      const std::string &body) {
    (void)hdr;
    (void)body;

    RestServerApi::Header resp_headers;
    resp_headers["Content-Type"] = "text/plain";

    std::string msg = "404 Not Found: " + uri;
    server->SendResponse(session, "404 Not Found", resp_headers, msg);
  });

  // 初始化并启动服务器
  auto err = server->Initialize(ip, port, -1, 10, false);
  if (err.code != WS_OK) {
    std::cerr << "服务器初始化失败: " << err.msg << std::endl;
    delete server;
    return 1;
  }

  std::cout << "服务器启动成功！" << std::endl;
  std::cout << "访问: http://localhost:" << port << "/" << std::endl;
  std::cout << "API endpoints:" << std::endl;
  std::cout << "  GET  http://localhost:" << port << "/api/hello" << std::endl;
  std::cout << "  GET  http://localhost:" << port << "/api/status" << std::endl;
  std::cout << "  POST http://localhost:" << port << "/api/echo" << std::endl;
  std::cout << "按 Ctrl+C 停止服务器" << std::endl;

  // 保持服务器运行
  while (server->is_running()) {
    server->Poll();
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // 停止服务器
  std::cout << "停止服务器..." << std::endl;
  server->Stop();

  // 清理
  delete server;

  std::cout << "服务器已停止" << std::endl;
  return 0;
}
