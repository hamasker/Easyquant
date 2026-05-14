#include "nvws/ws_web_client.h"
#include <chrono>
#include <iostream>
#include <thread>

USE_NOVA_NAMESPACE(ws)

// 示例REST客户端回调类
class MyRestClient : public RestClientSpi {
public:
  void OnError(uint64_t ref, const WS_ERROR_INFO *err) override {
    std::cout << "请求 " << ref << " 错误: " << err->msg << std::endl;
  }

  void OnSendRequest(uint64_t ref) override {
    std::cout << "请求 " << ref << " 已发送" << std::endl;
  }

  void OnResponse(uint64_t ref, const char *msg, size_t len,
                  const ResponseHeader &header,
                  const std::string &status_code) override {
    std::cout << "\n=== 响应 " << ref << " ===" << std::endl;
    std::cout << "状态码: " << status_code << std::endl;
    std::cout << "头部信息:" << std::endl;
    for (const auto &h : header) {
      std::cout << "  " << h.first << ": " << h.second << std::endl;
    }
    std::cout << "响应内容 (" << len << " 字节):" << std::endl;
    std::cout.write(msg, len);
    std::cout << "\n===================" << std::endl;
  }
};

int main(int argc, char *argv[]) {
  // 默认测试URL
  const char *base_url = "https://api.github.com";
  const char *endpoint = "/";

  if (argc > 1) {
    base_url = argv[1];
  }
  if (argc > 2) {
    endpoint = argv[2];
  }

  std::cout << "REST客户端示例" << std::endl;
  std::cout << "服务器: " << base_url << std::endl;
  std::cout << "端点: " << endpoint << std::endl;

  // 创建REST客户端
  MyRestClient client_spi;
  RestClientApi *client = RestClientApi::Create(&client_spi, true, false);

  // 初始化并连接
  auto err = client->Initialize(base_url, -1, 10, false);
  if (err.code != WS_OK) {
    std::cerr << "客户端初始化失败: " << err.msg << std::endl;
    delete client;
    return 1;
  }

  std::cout << "客户端初始化成功" << std::endl;

  // 等待连接建立
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // 发送GET请求
  std::cout << "\n发送GET请求到: " << endpoint << std::endl;

  RestClientApi::Header headers;
  headers["User-Agent"] = "NovaBase-REST-Client/1.0";
  headers["Accept"] = "application/json";

  err = client->Request(1, REST_REQ_GET, endpoint, headers, "");
  if (err.code != WS_OK) {
    std::cerr << "请求失败: " << err.msg << std::endl;
  }

  // 等待响应
  std::cout << "等待响应..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  // 停止客户端
  std::cout << "\n停止客户端..." << std::endl;
  client->Stop();

  // 清理
  delete client;

  std::cout << "程序结束" << std::endl;
  return 0;
}
