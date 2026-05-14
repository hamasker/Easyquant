#pragma once

#include "trade/trade_service.h"
#include <atomic>
#include <unistd.h>

BEGIN_NOVA_NAMESPACE(trade)

class TradeServer;
class TradeDispatcherService;

class TradeServerAuxThread {
public:
  static constexpr auto LOOP_TIME_MS = 50;

  static constexpr auto LOAD_ACCOUNT_MAX_PERIOD_MS = 1 * 60 * 1000;

  static constexpr auto LOAD_ACCOUNT_MIN_PERIOD_MS = 10 * 1000;

  static constexpr auto FAST_QUERY_FUND_YUAN = 5000000;

  static constexpr auto ON_USER_COMMAND_ALL_USER = "ALL";

  TradeServerAuxThread()
      : continue_(0), msg_count_(0), sequence_(0), command_efd_(-1),
        command_listen_fd_(STDIN_FILENO) {}
  ~TradeServerAuxThread() = default;

  bool Initialize(const Config &cfg);

  bool InitStrategyUserCommand(const Config *cfg);

  void Start() { continue_ = 1; }
  void Stop();
  bool Continue() const { return continue_.load() != 0; }

  static void Run(TradeServerAuxThread *aux, TradeDispatcherService *service);

  IRET TryProcessUserCommand(TradeDispatcherService *service);

  IRET DispatcherCommand(char *msg, TradeDispatcherService *service);

private:
  void *ws_server_ = nullptr;
  bool SendMsg(const char *msg, uint32_t size);
  void OnUserMsg(uint64_t sid, const std::string &str);

  std::atomic<int32_t> continue_;
  TradeServer *server_;
  TradeDispatcherService *service_;
  uint32_t msg_count_;
  uint32_t sequence_;

  int command_efd_;
  int command_listen_fd_;
};
END_NOVA_NAMESPACE(trade)