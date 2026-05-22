#include "config_watcher.h"
#include "base/base_async_log.h"
#include <chrono>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nova {
namespace config {

ConfigWatcher::ConfigWatcher()
    : watching_(false), stop_requested_(false), inotify_fd_(-1),
      watch_descriptor_(-1) {}

ConfigWatcher::~ConfigWatcher() { StopWatching(); }

bool ConfigWatcher::StartWatching(const std::string &config_file_path,
                                  ConfigChangeCallback callback) {
  if (watching_.load()) {
    ERROR_FLOG("ConfigWatcher: Already watching a file");
    return false;
  }

  config_file_path_ = config_file_path;
  callback_ = callback;

  inotify_fd_ = inotify_init();
  if (inotify_fd_ == -1) {
    ERROR_FLOG("ConfigWatcher: Failed to initialize inotify: {}",
               strerror(errno));
    return false;
  }

  int flags = fcntl(inotify_fd_, F_GETFL, 0);
  fcntl(inotify_fd_, F_SETFL, flags | O_NONBLOCK);

  watch_descriptor_ =
      inotify_add_watch(inotify_fd_, config_file_path_.c_str(),
                        IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF);
  if (watch_descriptor_ == -1) {
    ERROR_FLOG("ConfigWatcher: Failed to add watch for {}: {}",
               config_file_path_, strerror(errno));
    close(inotify_fd_);
    inotify_fd_ = -1;
    return false;
  }

  std::ifstream file(config_file_path_);
  if (file.is_open()) {
    std::stringstream buffer;
    buffer << file.rdbuf();
    last_file_content_ = buffer.str();
    file.close();
  }

  last_modify_time_ = std::chrono::steady_clock::now();

  watching_.store(true);
  stop_requested_.store(false);
  watch_thread_ =
      std::make_unique<std::thread>(&ConfigWatcher::WatchLoop, this);

  INFO_FLOG("ConfigWatcher: Started watching {}", config_file_path_);
  return true;
}

void ConfigWatcher::StopWatching() {
  if (!watching_.load()) {
    return;
  }

  stop_requested_.store(true);

  if (watch_thread_ && watch_thread_->joinable()) {
    watch_thread_->join();
  }

  if (watch_descriptor_ != -1) {
    inotify_rm_watch(inotify_fd_, watch_descriptor_);
    watch_descriptor_ = -1;
  }

  if (inotify_fd_ != -1) {
    close(inotify_fd_);
    inotify_fd_ = -1;
  }

  watching_.store(false);
  INFO_FLOG("ConfigWatcher: Stopped watching {}", config_file_path_);
}

void ConfigWatcher::WatchLoop() {
  while (!stop_requested_.load()) {
    HandleInotifyEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void ConfigWatcher::HandleInotifyEvents() {
  const size_t buffer_size = sizeof(struct inotify_event) + 256;
  char buffer[buffer_size];

  ssize_t length = read(inotify_fd_, buffer, buffer_size);
  if (length == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return;
    }
    ERROR_FLOG("ConfigWatcher: Error reading inotify events: {}",
               strerror(errno));
    return;
  }

  size_t i = 0;
  while (i < static_cast<size_t>(length)) {
    struct inotify_event *event =
        reinterpret_cast<struct inotify_event *>(&buffer[i]);

    if (event->mask & IN_MODIFY) {
      INFO_FLOG("ConfigWatcher: File modified: {}", config_file_path_);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      ReloadConfig();
    } else if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
      INFO_FLOG("ConfigWatcher: File deleted/moved: {}", config_file_path_);
      break;
    }

    i += sizeof(struct inotify_event) + event->len;
  }
}

void ConfigWatcher::ReloadConfig() {
  struct stat file_stat;
  if (stat(config_file_path_.c_str(), &file_stat) != 0) {
    ERROR_FLOG("ConfigWatcher: File not accessible: {}", config_file_path_);
    return;
  }

  std::ifstream file(config_file_path_);
  if (!file.is_open()) {
    ERROR_FLOG("ConfigWatcher: Failed to open file: {}", config_file_path_);
    return;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string current_content = buffer.str();
  file.close();

  if (current_content == last_file_content_) {
    return;
  }

  INFO_FLOG("ConfigWatcher: Configuration file changed, reloading...");

  last_file_content_ = current_content;
  last_modify_time_ = std::chrono::steady_clock::now();

  if (callback_) {
    try {
      callback_(config_file_path_);
    } catch (const std::exception &e) {
      ERROR_FLOG("ConfigWatcher: Error in callback: {}", e.what());
    }
  }
}

} // namespace config
} // namespace nova
