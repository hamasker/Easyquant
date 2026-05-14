#include "nvws/ws_websocket_server.h"
#include <chrono>
#include <iostream>
#include <thread>

USE_NOVA_NAMESPACE(ws)

// 示例WebSocket服务器回调类
class MyWSServer : public WSServerSpi {
public:
  void OnOpen(session_t session) override {
    std::cout << "新客户端连接，会话ID: " << session << std::endl;
  }

  void OnMessage(session_t session, const char *msg, size_t len) override {
    std::cout << "收到来自会话 " << session << " 的消息: ";
    std::cout.write(msg, len);
    std::cout << std::endl;

    // 回显消息给客户端
    server_->Send(session, 0, msg, len);
  }

  void OnClose(session_t session, WS_ERROR_INFO close_info) override {
    std::cout << "会话 " << session << " 已关闭: " << close_info.msg
              << std::endl;
  }

  void OnSend(session_t session, int64_t ref) override {
    (void)session;
    (void)ref;
    // 消息发送成功
  }

  void OnError(WS_ERROR_INFO err, session_t session, int64_t ref) override {
    (void)ref;
    std::cout << "会话 " << session << " 错误: " << err.msg << std::endl;
  }

  void SetServer(WSServerApi *server) { server_ = server; }

private:
  WSServerApi *server_ = nullptr;
};

int main(int argc, char *argv[]) {
  // 默认监听参数
  const char *ip = "0.0.0.0";
  uint16_t port = 8080;

  if (argc > 1) {
    port = std::atoi(argv[1]);
  }

  std::cout << "启动WebSocket服务器..." << std::endl;
  std::cout << "监听: " << ip << ":" << port << std::endl;

  // 创建WebSocket服务器
  MyWSServer server_spi;
  WSServerApi *server = WSServerApi::Create(&server_spi, false, "wspp");
  server_spi.SetServer(server);

  // 初始化并启动服务器
  auto err = server->Initialize(ip, port, -1, 10, false);
  if (err.code != WS_OK) {
    std::cerr << "服务器初始化失败: " << err.msg << std::endl;
    delete server;
    return 1;
  }

  std::cout << "服务器启动成功！" << std::endl;
  std::cout << "等待客户端连接..." << std::endl;
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
