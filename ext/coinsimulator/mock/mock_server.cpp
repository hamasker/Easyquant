#include "mock/mock_server.h"
#include "mock/mock_trade_service.h"
#include "mock/mock_trade_engine.h"

#include "base/base_log.h"

BEGIN_NOVA_NAMESPACE(trade)

namespace {

TradeServer *g_mock_server = nullptr;

} // namespace

// ========== TradeServer 方法实现 ==========

TradeServer::TradeServer() {
  server_info_.type = SERVER_TYPE_TRADE;
  service_ = nullptr;
  aux_ = nullptr;
}

bool TradeServer::Initialize() {
  service_ = CreateService();
  if (!service_) return false;

  // TradeDispatcherService::Initialize() 会处理 pending engines
  service_->Initialize(*config());

  INFO_LOG("[TradeServer] Initialized with mock service");
  return true;
}

void TradeServer::Destroy() {
  if (service_) {
    service_->Destroy();
  }
}

bool TradeServer::Run() { return true; }

void TradeServer::Stop() {
  if (service_) {
    service_->Stop();
  }
}

void TradeServer::OnShowVersion() {
  INFO_LOG("[TradeServer] coinrunner v1.0");
}

TradeDispatcherService *TradeServer::CreateService() {
  return new TradeDispatcherService();
}

// ========== 全局访问 ==========

void InitMockFramework() {
  if (g_mock_server) return;
  g_mock_server = new TradeServer();
}

TradeServer *GetMockServer() { return g_mock_server; }

void RegisterMockEngine(TradeEngine *engine) {
  GetPendingEngines().push_back(engine);
}

END_NOVA_NAMESPACE(trade)

// ========== GetServer 全局函数 ==========

nova::base::Server *nova::base::GetServer() {
  auto *srv = nova::trade::GetMockServer();
  if (!srv) {
    nova::trade::InitMockFramework();
    srv = nova::trade::GetMockServer();
  }
  return static_cast<nova::base::Server *>(srv);
}
