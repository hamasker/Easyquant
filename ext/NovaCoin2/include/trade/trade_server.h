#pragma once

#include <functional>

#include <quote/quote_util.h>

#include "base/base_config.h"
#include "base/base_server.h"

#include "trade/trade_server_aux.h"
#include "trade/trade_service.h"

USE_NOVA_NAMESPACE(base)

BEGIN_NOVA_NAMESPACE(trade)

class TradeServer : public Server {
public:
  TradeServer();
  ~TradeServer() override = default;

  bool Initialize() override;

  void Destroy() override;

  bool Run() override;

  void Stop();

  void OnShowVersion() override;

  template <class T> void SetRegisterStrategy(T &&func) {
    register_strategy_ = func;
  }

public:
  TradeDispatcherService *service() { return service_; }
  virtual TradeDispatcherService *CreateService();

protected:
  static void TradeSignalHandler(int sig);

private:
  TradeDispatcherService *service_;
  TradeServerAuxThread *aux_;
  std::function<bool(TradeServer *, TradeDispatcherService *)>
      register_strategy_;
};

inline TradeDispatcherService *GetTradeService() {
  auto *server = GetServer();
  if (server == nullptr || server->server_type() != SERVER_TYPE_TRADE) {
    return nullptr;
  }

  auto *ret = static_cast<TradeServer *>(server)->service();

  return ret;
}

END_NOVA_NAMESPACE(trade)