#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace nova {
namespace config {

// 配置文件变化回调函数类型
using ConfigChangeCallback = std::function<void(const std::string &)>;

// 配置文件监控器类
class ConfigWatcher {
public:
  ConfigWatcher();
  ~ConfigWatcher();

  // 开始监控配置文件
  bool StartWatching(const std::string &config_file_path,
                     ConfigChangeCallback callback);

  // 停止监控
  void StopWatching();

  // 检查是否正在监控
  bool IsWatching() const { return watching_.load(); }

private:
  // 监控线程函数
  void WatchLoop();

  // 处理inotify事件
  void HandleInotifyEvents();

  // 重新加载配置文件
  void ReloadConfig();

private:
  std::string config_file_path_;
  ConfigChangeCallback callback_;
  std::atomic<bool> watching_;
  std::atomic<bool> stop_requested_;
  std::unique_ptr<std::thread> watch_thread_;

  // inotify相关
  int inotify_fd_;
  int watch_descriptor_;

  // 文件状态
  std::string last_file_content_;
  std::chrono::steady_clock::time_point last_modify_time_;
};

} // namespace config
} // namespace nova
