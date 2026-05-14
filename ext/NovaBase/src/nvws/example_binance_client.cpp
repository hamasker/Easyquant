#include "nvws/ws_websocket_client.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

USE_NOVA_NAMESPACE(ws)

static std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

class BinanceWSClient : public WSClientSpi {
public:
  BinanceWSClient(std::string symbol, std::string channel)
      : symbol_(ToLower(std::move(symbol))), channel_(ToLower(std::move(channel))) {}

  void OnFail(WS_ERROR_INFO err) override {
    std::cout << "连接失败: [" << err.code << "] " << err.msg << std::endl;
  }

  void OnOpen(int group) override {
    std::cout << "Binance WebSocket 已连接，组ID: " << group << std::endl;
    // 订阅：{"method":"SUBSCRIBE","params":["btcusdt@ticker"],"id":1}
    std::string stream = symbol_ + "@" + channel_;
    std::string sub =
        std::string("{\"method\":\"SUBSCRIBE\",\"params\":[\"") + stream +
        "\"],\"id\":1}";
    if (api_) {
      auto err = api_->Send(sub);
      if (err.code != WS_OK) {
        std::cout << "发送订阅失败: " << err.msg << std::endl;
      } else {
        std::cout << "已发送订阅: " << sub << std::endl;
      }
    }
  }

  void OnMessage(const char *msg, size_t len, int group) override {
    std::cout << "收到消息 [组" << group << "]: ";
    std::cout.write(msg, len);
    std::cout << std::endl;
  }

  void OnClose(bool manual) override {
    std::cout << "连接已关闭，是否手动: " << (manual ? "是" : "否") << std::endl;
  }

  void Attach(WSClientApi *api) { api_ = api; }

private:
  std::string symbol_;
  std::string channel_;
  WSClientApi *api_ = nullptr;
};

int main(int argc, char *argv[]) {
  const char *ws_url = "wss://stream.binance.com:9443/ws"; // Binance 现货公共WS
  std::string symbol = "BTCUSDT";                          // 默认交易对
  std::string channel = "ticker";                          // 默认频道：ticker

  if (argc > 1) symbol = argv[1];
  if (argc > 2) channel = argv[2];

  std::cout << "连接: " << ws_url << "\n订阅: channel=" << channel
            << ", symbol=" << symbol << std::endl;

  BinanceWSClient spi(symbol, channel);
  WSClientApi *client = WSClientApi::Create(&spi, true, 0);
  spi.Attach(client);

  auto err = client->Initialize(ws_url, -1, 10, false);
  if (err.code != WS_OK) {
    std::cerr << "初始化失败: " << err.msg << std::endl;
    return 1;
  }

  // 持续接收并打印
  std::cout << "接收中，按 Ctrl+C 退出..." << std::endl;
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  client->Stop();
  return 0;
}


