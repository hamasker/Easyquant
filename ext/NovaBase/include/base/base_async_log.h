#pragma once

#include <cstddef>
#include <unordered_map>
#if __cplusplus < 201402L

#endif

#include <any>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unistd.h>
#include <vector>

#include "base/base_error.h"
#include "base/base_os_io.h"
#include "base/base_os_string.h"
#include "base/base_os_thread.h"
#include "base/base_os_time.h"
#include "base/base_spsc_queue.h"

#include "tpl/fmt/format.h"
#include "tpl/fmt/printf.h"

USE_NOVA_NAMESPACE(base)

template <class T>
struct fmt::formatter<T, std::enable_if_t<std::is_enum_v<T>, char>>
    : fmt::formatter<int> {
  auto format(T e, fmt::format_context &ctx) {
    return formatter<int>::format(static_cast<int>(e), ctx);
  }
};

namespace nova {
namespace log {
enum LogLevel {
  LOG_LEVEL_FATAL = 0,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_WARNING,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_TRACE,
};

static constexpr const char *LogLevelString[] = {"FATAL", "ERROR", "WARNING",
                                                 "INFO",  "DEBUG", "TRACE"};

inline LogLevel GetLogLevelFromString(const char *src) {
  if (0 == os_strcasecmp(src, "FATAL"))
    return LOG_LEVEL_FATAL;
  if (0 == os_strcasecmp(src, "ERROR"))
    return LOG_LEVEL_ERROR;
  if (0 == os_strcasecmp(src, "WARNING"))
    return LOG_LEVEL_WARNING;
  if (0 == os_strcasecmp(src, "INFO"))
    return LOG_LEVEL_INFO;
  if (0 == os_strcasecmp(src, "DEBUG"))
    return LOG_LEVEL_DEBUG;
  if (0 == os_strcasecmp(src, "TRACE"))
    return LOG_LEVEL_TRACE;
  return LOG_LEVEL_DEBUG;
}

using SerializeFunc = std::function<size_t(char *, const std::any &, size_t)>;
using DeserializeFunc = std::function<size_t(const char *, std::any &)>;

using TypeId = uint32_t;

class LoggerTypeRegistry {
private:
  std::unordered_map<TypeId, std::pair<SerializeFunc, DeserializeFunc>>
      typeFunctions;
  std::unordered_map<std::type_index, TypeId> typeidMap;
  TypeId currid = 1;

public:
  static LoggerTypeRegistry &instance() {
    static LoggerTypeRegistry registry;
    return registry;
  }

  template <typename T> void registerType() {
    if (typeidMap.find(typeid(T)) == typeidMap.end()) {
      typeidMap[typeid(T)] = currid;
      typeFunctions[currid] = std::make_pair(
          [](char *buf, const std::any &obj, size_t pos) -> size_t {
            const T &value = std::any_cast<const T &>(obj);
            if constexpr (std::is_same_v<T, std::string>) {
              size_t sz = value.size();
              memcpy(buf + pos, &sz, sizeof(sz));
              pos += sizeof(size_t);
              memcpy(buf + pos, value.data(), sz);
              pos += sz;
              return pos;
            } else {
              std::memcpy(buf + pos, &value, sizeof(T));
              pos += sizeof(T);
              return pos;
            }
          },
          [](const char *buf, std::any &obj) -> size_t {
            T value{};
            size_t pos = 0;
            if constexpr (std::is_arithmetic_v<T>) {
              memcpy(&value, buf, sizeof(T));
              pos += sizeof(T);
            } else if constexpr (std::is_same_v<T, std::string>) {
              size_t sz;
              memcpy(&sz, buf, sizeof(sz));
              pos += sizeof(size_t);
              value.assign(buf + sizeof(sz), sz);
              pos += sz;
            } else if constexpr (std::is_same_v<T, const char *>) {
              memcpy(&value, buf, sizeof(T));
              pos += sizeof(T);
            }
            obj = std::any(value);
            return pos;
          });
      currid++;
    }
  }

  template <typename T> TypeId getTypeId() const {
    auto it = typeidMap.find(typeid(T));
    return it != typeidMap.end() ? it->second : 0;
  }
  SerializeFunc getSerializeFunc(TypeId id) const {
    auto it = typeFunctions.find(id);
    return it != typeFunctions.end() ? it->second.first : nullptr;
  }

  DeserializeFunc getDeserializeFunc(TypeId id) const {
    auto it = typeFunctions.find(id);
    return it != typeFunctions.end() ? it->second.second : nullptr;
  }

  bool existId(TypeId id) {
    if (typeFunctions.find(id) != typeFunctions.end())
      return true;
    return false;
  }
};

class LoggerQueue {
public:
  using Buffer = IntrusiveSpscBuffer<1 << 20, 8>;

  struct LoggerMessageHead {
    int32_t sys_err_code;
    int32_t log_id;
    uint64_t tsc;
  };

public:
  inline LoggerMessageHead *TryPush(bool if_err, int32_t log_id,
                                    size_t msg_size) {
    // static_assert(sizeof(Buffer::Header) == 8, "");

    auto real_size = msg_size + sizeof(LoggerMessageHead);
    if UNLIKELY (real_size > std::numeric_limits<uint16_t>::max())
      return nullptr;

    auto *m = buffer_.TryPush(real_size);
    if UNLIKELY (m == nullptr)
      return nullptr;

    auto *ret = reinterpret_cast<LoggerMessageHead *>(m + 1);
    if (if_err) {
      ret->sys_err_code = errno;
      errno = 0;
    } else {
      ret->sys_err_code = 0;
    }

    ret->log_id = log_id;

    ret->tsc = rdtsc();
    return ret;
  }

  void EndPush() { return buffer_.EndPush(); }

  const LoggerMessageHead *TryPop() {
    auto *m = buffer_.TryPop();
    if (m == nullptr)
      return nullptr;
    return reinterpret_cast<const LoggerMessageHead *>(m + 1);
  }

  void EndPop() { return buffer_.EndPop(); }

private:
  Buffer buffer_{};
};

class LoggerClient;

class LoggerServer {
public:
  using MemoryBuffer = fmt::basic_memory_buffer<char, 2048>;
  using LoggerMessageHeader = LoggerQueue::LoggerMessageHead;

  using FormatContext = fmt::format_context;
  using PrintfContext = fmt::printf_context;

  template <class Context, class T> struct log_trait {
    static constexpr auto mapped_type =
        fmt::detail::mapped_type_constant<T, Context>::value;

    static constexpr auto is_cstring =
        mapped_type == fmt::detail::type::cstring_type;

    static constexpr auto is_string =
        mapped_type == fmt::detail::type::string_type;
  };

  template <class Context, size_t Idx, size_t N>
  static inline const char *
  DeserializeArgs(const char *in, fmt::basic_format_arg<Context> *args) {
    if constexpr (Idx < N) {
      TypeId id;
      memcpy(&id, in, sizeof(TypeId));
      std::any obj;
      size_t pos = LoggerTypeRegistry::instance().getDeserializeFunc(id)(
          in + sizeof(TypeId), obj);
      if (obj.type() == typeid(int)) {
        args[Idx] = fmt::detail::make_arg<Context>(std::any_cast<int &>(obj));
      } else if (obj.type() == typeid(char *)) {
        args[Idx] = fmt::detail::make_arg<Context>(std::any_cast<char *&>(obj));
      } else if (obj.type() == typeid(const char *)) {
        args[Idx] =
            fmt::detail::make_arg<Context>(std::any_cast<const char *&>(obj));
      } else if (obj.type() == typeid(double)) {
        args[Idx] =
            fmt::detail::make_arg<Context>(std::any_cast<double &>(obj));
      }
      return DeserializeArgs<Context, Idx + 1, N>(in + sizeof(TypeId) + pos,
                                                  args);
    } else
      return in;
  }

  template <class Context, size_t Idx, typename Arg, typename... Args>
  static inline typename std::enable_if<
      log_trait<Context, fmt::remove_cvref_t<Arg>>::is_cstring,
      const char *>::type
  DeserializeArgs(const char *in, fmt::basic_format_arg<Context> *args) {
    size_t size = strlen(in);
    fmt::string_view v(in, size);
    args[Idx] = fmt::detail::make_arg<Context>(v);
    return DeserializeArgs<Context, Idx + 1, Args...>(in + size + 1, args);
  }

  template <class Context, size_t Idx, typename Arg, typename... Args>
  static inline typename std::enable_if<
      !log_trait<Context, fmt::remove_cvref_t<Arg>>::is_cstring,
      const char *>::type
  DeserializeArgs(const char *in, fmt::basic_format_arg<Context> *args) {
    using ArgType = fmt::remove_cvref_t<Arg>;

    args[Idx] = fmt::detail::make_arg<Context>(*(ArgType *)in);
    ;
    return DeserializeArgs<Context, Idx + 1, Args...>(in + sizeof(ArgType),
                                                      args);
  }

public:
  using ArgsFormatFunc = void (*)(fmt::string_view, const LoggerMessageHeader *,
                                  MemoryBuffer &);

  struct LoggerRegisterInfo {
    fmt::string_view fmt_str = {};
    ArgsFormatFunc args_fmt = nullptr;
    int32_t line;
    LogLevel level;

    const char *file_name = nullptr;
  };

  struct ClientInfo {
    LoggerClient *client;
    LoggerQueue *queue;
    const LoggerQueue::LoggerMessageHead *header = nullptr;
    int32_t tid;
  };

public:
  bool Initialize(const char *log_file, LogLevel file_lvl = LOG_LEVEL_DEBUG,
                  LogLevel screen_lvl = LOG_LEVEL_DEBUG, int32_t bind_cput = -1,
                  bool is_async = true, bool is_local = false,
                  bool rotating = true);
  static LoggerServer *Instance();
  std::thread *StartThread(int32_t loop_ms = 20);

public:
  bool RegClient(LoggerClient *client);

  void Start() { continue_ = true; }
  void Stop() { continue_ = false; }

  template <class Context, int N>
  static
      typename std::enable_if<std::is_same<Context, FormatContext>::value>::type
      VFormatArgs(fmt::string_view format, const LoggerMessageHeader *hdr,
                  MemoryBuffer &out) {
    constexpr size_t num_args = N;

    std::array<fmt::basic_format_arg<Context>, num_args> args;

    DeserializeArgs<Context, 0, N>((char *)(hdr + 1), args.data());

    fmt::detail::vformat_to(
        out, format, fmt::basic_format_args<Context>(args.data(), num_args));
  }

  template <class Context, int N>
  static
      typename std::enable_if<std::is_same<Context, PrintfContext>::value>::type
      VFormatArgs(fmt::string_view format, const LoggerMessageHeader *hdr,
                  MemoryBuffer &out) {
    constexpr size_t num_args = N;

    std::array<fmt::basic_format_arg<Context>, num_args> args;

    DeserializeArgs<Context, 0, N>((char *)(hdr + 1), args.data());

    fmt::detail::vprintf(
        out, format, fmt::basic_format_args<Context>(args.data(), num_args));
  }

  int32_t LoopAll();

public:
  void RegisterLogInfo(int32_t &log_id, LogLevel level,
                       fmt::string_view fmt_str, ArgsFormatFunc fmt_func,
                       const char *file, int32_t line);

  void WriteLog(int32_t tid, MemoryBuffer &buffer, LoggerRegisterInfo &info,
                const LoggerMessageHeader *hdr);

public:
  std::thread *thread() const { return thread_; }
  LogLevel file_level() const { return file_lvl_; }
  LogLevel screen_level() const { return screen_lvl_; }
  bool rentime_async() const { return runtime_async_; }

protected:
  LoggerServer();
  ~LoggerServer() {
    while (LoopAll() > 0)
      ;
  }

  void output(LogLevel level, int ymd, const char *msg, size_t len);

private:
  LogLevel file_lvl_ = LOG_LEVEL_DEBUG;
  LogLevel screen_lvl_ = LOG_LEVEL_DEBUG;
  os_file_t file_out = OS_FILE_NULL;
  bool rotating_ = true;
  bool local_time_ = false;
  std::string file_path_{};
  int log_ymd_ = 0;
  int32_t bind_cpu_ = -1;
  std::atomic<bool> runtime_async_{false};
  std::mutex runtime_lock_;

  TSCConverter tsc_converter_;

  std::mutex out_lock_;
  MemoryBuffer buffer_{};

  std::atomic<bool> continue_{true};
  std::thread *thread_ = nullptr;

  std::mutex client_lock_;
  std::atomic<size_t> client_size_{0};
  std::vector<ClientInfo> clients_;
  std::vector<ClientInfo> bg_clients_;

  std::mutex log_reg_lock_;
  std::atomic<size_t> log_info_size_{0};
  std::vector<LoggerRegisterInfo> log_reg_info_;
  std::vector<LoggerRegisterInfo> bg_log_reg_info_;

public:
  std::vector<ClientInfo> GetAllAsyncClientInfo() {
    std::lock_guard<std::mutex> _{client_lock_};
    std::vector<ClientInfo> tmp = bg_clients_;
    return tmp;
  }
};

class NOVA_ALIGNED_CACHE_LINE LoggerClient {
public:
  LoggerClient();
  ~LoggerClient() = default;

public:
  using PrintfContext = LoggerServer::PrintfContext;
  using FormatContext = LoggerServer::FormatContext;
  using MemoryBuffer = LoggerServer::MemoryBuffer;

  template <class T>
  using log_trait = LoggerServer::log_trait<PrintfContext, T>;

public:
  template <size_t CstringIdx>
  static inline constexpr size_t sizeof_args(size_t *cstringSize) {
    return 0;
  }

  template <size_t CstringIdx, typename Arg, typename... Args>
  static inline
      typename std::enable_if<log_trait<Arg>::is_cstring, size_t>::type
      sizeof_args(size_t *cstringSize, const Arg &arg, const Args &...args) {
    size_t len = strlen(arg) + 1;
    cstringSize[CstringIdx] = len;
    return len + sizeof_args<CstringIdx + 1>(cstringSize, args...);
  }

  template <size_t CstringIdx, typename Arg, typename... Args>
  static inline
      typename std::enable_if<!log_trait<Arg>::is_cstring, size_t>::type
      sizeof_args(size_t *cstringSize, const Arg &arg, const Args &...args) {
    return sizeof(Arg) + sizeof_args<CstringIdx>(cstringSize, args...);
  }

public:
  static LoggerClient *ThreadInstance();

public:
  template <typename T> size_t Write(char *buf, const T &value) {
    TypeId id = LoggerTypeRegistry::instance().getTypeId<T>();
    memcpy(buf, &id, sizeof(TypeId));
    return LoggerTypeRegistry::instance().getSerializeFunc(id)(buf, value,
                                                               sizeof(TypeId));
  }

  template <class T, class Enable = void> struct DecayArg {
    using type = typename std::decay<T>::type;

    static constexpr type decay(T &&t) noexcept { return static_cast<type>(t); }
  };

  template <class T>
  struct DecayArg<T, typename std::enable_if<std::is_enum<
                         typename std::decay<T>::type>::value>::type> {
    static constexpr int64_t decay(T &&t) noexcept { return int64_t(t); }
  };

  template <class T>
  struct DecayArg<T, typename std::enable_if<log_trait<
                         typename std::decay<T>::type>::is_string>::type> {
    static constexpr const char *decay(T &&t) noexcept { return t.c_str(); }
  };

  template <class Context, class S, class... Args>
  inline void _DoLog(S fmt, int32_t &log_id, LogLevel level, const char *file,
                     int line, Args... args) {
    constexpr int N = sizeof...(Args);

    if UNLIKELY (log_id < 0) {
      if constexpr (std::is_same<Context, FormatContext>::value) {
        fmt::detail::check_format_string<Args...>(fmt);
      }

      auto *fmt_func = LoggerServer::VFormatArgs<Context, N>;

      LoggerServer::Instance()->RegisterLogInfo(log_id, level, fmt, fmt_func,
                                                file, line);
      LoggerTypeRegistry::instance().registerType<int>();
      LoggerTypeRegistry::instance().registerType<float>();
      LoggerTypeRegistry::instance().registerType<double>();
      LoggerTypeRegistry::instance().registerType<const char *>();
    }
    constexpr auto cstring_count =
        fmt::detail::count<log_trait<Args>::is_cstring...>();
    size_t cstring_size[cstring_count + 1];

    auto tuple_size =
        sizeof_args<0>(cstring_size, args...) + N * sizeof(TypeId);
    LoggerQueue::LoggerMessageHead *hdr = nullptr;
    hdr = fmt_que_.TryPush(level < LOG_LEVEL_WARNING, log_id, tuple_size);
    if (!hdr)
      return;

    size_t pos = 0;
    ((pos += Write((char *)(hdr + 1) + pos, args)), ...);

    fmt_que_.EndPush();

    if UNLIKELY (!runtime_async_) {
      LoggerServer::Instance()->LoopAll();
    }
  }

  template <class... Args>
  inline void _Log(fmt::string_view fmt, int32_t &i, LogLevel level,
                   const char *file, int line, Args &&...args) {
    return _DoLog<PrintfContext>(
        fmt, i, level, file, line,
        DecayArg<Args>::decay(std::forward<Args>(args))...);
  }

  template <class... Args>
  inline void _FLog(fmt::format_string<Args...> fmt, int32_t &i, LogLevel level,
                    const char *file, int line, Args &&...args) {
    return _DoLog<FormatContext>(
        fmt, i, level, file, line,
        DecayArg<Args>::decay(std::forward<Args>(args))...);
  }

  template <class S, class... Args>
  inline void _FLogOld(const S fmt, int32_t &i, LogLevel level,
                       const char *file, int line, Args &&...args) {
    return _DoLog<FormatContext>(
        fmt, i, level, file, line,
        DecayArg<Args>::decay(std::forward<Args>(args))...);
  }

  template <class Context, class S, class... Args>
  inline void _DoSyncLog(S fmt, int32_t &log_id, LogLevel level,
                         const char *file, int line, Args &&...args) {
    // if constexpr (std::is_same<Context, FormatContext>::value) {
    //   fmt::detail::check_format_string<Args...>(fmt);
    // }
    // LoggerServer::LoggerRegisterInfo log_info;
    // log_info.fmt_str = fmt;
    // log_info.args_fmt = LoggerServer::VFormatArgs<Context, Args...>;
    // log_info.file_name = get_file_name(file);
    // log_info.line = line;
    // log_info.level = level;

    // constexpr auto cstring_count =
    //     fmt::detail::count<log_trait<Args>::is_cstring...>();
    // size_t cstring_size[cstring_count + 1];

    // auto tuple_size = sizeof_args<0>(cstring_size, args...);
    // auto buf_size = sizeof(LoggerQueue::LoggerMessageHead) + tuple_size;
    // if (buf_size > std::numeric_limits<uint16_t>::max()) {
    return;
    // }

    // char buf[buf_size];
    // auto *hdr = reinterpret_cast<LoggerQueue::LoggerMessageHead *>(buf);
    // {
    //   auto if_err = level <= LOG_LEVEL_WARNING;
    //   if (if_err) {
    //     hdr->sys_err_code = errno;
    //     errno = 0;
    //   } else {
    //     hdr->sys_err_code = 0;
    //   }
    //   hdr->log_id = log_id;
    //   hdr->tsc = rdtsc();
    // }

    // SerializeArgs<0>(cstring_size, (char *)(hdr + 1),
    //                  std::forward<Args>(args)...);

    // MemoryBuffer mem_buf;

    // LoggerServer::Instance()->WriteLog(tid(), mem_buf, log_info, hdr);
  }

  template <class... Args>
  inline void _SyncLog(fmt::string_view fmt, int32_t &i, LogLevel level,
                       const char *file, int line, Args &&...args) {
    return _DoSyncLog<PrintfContext>(
        fmt, i, level, file, line,
        DecayArg<Args>::decay(std::forward<Args>(args))...);
  }

  template <class... Args>
  inline void _SyncFLog(fmt::format_string<Args...> fmt, int32_t &i,
                        LogLevel level, const char *file, int line,
                        Args &&...args) {
    return _DoSyncLog<FormatContext>(
        fmt, i, level, file, line,
        DecayArg<Args>::decay(std::forward<Args>(args))...);
  }

public:
  LogLevel max_level() const { return max_level_; }
  int32_t tid() const { return tid_; }
  LoggerQueue *queue() { return &fmt_que_; }
  void set_max_level(LogLevel lv) { max_level_ = lv; }

  bool running() const { return continue_; }

public:
  static thread_local struct TreadScope {
    ~TreadScope() {
      auto *client = LoggerClient::ThreadInstance();
      client->continue_ = false;
    }
    void Foo() {}
  } thread_scope_;

private:
  LoggerQueue fmt_que_;
  LogLevel max_level_;
  int32_t tid_;
  bool runtime_async_ = true;
  bool continue_ = true;
};

}; // namespace log
}; // namespace nova

#define _CALL_ASYNC_LOG(_m_level, _m_fmt, ...)                                 \
  do {                                                                         \
    static int32_t _log_id = -1;                                               \
    auto *_client = ::nova::log::LoggerClient::ThreadInstance();               \
    sizeof(printf(_m_fmt, ##__VA_ARGS__));                                     \
    if ((_m_level) > _client->max_level())                                     \
      break;                                                                   \
    _client->_Log((_m_fmt), _log_id, (_m_level), __FILE__, __LINE__,           \
                  ##__VA_ARGS__);                                              \
  } while (0)

#define _CALL_ASYNC_FLOG(_m_level, _m_fmt, ...)                                \
  do {                                                                         \
    static int32_t _log_id = -1;                                               \
    auto *_client = ::nova::log::LoggerClient::ThreadInstance();               \
    if ((_m_level) > _client->max_level())                                     \
      break;                                                                   \
    _client->_FLog((_m_fmt), _log_id, (_m_level), __FILE__, __LINE__,          \
                   ##__VA_ARGS__);                                             \
  } while (0)

#define _CALL_ASYNC_FLOG_OLD(_m_level, _m_fmt, ...)                            \
  do {                                                                         \
    static int32_t _log_id = -1;                                               \
    auto *_client = ::nova::log::LoggerClient::ThreadInstance();               \
    if ((_m_level) > _client->max_level())                                     \
      break;                                                                   \
    _client->_FLogOld((_m_fmt), _log_id, (_m_level), __FILE__, __LINE__,       \
                      ##__VA_ARGS__);                                          \
  } while (0)

#define _CALL_SYNC_LOG(_m_level, _m_fmt, ...)                                  \
  do {                                                                         \
    static int32_t _log_id = -1;                                               \
    auto *_client = ::nova::log::LoggerClient::ThreadInstance();               \
    sizeof(printf(_m_fmt, ##__VA_ARGS__));                                     \
    if ((_m_level) > _client->max_level())                                     \
      break;                                                                   \
    _client->_SyncLog((_m_fmt), _log_id, (_m_level), __FILE__, __LINE__,       \
                      ##__VA_ARGS__);                                          \
  } while (0)

#define _CALL_SYNC_FLOG(_m_level, _m_fmt, ...)                                 \
  do {                                                                         \
    static int32_t _log_id = -1;                                               \
    auto *_client = ::nova::log::LoggerClient::ThreadInstance();               \
    if ((_m_level) > _client->max_level())                                     \
      break;                                                                   \
    _client->_SyncFLog((_m_fmt), _log_id, (_m_level), __FILE__, __LINE__,      \
                       ##__VA_ARGS__);                                         \
  } while (0)

#define SYNC_FATAL_LOG(_macro_fmt, ...)                                        \
  _CALL_SYNC_LOG(::nova::log::LOG_LEVEL_FATAL, _macro_fmt, ##__VA_ARGS__)
#define SYNC_ERROR_LOG(_macro_fmt, ...)                                        \
  _CALL_SYNC_LOG(::nova::log::LOG_LEVEL_ERROR, _macro_fmt, ##__VA_ARGS__)
#define SYNC_WARNING_LOG(_macro_fmt, ...)                                      \
  _CALL_SYNC_LOG(::nova::log::LOG_LEVEL_WARNING, _macro_fmt, ##__VA_ARGS__)
#define SYNC_INFO_LOG(_macro_fmt, ...)                                         \
  _CALL_SYNC_LOG(::nova::log::LOG_LEVEL_INFO, _macro_fmt, ##__VA_ARGS__)
#define SYNC_DEBUG_LOG(_macro_fmt, ...)                                        \
  _CALL_SYNC_LOG(::nova::log::LOG_LEVEL_DEBUG, _macro_fmt, ##__VA_ARGS__)
#define SYNC_TRACE_LOG(_macro_fmt, ...)                                        \
  _CALL_SYNC_LOG(::nova::log::LOG_LEVEL_TRACE, _macro_fmt, ##__VA_ARGS__)

#define SYNC_FATAL_FLOG(_macro_fmt, ...)                                       \
  _CALL_SYNC_FLOG(::nova::log::LOG_LEVEL_FATAL, _macro_fmt, ##__VA_ARGS__)
#define SYNC_ERROR_FLOG(_macro_fmt, ...)                                       \
  _CALL_SYNC_FLOG(::nova::log::LOG_LEVEL_ERROR, _macro_fmt, ##__VA_ARGS__)
#define SYNC_WARNING_FLOG(_macro_fmt, ...)                                     \
  _CALL_SYNC_FLOG(::nova::log::LOG_LEVEL_WARNING, _macro_fmt, ##__VA_ARGS__)
#define SYNC_INFO_FLOG(_macro_fmt, ...)                                        \
  _CALL_SYNC_FLOG(::nova::log::LOG_LEVEL_INFO, _macro_fmt, ##__VA_ARGS__)
#define SYNC_DEBUG_FLOG(_macro_fmt, ...)                                       \
  _CALL_SYNC_FLOG(::nova::log::LOG_LEVEL_DEBUG, _macro_fmt, ##__VA_ARGS__)
#define SYNC_TRACE_FLOG(_macro_fmt, ...)                                       \
  _CALL_SYNC_FLOG(::nova::log::LOG_LEVEL_TRACE, _macro_fmt, ##__VA_ARGS__)

#ifdef _SYNC_LOG

#else

#ifndef NOVA_BASE_DISABLE_LOGGER
#define FATAL_LOG(_macro_fmt, ...)                                             \
  _CALL_ASYNC_LOG(::nova::log::LOG_LEVEL_FATAL, _macro_fmt, ##__VA_ARGS__)
#define ERROR_LOG(_macro_fmt, ...)                                             \
  _CALL_ASYNC_LOG(::nova::log::LOG_LEVEL_ERROR, _macro_fmt, ##__VA_ARGS__)
#define WARNING_LOG(_macro_fmt, ...)                                           \
  _CALL_ASYNC_LOG(::nova::log::LOG_LEVEL_WARNING, _macro_fmt, ##__VA_ARGS__)
#define INFO_LOG(_macro_fmt, ...)                                              \
  _CALL_ASYNC_LOG(::nova::log::LOG_LEVEL_INFO, _macro_fmt, ##__VA_ARGS__)
#define DEBUG_LOG(_macro_fmt, ...)                                             \
  _CALL_ASYNC_LOG(::nova::log::LOG_LEVEL_DEBUG, _macro_fmt, ##__VA_ARGS__)
#define TRACE_LOG(_macro_fmt, ...)                                             \
  _CALL_ASYNC_LOG(::nova::log::LOG_LEVEL_TRACE, _macro_fmt, ##__VA_ARGS__)

#define FATAL_FLOG(_macro_fmt, ...)                                            \
  _CALL_ASYNC_FLOG(::nova::log::LOG_LEVEL_FATAL, _macro_fmt, ##__VA_ARGS__)
#define ERROR_FLOG(_macro_fmt, ...)                                            \
  _CALL_ASYNC_FLOG(::nova::log::LOG_LEVEL_ERROR, _macro_fmt, ##__VA_ARGS__)
#define WARNING_FLOG(_macro_fmt, ...)                                          \
  _CALL_ASYNC_FLOG(::nova::log::LOG_LEVEL_WARNING, _macro_fmt, ##__VA_ARGS__)
#define INFO_FLOG(_macro_fmt, ...)                                             \
  _CALL_ASYNC_FLOG(::nova::log::LOG_LEVEL_INFO, _macro_fmt, ##__VA_ARGS__)
#define DEBUG_FLOG(_macro_fmt, ...)                                            \
  _CALL_ASYNC_FLOG(::nova::log::LOG_LEVEL_DEBUG, _macro_fmt, ##__VA_ARGS__)
#define TRACE_FLOG(_macro_fmt, ...)                                            \
  _CALL_ASYNC_FLOG(::nova::log::LOG_LEVEL_TRACE, _macro_fmt, ##__VA_ARGS__)

#else
// ============================================================
// Lightweight logging when NovaBase async logger is disabled
// Timestamped output to stderr and/or file, with optional async
// ============================================================
namespace nova { namespace log {

inline int g_screen_level = LOG_LEVEL_INFO;
inline int g_file_level = LOG_LEVEL_DEBUG;
inline FILE *g_log_file = nullptr;
inline bool g_async = false;
inline std::atomic<bool> g_writer_running{false};
inline std::thread g_writer_thread;
inline std::mutex g_queue_lock;
inline std::vector<std::string> g_queue;

inline void SetLogFile(const char *path) {
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
          fputs(s.c_str(), stderr);
        }
        batch.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
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

}} // namespace nova::log

namespace { struct _NovaLogAsyncGuard { ~_NovaLogAsyncGuard() { nova::log::StopAsync(); } } _nova_log_async_guard; }

#define _LIGHTWEIGHT_FLOG(_m_level, _tag, _fmt, ...)                           \
  do {                                                                          \
    int _cr_lv = (int)(_m_level);                                                \
    bool _cr_to_scrn = (_cr_lv <= ::nova::log::g_screen_level);                  \
    bool _cr_to_file = (::nova::log::g_log_file &&                               \
                        _cr_lv <= ::nova::log::g_file_level);                    \
    if (!_cr_to_scrn && !_cr_to_file) break;                                     \
    auto _cr_now = std::chrono::system_clock::now();                             \
    auto _cr_tt = std::chrono::system_clock::to_time_t(_cr_now);                 \
    auto _cr_us = std::chrono::duration_cast<std::chrono::microseconds>(         \
        _cr_now.time_since_epoch()).count() % 1000000;                           \
    char _cr_ts[32]; (void)strftime(_cr_ts, sizeof(_cr_ts), "%H:%M:%S",          \
                                 localtime(&_cr_tt));                             \
    static const char *_cr_lv_str[] = {"F","E","W","I","D","T"};                 \
    auto _cr_hdr = fmt::format("[{}.{:06d}] [{}] [{}] ", _cr_ts, (int)_cr_us,    \
                           _cr_lv_str[_cr_lv], getpid());                         \
    auto _cr_msg = fmt::format("{}{}\n", _cr_hdr,                                 \
                               fmt::format(_tag _fmt, ##__VA_ARGS__));            \
    if (::nova::log::g_async) {                                                   \
      if (_cr_to_scrn) fputs(_cr_msg.c_str(), stderr);                            \
      if (_cr_to_file) {                                                          \
        std::lock_guard<std::mutex> _cr_lk(::nova::log::g_queue_lock);            \
        ::nova::log::g_queue.push_back(std::move(_cr_msg));                       \
      }                                                                           \
    } else {                                                                      \
      if (_cr_to_scrn) fputs(_cr_msg.c_str(), stderr);                            \
      if (_cr_to_file) fputs(_cr_msg.c_str(), ::nova::log::g_log_file);           \
    }                                                                             \
  } while (0)

#define _LIGHTWEIGHT_LOG(_m_level, _tag, _fmt, ...)                            \
  do {                                                                          \
    int _cr_lv = (int)(_m_level);                                                \
    bool _cr_to_scrn = (_cr_lv <= ::nova::log::g_screen_level);                  \
    bool _cr_to_file = (::nova::log::g_log_file &&                               \
                        _cr_lv <= ::nova::log::g_file_level);                    \
    if (!_cr_to_scrn && !_cr_to_file) break;                                     \
    auto _cr_now = std::chrono::system_clock::now();                             \
    auto _cr_tt = std::chrono::system_clock::to_time_t(_cr_now);                 \
    auto _cr_us = std::chrono::duration_cast<std::chrono::microseconds>(         \
        _cr_now.time_since_epoch()).count() % 1000000;                           \
    char _cr_ts[32]; (void)strftime(_cr_ts, sizeof(_cr_ts), "%H:%M:%S",          \
                                 localtime(&_cr_tt));                             \
    static const char *_cr_lv_str[] = {"F","E","W","I","D","T"};                 \
    auto _cr_hdr = fmt::format("[{}.{:06d}] [{}] [{}] ", _cr_ts, (int)_cr_us,    \
                           _cr_lv_str[_cr_lv], getpid());                         \
    auto _cr_msg = _cr_hdr + fmt::format(_tag _fmt, ##__VA_ARGS__);              \
    if (_cr_to_scrn) fprintf(stderr, "%s\n", _cr_msg.c_str());                   \
    if (_cr_to_file) fprintf(::nova::log::g_log_file, "%s\n", _cr_msg.c_str());  \
  } while (0)

#define FATAL_LOG(_fmt, ...)   _LIGHTWEIGHT_LOG(::nova::log::LOG_LEVEL_FATAL,   "[FATAL] ", _fmt, ##__VA_ARGS__)
#define ERROR_LOG(_fmt, ...)   _LIGHTWEIGHT_LOG(::nova::log::LOG_LEVEL_ERROR,   "[ERROR] ", _fmt, ##__VA_ARGS__)
#define WARNING_LOG(_fmt, ...) _LIGHTWEIGHT_LOG(::nova::log::LOG_LEVEL_WARNING, "[WARN] ",  _fmt, ##__VA_ARGS__)
#define INFO_LOG(_fmt, ...)    _LIGHTWEIGHT_LOG(::nova::log::LOG_LEVEL_INFO,    "[INFO] ",  _fmt, ##__VA_ARGS__)
#define DEBUG_LOG(_fmt, ...)   _LIGHTWEIGHT_LOG(::nova::log::LOG_LEVEL_DEBUG,   "[DEBUG] ", _fmt, ##__VA_ARGS__)
#define TRACE_LOG(_fmt, ...)   _LIGHTWEIGHT_LOG(::nova::log::LOG_LEVEL_TRACE,   "[TRACE] ", _fmt, ##__VA_ARGS__)

#define FATAL_FLOG(_fmt, ...)   _LIGHTWEIGHT_FLOG(::nova::log::LOG_LEVEL_FATAL,   "[FATAL] ", _fmt, ##__VA_ARGS__)
#define ERROR_FLOG(_fmt, ...)   _LIGHTWEIGHT_FLOG(::nova::log::LOG_LEVEL_ERROR,   "[ERROR] ", _fmt, ##__VA_ARGS__)
#define WARNING_FLOG(_fmt, ...) _LIGHTWEIGHT_FLOG(::nova::log::LOG_LEVEL_WARNING, "[WARN] ",  _fmt, ##__VA_ARGS__)
#define INFO_FLOG(_fmt, ...)    _LIGHTWEIGHT_FLOG(::nova::log::LOG_LEVEL_INFO,    "",         _fmt, ##__VA_ARGS__)
#define DEBUG_FLOG(_fmt, ...)   _LIGHTWEIGHT_FLOG(::nova::log::LOG_LEVEL_DEBUG,   "[DEBUG] ", _fmt, ##__VA_ARGS__)
#define TRACE_FLOG(_fmt, ...)   _LIGHTWEIGHT_FLOG(::nova::log::LOG_LEVEL_TRACE,   "[TRACE] ", _fmt, ##__VA_ARGS__)
#endif

#endif

#ifndef CHEKC
#define CHEKC(condition)                                                       \
  if (!(condition))                                                            \
    FATAL_LOG("Check failed: " #condition);
#endif

#ifndef CHEKC_EQ
#define CHEKC_EQ(a, b) CHECK((a) == (b))
#endif

#ifndef CHEKC_NE
#define CHEKC_NE(a, b) CHECK((a) == (b))
#endif

#ifndef CHEKC_GE
#define CHEKC_GE(a, b) CHECK((a) == (b))
#endif

#ifndef CHEKC_LE
#define CHEKC_LE(a, b) CHECK((a) == (b))
#endif

#ifndef CHEKC_GT
#define CHEKC_GT(a, b) CHECK((a) == (b))
#endif

#ifndef CHEKC_LT
#define CHEKC_LT(a, b) CHECK((a) == (b))
#endif

#ifndef CHEKC_NOTNULL
#define CHEKC_NOTNULL(pointer)                                                 \
  if ((pointer) == nullptr)                                                    \
    FATAL_LOG(#pointer "Can't be NULL");
#endif