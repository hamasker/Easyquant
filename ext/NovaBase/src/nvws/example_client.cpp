#include "nvws/ws_websocket_client.h"
#include <chrono>
#include <iostream>
#include <thread>

USE_NOVA_NAMESPACE(ws)

// 示例WebSocket客户端回调类
class MyWSClient : public WSClientSpi {
public:
  void OnFail(WS_ERROR_INFO err) override {
    std::cout << "连接失败: [" << err.code << "] " << err.msg << std::endl;
  }

  void OnOpen(int group) override {
    std::cout << "WebSocket连接已建立，组ID: " << group << std::endl;
  }

  void OnMessage(const char *msg, size_t len, int group) override {
    std::cout << "收到消息 [组" << group << "]: ";
    std::cout.write(msg, len);
    std::cout << std::endl;
  }

  void OnClose(bool manual) override {
    std::cout << "连接已关闭，是否手动: " << (manual ? "是" : "否")
              << std::endl;
  }

  void OnGroupClose(bool manual, int group) override {
    std::cout << "组 " << group
              << " 已关闭，是否手动: " << (manual ? "是" : "否") << std::endl;
  }

  void OnPong(const char *msg, size_t len) override {
    std::cout << "收到Pong响应，长度: " << len << std::endl;
  }
};

int main(int argc, char *argv[]) {
  // 示例：连接到WebSocket测试服务器
  const char *ws_url = "wss://echo.websocket.org";

  if (argc > 1) {
    ws_url = argv[1];
  }

  std::cout << "连接到: " << ws_url << std::endl;

  // 创建WebSocket客户端
  MyWSClient client_spi;
  WSClientApi *client = WSClientApi::Create(&client_spi, true, 0);

  // 初始化并连接
  auto err = client->Initialize(ws_url, -1, 10, false);
  if (err.code != WS_OK) {
    std::cerr << "初始化失败: " << err.msg << std::endl;
    return 1;
  }

  // 等待连接建立
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // 发送测试消息
  std::cout << "发送测试消息..." << std::endl;
  // 注意：这里需要在实现中添加SendText方法到公共接口

  // 保持连接一段时间
  std::cout << "保持连接30秒..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(30));

  // 停止客户端
  std::cout << "停止客户端..." << std::endl;
  client->Stop();

  // 清理
  delete client;

  std::cout << "程序结束" << std::endl;
  return 0;
}
