# NovaBase WebSocket 和 HTTP 实现

## 概述

这个目录包含了 NovaBase 项目的完整网络通信实现：
- **WebSocket 客户端** - 用于连接WebSocket服务器
- **WebSocket 服务器** - 用于提供WebSocket服务
- **REST 客户端** - 用于HTTP/HTTPS请求

## 文件说明

### 头文件 (在 `../../include/tpl/nvws/`)

- `ws_struct.h` - WebSocket 基础数据结构定义
- `ws_util.h` - 工具函数声明
- `ws_thread_pool.h` - 线程池类声明
- `ws_websocket_client.h` - WebSocket 客户端API接口

### 源文件

- `ws_util.cpp` - 工具函数实现
  - gzip解压缩 (`gz_decompress`)
  - SSL环境初始化 (`SSL_env_init`)

- `ws_thread_pool.cpp` - 线程池实现 (单例模式)
  - 异步任务调度
  - CPU核心绑定支持
  - 任务循环执行
  - 全局单例访问

- `ws_websocket_client.cpp` - WebSocket客户端实现
  - WebSocket协议握手
  - 帧的发送和接收
  - TLS/SSL支持
  - Ping/Pong心跳

- `ws_websocket_server.cpp` - WebSocket服务器实现
  - 多客户端连接管理
  - WebSocket协议握手处理
  - 帧的接收和发送
  - 会话管理
  - TLS/SSL支持（可选）

- `ws_web_client.cpp` - REST HTTP/HTTPS客户端实现
  - GET/POST/PUT/DELETE请求
  - HTTPS支持
  - 请求队列管理
  - 异步请求处理

## 依赖项

### 必需的库

1. **OpenSSL** - 用于TLS/SSL连接
   ```bash
   # Ubuntu/Debian
   sudo apt-get install libssl-dev
   
   # CentOS/RHEL
   sudo yum install openssl-devel
   ```

2. **zlib** - 用于数据解压缩
   ```bash
   # Ubuntu/Debian
   sudo apt-get install zlib1g-dev
   
   # CentOS/RHEL
   sudo yum install zlib-devel
   ```

3. **pthread** - POSIX线程库（通常系统自带）

## 编译

### 使用 CMake 编译

```bash
# 在 NovaBase 根目录下
mkdir -p build
cd build
cmake ..
make
```

这将编译生成 `libnvws.a` 静态库。

### 编译示例程序

```bash
cd build
make example_client
./example_client
```

或者指定WebSocket URL：
```bash
./example_client wss://echo.websocket.org
```

## 使用方法

### WebSocket 客户端

1. **实现回调接口**

```cpp
#include "nvws/ws_websocket_client.h"

class MyClient : public nova::ws::WSClientSpi {
public:
    void OnOpen(int group) override {
        // 连接建立时的回调
    }
    
    void OnMessage(const char *msg, size_t len, int group) override {
        // 收到消息时的回调
    }
    
    void OnClose(bool manual) override {
        // 连接关闭时的回调
    }
    
    void OnFail(nova::ws::WS_ERROR_INFO err) override {
        // 连接失败时的回调
    }
};
```

2. **创建客户端并连接**

```cpp
MyClient spi;
auto *client = nova::ws::WSClientApi::Create(&spi, true, 0);

// 参数：URL, CPU核心(-1表示不绑定), 循环间隔(ms), 全局线程
auto err = client->Initialize("wss://example.com/ws", -1, 10, false);

if (err.code == nova::ws::WS_OK) {
    // 连接成功
}
```

3. **清理资源**

```cpp
client->Stop();
delete client;
```

### WebSocket 服务器

1. **实现服务器回调**

```cpp
#include "nvws/ws_websocket_server.h"

class MyServer : public nova::ws::WSServerSpi {
public:
    void OnOpen(session_t session) override {
        // 新客户端连接
    }
    
    void OnMessage(session_t session, const char *msg, size_t len) override {
        // 收到客户端消息
        // 可以使用 server->Send() 回复
    }
    
    void OnClose(session_t session, WS_ERROR_INFO close_info) override {
        // 客户端断开连接
    }
};
```

2. **启动服务器**

```cpp
MyServer spi;
auto *server = nova::ws::WSServerApi::Create(&spi, false, "wspp");

// 监听 0.0.0.0:8080
auto err = server->Initialize("0.0.0.0", 8080, -1, 10, false);

// 发送消息给客户端
server->Send(session_id, ref_id, "Hello", 5);

// 关闭服务器
server->Stop();
delete server;
```

### REST HTTP/HTTPS 客户端

1. **实现REST回调**

```cpp
#include "nvws/ws_web_client.h"

class MyRestClient : public nova::ws::RestClientSpi {
public:
    void OnResponse(uint64_t ref, const char *msg, size_t len,
                   const ResponseHeader &header,
                   const std::string &status_code) override {
        // 处理HTTP响应
    }
    
    void OnError(uint64_t ref, const WS_ERROR_INFO *err) override {
        // 处理错误
    }
};
```

2. **发送HTTP请求**

```cpp
MyRestClient spi;
auto *client = nova::ws::RestClientApi::Create(&spi, true, false);

// 初始化客户端
client->Initialize("https://api.example.com", -1, 10, false);

// 发送GET请求
RestClientApi::Header headers;
headers["User-Agent"] = "MyApp/1.0";

client->Request(1, REST_REQ_GET, "/endpoint", headers, "");

// 发送POST请求
std::string json_body = R"({"key":"value"})";
headers["Content-Type"] = "application/json";
client->Request(2, REST_REQ_POST, "/api/data", headers, json_body);

// 清理
client->Stop();
delete client;
```

## 功能特性

### 已实现的功能

- ✅ WebSocket 客户端连接
- ✅ TLS/SSL 加密支持 (wss://)
- ✅ 非加密连接支持 (ws://)
- ✅ WebSocket 握手协议
- ✅ 帧的发送和接收
- ✅ Ping/Pong 心跳支持
- ✅ 异步消息处理
- ✅ CPU 核心绑定
- ✅ 线程池任务调度

### 支持的WebSocket操作码

- `TEXT` (0x1) - 文本消息
- `BINARY` (0x2) - 二进制消息
- `CLOSE` (0x8) - 关闭连接
- `PING` (0x9) - Ping请求
- `PONG` (0xA) - Pong响应

## API 参考

### WSClientSpi 回调接口

```cpp
class WSClientSpi {
    virtual void OnFail(WS_ERROR_INFO err);      // 连接失败
    virtual void OnOpen(int group);              // 连接建立
    virtual void OnMessage(const char *msg, size_t len, int group); // 收到消息
    virtual void OnClose(bool manual);           // 连接关闭
    virtual void OnGroupClose(bool manual, int group); // 组关闭
    virtual void OnPong(const char *msg, size_t len);  // Pong响应
};
```

### WSClientApi 接口

```cpp
class WSClientApi {
    static WSClientApi *Create(WSClientSpi *spi, bool tls = true, int group = -1);
    
    virtual WS_ERROR_INFO Initialize(
        const char *url,           // WebSocket URL
        int32_t core = -1,         // CPU核心（-1表示不绑定）
        int32_t loop_ms = 10,      // 循环间隔（毫秒）
        bool global_thread = false // 是否使用全局线程
    ) = 0;
    
    virtual void Stop() = 0;       // 停止客户端
};
```

## 错误码

```cpp
enum WS_ERROR_CODE {
    WS_OK = 0,                      // 成功
    WS_ERR = 1,                     // 一般错误
    WS_ERR_SESSION_NOT_FOUND = 11,  // 会话未找到
    WS_ERR_SESSION_INVALID_STATE = 12, // 会话状态无效
    WS_ERR_INVALID_SERVICE = 13,    // 无效的服务
    WS_ERR_INTERNAL = 90,           // 内部错误
    WS_ERR_CLSOE_MANUAL = 91,       // 手动关闭
    WS_ERR_TIMEOUT = 92,            // 超时
    WS_ERR_FORMATTER = 93,          // 格式错误
    WS_ERR_UNKNOWN = 100            // 未知错误
};
```

## 注意事项

1. **线程安全**：客户端的回调函数在独立的线程中调用，需要注意线程安全
2. **资源释放**：使用完毕后必须调用 `Stop()` 和 `delete` 来释放资源
3. **SSL证书**：使用wss://时，需要系统已安装有效的CA证书
4. **连接超时**：默认连接超时为5秒

## 已知限制

1. 目前只支持客户端模式，不支持服务器模式（服务器实现在其他文件中）
2. 不支持WebSocket扩展协议（如压缩）
3. 帧的最大大小受限于可用内存

## 示例场景

### 连接到交易所WebSocket

```cpp
// Binance WebSocket
client->Initialize("wss://stream.binance.com:9443/ws", -1, 10);

// Kraken WebSocket
client->Initialize("wss://ws.kraken.com", -1, 10);

// OKX WebSocket
client->Initialize("wss://ws.okx.com:8443/ws/v5/public", -1, 10);
```

## 故障排查

### 编译错误

1. **找不到 zlib.h**
   - 解决：安装 zlib 开发包

2. **找不到 openssl/ssl.h**
   - 解决：安装 OpenSSL 开发包

3. **链接错误**
   - 解决：确保链接了 `-lssl -lcrypto -lz -lpthread`

### 运行时错误

1. **连接超时**
   - 检查网络连接
   - 检查URL是否正确
   - 检查防火墙设置

2. **SSL错误**
   - 确保系统CA证书已安装
   - 尝试使用 ws:// 而非 wss:// 进行测试

## 贡献

欢迎提交问题和改进建议！

## 许可证

请参考 NovaBase 项目的整体许可证。

