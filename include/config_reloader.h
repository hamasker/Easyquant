#pragma once

#include "base/base_async_log.h"
#include "base/base_config.h"
#include "config_watcher.h"
#include <memory>
#include <string>

namespace nova {
namespace config {

// 配置重载管理器，专门处理日志等级的动态切换
class ConfigReloader {
public:
  ConfigReloader();
  ~ConfigReloader();

  // 开始监控配置文件并自动重载日志等级
  bool StartAutoReload(const std::string &config_file_path);

  // 停止自动重载
  void StopAutoReload();

  // 手动重载配置
  void ManualReload();

  // 手动重载配置并切换到file_level_real
  void ManualReloadWithRealLevel(const std::string &file_level_real);

  // 检查是否正在监控
  bool IsWatching() const {
    return config_watcher_ ? config_watcher_->IsWatching() : false;
  }

public:
  // 将字符串日志等级转换为枚举值
  nova::log::LogLevel StringToLogLevel(const std::string &level_str);

private:
  // 配置文件变化时的回调函数
  void OnConfigChanged(const std::string &config_file_path);

  // 重新加载日志配置
  void ReloadLogConfig(const nova::base::Config &cfg);

  // 应用新的日志等级
  void ApplyLogLevel(nova::log::LogLevel new_level);

private:
  std::unique_ptr<ConfigWatcher> config_watcher_;
  std::string current_config_file_;
  nova::log::LogLevel current_log_level_;
  bool auto_reload_enabled_;
  bool use_file_level_real_; // 是否已切换到使用file_level_real

  // 主线程的LoggerClient指针
  nova::log::LoggerClient *main_thread_logger_;
};

} // namespace config
} // namespace nova
