#include "config_reloader.h"
#include <algorithm>
#include <cstring>

#ifdef NOVA_BASE_DISABLE_LOGGER
#include "coinrunner_log.h"
#endif

namespace nova {
namespace config {

ConfigReloader::ConfigReloader()
    : current_log_level_(nova::log::LOG_LEVEL_DEBUG),
      auto_reload_enabled_(false), use_file_level_real_(false),
      main_thread_logger_(nullptr) {
#ifndef NOVA_BASE_DISABLE_LOGGER
  main_thread_logger_ = nova::log::LoggerClient::ThreadInstance();
#endif
}

ConfigReloader::~ConfigReloader() { StopAutoReload(); }

bool ConfigReloader::StartAutoReload(const std::string &config_file_path) {
  if (auto_reload_enabled_) {
    ERROR_FLOG("ConfigReloader: Auto-reload already enabled");
    return false;
  }

  current_config_file_ = config_file_path;

  config_watcher_ = std::make_unique<ConfigWatcher>();

  if (!config_watcher_->StartWatching(
          config_file_path,
          [this](const std::string &path) { this->OnConfigChanged(path); })) {
    ERROR_FLOG("ConfigReloader: Failed to start watching config file");
    config_watcher_.reset();
    return false;
  }

  ManualReload();

  INFO_FLOG("ConfigReloader: Initial configuration loaded");

  auto_reload_enabled_ = true;
  INFO_FLOG("ConfigReloader: Auto-reload enabled for {}", config_file_path);
  return true;
}

void ConfigReloader::StopAutoReload() {
  if (!auto_reload_enabled_) {
    return;
  }

  if (config_watcher_) {
    config_watcher_->StopWatching();
    config_watcher_.reset();
  }

  auto_reload_enabled_ = false;
  INFO_FLOG("ConfigReloader: Auto-reload disabled");
}

void ConfigReloader::ManualReload() {
  if (current_config_file_.empty()) {
    ERROR_FLOG("ConfigReloader: No config file specified");
    return;
  }

  nova::base::Config cfg;
  if (!cfg.Load(current_config_file_.c_str())) {
    ERROR_FLOG("ConfigReloader: Failed to load config file: {}",
               current_config_file_);
    return;
  }

  ReloadLogConfig(cfg);
}

void ConfigReloader::ManualReloadWithRealLevel(
    const std::string &file_level_real) {
  nova::log::LogLevel new_level = StringToLogLevel(file_level_real);

  INFO_FLOG("ConfigReloader: Setting log level to {} (real level)",
            nova::log::LogLevelString[new_level]);

  ApplyLogLevel(new_level);
  use_file_level_real_ = true;

  INFO_FLOG("ConfigReloader: Global log level change completed successfully");
}

void ConfigReloader::OnConfigChanged(const std::string &) {
  INFO_FLOG("ConfigReloader: Configuration file changed, reloading...");
  ManualReload();
}

void ConfigReloader::ReloadLogConfig(const nova::base::Config &cfg) {
  std::string file_level_str;

  if (use_file_level_real_) {
    std::string file_level_real_str;
    if (cfg.GetItemValue("Server.Log.file_level_real", file_level_real_str) &&
        !file_level_real_str.empty()) {
      file_level_str = file_level_real_str;
      INFO_FLOG("ConfigReloader: Using file_level_real: {} (runtime reload)",
                file_level_str);
    } else {
      ERROR_FLOG("ConfigReloader: Failed to get Server.Log.file_level_real for "
                 "runtime reload");
      return;
    }
  } else {
    if (cfg.GetItemValue("Server.Log.file_level", file_level_str)) {
      INFO_FLOG("ConfigReloader: Using file_level: {} (initial load)",
                file_level_str);
    } else {
      ERROR_FLOG("ConfigReloader: Failed to get Server.Log.file_level for "
                 "initial load");
      return;
    }
  }

  nova::log::LogLevel new_level = StringToLogLevel(file_level_str);

  INFO_FLOG("ConfigReloader: Setting log level to {}",
            nova::log::LogLevelString[new_level]);

  ApplyLogLevel(new_level);
  current_log_level_ = new_level;
}

nova::log::LogLevel
ConfigReloader::StringToLogLevel(const std::string &level_str) {
  std::string upper_level = level_str;
  std::transform(upper_level.begin(), upper_level.end(), upper_level.begin(),
                 ::toupper);

  if (upper_level == "FATAL")
    return nova::log::LOG_LEVEL_FATAL;
  if (upper_level == "ERROR")
    return nova::log::LOG_LEVEL_ERROR;
  if (upper_level == "WARNING" || upper_level == "WARN")
    return nova::log::LOG_LEVEL_WARNING;
  if (upper_level == "INFO")
    return nova::log::LOG_LEVEL_INFO;
  if (upper_level == "DEBUG")
    return nova::log::LOG_LEVEL_DEBUG;
  if (upper_level == "TRACE")
    return nova::log::LOG_LEVEL_TRACE;

  ERROR_FLOG("ConfigReloader: Unknown log level: {}, using DEBUG as default",
             level_str);
  return nova::log::LOG_LEVEL_DEBUG;
}

void ConfigReloader::ApplyLogLevel(nova::log::LogLevel new_level) {
  try {
#ifdef NOVA_BASE_DISABLE_LOGGER
    coinrunner::SetFileLevel(static_cast<int>(new_level));
    current_log_level_ = new_level;
    INFO_FLOG("ConfigReloader: Set coinrunner file log level to: {}",
              nova::log::LogLevelString[new_level]);
#else
    if (main_thread_logger_) {
      main_thread_logger_->set_max_level(new_level);
      INFO_FLOG("ConfigReloader: Set main thread logger level to: {}",
                nova::log::LogLevelString[new_level]);
    }

    auto *log_server = nova::log::LoggerServer::Instance();
    if (log_server) {
      auto all = log_server->GetAllAsyncClientInfo();
      int cnt = 0;
      for (auto &one : all) {
        auto *client = one.client;
        client->set_max_level(new_level);
        cnt++;
      }
      INFO_FLOG("ConfigReloader: Set {} client(s) log level to: {}", cnt,
                nova::log::LogLevelString[new_level]);
      DEBUG_FLOG("ConfigReloader: Total registered clients: {}", cnt);
    }

    current_log_level_ = new_level;
    INFO_FLOG("ConfigReloader: Successfully applied new log level: {}",
              nova::log::LogLevelString[new_level]);
#endif
    INFO_FLOG("ConfigReloader: Global log level change completed successfully");
  } catch (const std::exception &e) {
    ERROR_FLOG("ConfigReloader: Error applying log level: {}", e.what());
  }
}

} // namespace config
} // namespace nova
