#include "nvws/ws_websocket_client.h"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

USE_NOVA_NAMESPACE(ws)

class KrakenWSClient : public WSClientSpi {
public:
  void OnFail(WS_ERROR_INFO err) override {
    std::cout << "连接失败: [" << err.code << "] " << err.msg << std::endl;
  }

  void OnOpen(int group) override {
    std::cout << "Kraken WebSocket 已连接，组ID: " << group << std::endl;
  }

  void OnMessage(const char *msg, size_t len, int group) override {
    std::cout << "收到消息 [组" << group << "]: ";
    std::cout.write(msg, len);
    std::cout << std::endl;
  }

  void OnClose(bool manual) override {
    std::cout << "连接已关闭，是否手动: " << (manual ? "是" : "否") << std::endl;
  }

  void OnPong(const char *msg, size_t len) override {
    (void)msg;
    std::cout << "收到Pong响应，长度: " << len << std::endl;
  }
};

static std::string BuildSubscribeMsg(const std::string &channel,
                                     const std::string &pair) {
  // Kraken 订阅消息格式: {"event":"subscribe","pair":["XBT/USD"],"subscription":{"name":"ticker"}}
  std::string json =
      std::string("{\"event\":\"subscribe\",\"pair\":[\"") + pair +
      "\"],\"subscription\":{\"name\":\"" + channel + "\"}}";
  return json;
}

int main(int argc, char *argv[]) {
  const char *ws_url = "wss://ws.kraken.com"; // Kraken 公共行情WS
  std::string channel = "ticker";             // 默认订阅 ticker
  std::string pair = "XBT/USD";               // 默认交易对

  if (argc > 1) {
    pair = argv[1];
  }
  if (argc > 2) {
    channel = argv[2];
  }

  std::cout << "连接: " << ws_url << "\n订阅: channel=" << channel
            << ", pair=" << pair << std::endl;

  KrakenWSClient spi;
  WSClientApi *client = WSClientApi::Create(&spi, true, 0);

  auto err = client->Initialize(ws_url, -1, 10, false);
  if (err.code != WS_OK) {
    std::cerr << "初始化失败: " << err.msg << std::endl;
    return 1;
  }

  // 等待握手完成
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // 发送订阅
  std::string sub = BuildSubscribeMsg(channel, pair);
  err = client->Send(sub);
  if (err.code != WS_OK) {
    std::cerr << "发送订阅失败: " << err.msg << std::endl;
  } else {
    std::cout << "已发送订阅: " << sub << std::endl;
  }

  // 持续接收并打印
  std::cout << "接收中，按 Ctrl+C 退出..." << std::endl;
  for (int i = 0; i < 600; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  client->Stop();

  return 0;
}


