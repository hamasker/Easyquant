# NovaBase WebSocket 源文件

# 查找依赖库
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

# WebSocket 源文件
set(NVWS_SOURCES
    ws_util.cpp
    ws_thread_pool.cpp
    ws_websocket_client.cpp
    ws_websocket_server.cpp
    ws_web_client.cpp
    ws_web_server.cpp
)

# 创建静态库
add_library(nvws STATIC ${NVWS_SOURCES})

# 包含目录
target_include_directories(nvws PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/base
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/tpl
)

# 链接库
target_link_libraries(nvws PUBLIC
    ZLIB::ZLIB
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
)

# 设置编译选项
target_compile_options(nvws PRIVATE
    -Wall
    -Wextra
    -fPIC
)

# 安装库文件
install(TARGETS nvws
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# 示例：Kraken 行情客户端
add_executable(example_kraken_client example_kraken_client.cpp)
target_link_libraries(example_kraken_client PRIVATE nvws ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto pthread)

# 示例：Binance 行情客户端
add_executable(example_binance_client example_binance_client.cpp)
target_link_libraries(example_binance_client PRIVATE nvws ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto pthread)

# 示例：Nova 行情中心 WebSocket 桥接
add_executable(example_nova_quote_bridge example_nova_quote_bridge.cpp)
target_link_libraries(example_nova_quote_bridge PRIVATE nvws ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto pthread)
target_include_directories(example_nova_quote_bridge PRIVATE
    ${PROJECT_SOURCE_DIR}/ext/NovaBase/include
    ${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include
    ${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include/api
    ${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include/quote
    ${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include/trade
    ${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include/symbol_translator
)
target_compile_definitions(example_nova_quote_bridge PRIVATE
    NOVA_BASE_DISABLE_LOGGER
    FMT_HEADER_ONLY
)

