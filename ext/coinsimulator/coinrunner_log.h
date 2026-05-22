#pragma once
// coinrunner 日志系统
// NOVA_BASE_DISABLE_LOGGER 下自定义日志宏
// 支持 screen_level / file_level / async_log

#include <atomic>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "tpl/fmt/format.h"

namespace coinrunner {

enum LogLevel { FATAL = 0, ERROR = 1, WARNING = 2, INFO = 3, DEBUG = 4, TRACE = 5 };

inline int g_screen_level = INFO;
inline int g_file_level = DEBUG;
inline FILE *g_log_file = nullptr;
inline bool g_async = false;
inline std::atomic<bool> g_writer_running{false};
inline std::thread g_writer_thread;
inline std::mutex g_queue_lock;
inline std::vector<std::string> g_queue;

inline void SetLogFile(const char *path) {
  // 自动创建父目录
  std::string p(path);
  auto slash = p.rfind('/');
  if (slash != std::string::npos) {
    std::string dir = p.substr(0, slash);
    mkdir(dir.c_str(), 0755);
  }
  if (g_log_file) fclose(g_log_file);
  g_log_file = fopen(path, "a");
  if (g_log_file) setvbuf(g_log_file, nullptr, _IOLBF, 0);
}

inline void SetScreenLevel(int lv) { g_screen_level = lv; }
inline void SetFileLevel(int lv) { g_file_level = lv; }

inline void SetAsync(bool on) {
  if (on && !g_async) {
    g_async = true;
    g_writer_running = true;
    g_writer_thread = std::thread([] {
      std::vector<std::string> batch;
      while (g_writer_running) {
        {
          std::lock_guard<std::mutex> lk(g_queue_lock);
          std::swap(batch, g_queue);
        }
        for (auto &s : batch) {
          fputs(s.c_str(), g_log_file ? g_log_file : stderr);
          fputs(s.c_str(), stderr); // screen also
        }
        batch.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      // flush remaining
      std::lock_guard<std::mutex> lk(g_queue_lock);
      for (auto &s : g_queue) {
        fputs(s.c_str(), g_log_file ? g_log_file : stderr);
        fputs(s.c_str(), stderr);
      }
      g_queue.clear();
    });
  }
}

inline void StopAsync() {
  if (g_async) {
    g_writer_running = false;
    if (g_writer_thread.joinable()) g_writer_thread.join();
    g_async = false;
  }
}

inline int LevelFromString(const char *s) {
  if (!s) return INFO;
  switch (s[0]) {
  case 'T': case 't': return TRACE;
  case 'D': case 'd': return DEBUG;
  case 'I': case 'i': return INFO;
  case 'W': case 'w': return WARNING;
  case 'E': case 'e': return ERROR;
  case 'F': case 'f': return FATAL;
  default: return INFO;
  }
}

} // namespace coinrunner

// 自动在 main 结束时调用 StopAsync
namespace { struct _AsyncGuard { ~_AsyncGuard() { coinrunner::StopAsync(); } } _async_guard; }

#ifdef NOVA_BASE_DISABLE_LOGGER

#define _COINRUNNER_LOG(_level, _tag, _fmt, ...)                               \
  do {                                                                          \
    int lv = (int)coinrunner::_level;                                           \
    bool to_scrn = (lv <= coinrunner::g_screen_level);                          \
    bool to_file = (coinrunner::g_log_file && lv <= coinrunner::g_file_level);  \
    if (!to_scrn && !to_file) break;                                            \
    auto now = std::chrono::system_clock::now();                                \
    auto tt = std::chrono::system_clock::to_time_t(now);                        \
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(             \
        now.time_since_epoch()).count() % 1000000;                              \
    char ts[32]; (void)strftime(ts, sizeof(ts), "%H:%M:%S",                     \
                                 localtime(&tt));                               \
    static const char *lv_str[] = {"F","E","W","I","D","T"};                    \
    auto hdr = fmt::format("[{}.{:06d}] [{}] [{}] ", ts, (int)us,               \
                           lv_str[lv], getpid());                               \
    auto msg = fmt::format("{}{}\n", hdr, fmt::format(_tag _fmt, ##__VA_ARGS__));\
    if (coinrunner::g_async) {                                                  \
      if (to_scrn) fputs(msg.c_str(), stderr);                                  \
      if (to_file) { std::lock_guard<std::mutex> lk(coinrunner::g_queue_lock);  \
                     coinrunner::g_queue.push_back(std::move(msg)); }            \
    } else {                                                                     \
      if (to_scrn) fputs(msg.c_str(), stderr);                                   \
      if (to_file) fputs(msg.c_str(), coinrunner::g_log_file);                   \
    }                                                                            \
  } while (0)

#undef FATAL_FLOG
#undef ERROR_FLOG
#undef WARNING_FLOG
#undef INFO_FLOG
#undef DEBUG_FLOG
#undef TRACE_FLOG

#define FATAL_FLOG(_fmt, ...)   _COINRUNNER_LOG(FATAL,   "[FATAL] ", _fmt, ##__VA_ARGS__)
#define ERROR_FLOG(_fmt, ...)   _COINRUNNER_LOG(ERROR,   "[ERROR] ", _fmt, ##__VA_ARGS__)
#define WARNING_FLOG(_fmt, ...) _COINRUNNER_LOG(WARNING, "[WARN] ",  _fmt, ##__VA_ARGS__)
#define INFO_FLOG(_fmt, ...)    _COINRUNNER_LOG(INFO,    "",         _fmt, ##__VA_ARGS__)
#define DEBUG_FLOG(_fmt, ...)   _COINRUNNER_LOG(DEBUG,   "[DEBUG] ", _fmt, ##__VA_ARGS__)
#define TRACE_FLOG(_fmt, ...)   _COINRUNNER_LOG(TRACE,   "[TRACE] ", _fmt, ##__VA_ARGS__)

// printf-style macros — 同 FLOG 格式, 带头部时间戳
#define _COINRUNNER_LOGF(_level, _tag, _fmt, ...)                              \
  do {                                                                          \
    int lv = (int)coinrunner::_level;                                           \
    bool to_scrn = (lv <= coinrunner::g_screen_level);                          \
    bool to_file = (coinrunner::g_log_file && lv <= coinrunner::g_file_level);  \
    if (!to_scrn && !to_file) break;                                            \
    auto now = std::chrono::system_clock::now();                                \
    auto tt = std::chrono::system_clock::to_time_t(now);                        \
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(             \
        now.time_since_epoch()).count() % 1000000;                              \
    char ts[32]; (void)strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&tt));    \
    static const char *lv_str[] = {"F","E","W","I","D","T"};                    \
    auto hdr = fmt::format("[{}.{:06d}] [{}] [{}] ", ts, (int)us,               \
                           lv_str[lv], getpid());                               \
    auto msg = hdr + fmt::format(_tag _fmt, ##__VA_ARGS__);                     \
    if (to_scrn) fprintf(stderr, "%s\n", msg.c_str());                          \
    if (to_file) fprintf(coinrunner::g_log_file, "%s\n", msg.c_str());          \
  } while (0)

#undef FATAL_LOG
#undef ERROR_LOG
#undef WARNING_LOG
#undef INFO_LOG
#undef DEBUG_LOG
#undef TRACE_LOG

#define FATAL_LOG(_fmt, ...)   _COINRUNNER_LOGF(FATAL,   "[FATAL] ", _fmt, ##__VA_ARGS__)
#define ERROR_LOG(_fmt, ...)   _COINRUNNER_LOGF(ERROR,   "[ERROR] ", _fmt, ##__VA_ARGS__)
#define WARNING_LOG(_fmt, ...) _COINRUNNER_LOGF(WARNING, "[WARN] ",  _fmt, ##__VA_ARGS__)
#define INFO_LOG(_fmt, ...)    _COINRUNNER_LOGF(INFO,    "[INFO] ",  _fmt, ##__VA_ARGS__)
#define DEBUG_LOG(_fmt, ...)   _COINRUNNER_LOGF(DEBUG,   "[DEBUG] ", _fmt, ##__VA_ARGS__)
#define TRACE_LOG(_fmt, ...)   _COINRUNNER_LOGF(TRACE,   "[TRACE] ", _fmt, ##__VA_ARGS__)

#endif // NOVA_BASE_DISABLE_LOGGER
