#pragma once

#include "base/base_async_log.h"
#include "base/base_config.h"
#include "base/base_memory_pool.h"
#include "base/base_os_thread.h"
#include "base/base_recv_manager.h"

#include <map>
#include <vector>

BEGIN_NOVA_NAMESPACE(base)

class Server {
public:
  static constexpr auto DEFAULT_FILE_LOG_LVL = nova::log::LOG_LEVEL_DEBUG;
  static constexpr auto DEFAULT_SCREEN_LOG_LVL = nova::log::LOG_LEVEL_DEBUG;
  static constexpr auto DEFAULT_LOG_FILE_PATH = "./nova.log";

  using sig_handler_t = void (*)(int);

  Server() {}
  virtual ~Server() = default;

  bool StartService(int argc, char **argv, uint32_t exit_sleep_ms = 0,
                    bool is_async_log = true);
  bool AddThread(NovaThread *thd);
  void JoinThreads();

public:
  virtual bool Initialize();
  virtual void Destroy();

  virtual bool Run() = 0;

  virtual void OnShowHelp();
  virtual void OnShowVersion();

public:
  BlockPool *mem_block_pool() const { return block_pool_; }
  BlockObjectAllocator *mem_obj_pool() { return &block_obj_pool_; }
  const Config *config() const { return &config_; }
  NovaThreadManager *thread_manager() { return &thd_mgr_; }
  nova::log::LoggerServer *logger() const;

  void set_server_info(server_info_t svr_info) { server_info_ = svr_info; }
  server_info_t server_info() const { return server_info_; }
  ServerType server_type() const { return server_info_.type; }
  bool is_daemon() const { return daemon_; }

public:
  bool AddUserPass(const char *user, const char *pwd, uid_t uid,
                   bool hashed = true);
  bool AddRecvService(uint16_t code, RecvFunc cb);
  bool StartRecv();

protected:
  virtual void SignalHandler(int sig);
  virtual void FailureHandler(int sig);
  virtual bool SetSignalHandler();

  bool parse_args(int argc, char **argv);
  virtual bool ParseConfig(Config::CONFIG_SOURCE tp, const char *cfg) {
    return false;
  }

private:
  bool init_block_pool();
  bool init_logger();
  bool init_daemon();
  bool init_recv();

  static void signal_handler(int sig);
  static void failure_handler(int sig);

protected:
  server_info_t server_info_;
  BlockPool *block_pool_;
  BlockObjectAllocator block_obj_pool_;
  NovaThreadManager thd_mgr_;
  Config config_;
  bool daemon_;
  bool is_async_log_;
  bool is_local_time_;

  std::map<int, sig_handler_t> sig_old_handler_;

  NovaRecvManager *recv_ = nullptr;
};

#define NOVA_SERVERIMPLEMENT(_macro_fmt)                                       \
  int main(int argc, char **argv) {                                            \
    auto *server = new _macro_server{};                                        \
    {                                                                          \
      auto exe_v = str_to_version(NOVA_VERSION);                               \
      auto lib_v = server->server_info().version;                              \
      if (exe_v.u32 != lib_v.u32) {                                            \
        auto exe_str = version_to_str(exe_v);                                  \
        auto lib_str = version_to_str(lib_v);                                  \
        fprintf(stderr, "Version mismatch: %s != %s\n", exe_str.c_str(),       \
                lib_str.c_str());                                              \
        return EXIT_FAILURE;                                                   \
      }                                                                        \
    }                                                                          \
    if (!server->StartService(argc, argv))                                     \
      return 1;                                                                \
    return 0;                                                                  \
  }

Server *GetServer();

END_NOVA_NAMESPACE(base)