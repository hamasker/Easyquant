#pragma once

#include "feed/feed_engine.h"
#include "mock/mock_server.h"

#include <memory>
#include <string>
#include <vector>

namespace nova::trade {
class StrategyApi;
class MockTradeDispatcherService;
} // namespace nova::trade

/**
 * StrategyRunner — 策略生命周期管理器。
 *
 * 负责:
 *   1. 初始化 Mock 框架 + 注册 TradeEngine
 *   2. 加载配置 JSON
 *   3. 实例化策略并调用 on_init
 *   4. 创建 FeedEngine 并进入主循环
 *   5. 处理 Reminder 定时回调
 *
 * Mode:
 *   - "mock":    模拟行情 + 模拟交易
 *   - "backtest": 历史数据回放 + 模拟交易
 *   - "prod":    真实交易所 WebSocket 行情 + 模拟交易
 */
class StrategyRunner {
public:
  StrategyRunner();
  ~StrategyRunner();

  // 设置运行模式: "mock", "backtest", "prod"
  void SetMode(const std::string &mode) { mode_ = mode; }
  const std::string &GetMode() const { return mode_; }

  // 设置配置 JSON 文件路径
  void SetConfigFile(const std::string &path) { config_path_ = path; }
  const std::string &GetConfigFile() const { return config_path_; }

  // 设置回测数据目录
  void SetDataDir(const std::string &dir) { data_dir_ = dir; }

  // 注册一个 TradeEngine (在 Initialize 之前调用)
  void RegisterEngine(nova::trade::TradeEngine *engine);

  // 初始化并运行策略
  bool Initialize();
  void Run();
  void Stop();

private:
  bool InitConfig();
  bool InitMockFramework();
  bool InitEngines();
  bool InitStrategy();
  bool InitFeed();
  bool InitProdFeeds();
  void MainLoop();
  void ProcessReminders(uint64_t cur_ns);

  std::string mode_ = "mock";
  std::string config_path_ = "config/taking_all_multi.json";
  std::string data_dir_;

  struct EngineWsInfo {
    std::string ws_url;   // ws_front_address from config
    int ws_core = -1;     // ws_core from config
  };
  // ws_exch (bn/bn_swap/krk/ok/cb) → WS config
  std::unordered_map<std::string, EngineWsInfo> ws_configs_;

  std::vector<nova::trade::TradeEngine *> engines_;
  nova::trade::StrategyApi *strategy_ = nullptr;
  std::vector<std::unique_ptr<nova::trade::FeedEngine>> feeds_;

  bool running_ = false;
};
