#include "mock/mock_trade_service.h"

#include "base/base_log.h"

BEGIN_NOVA_NAMESPACE(trade)

// ========== MockServiceState ==========

MockServiceState &MockServiceState::Instance() {
  static MockServiceState state;
  return state;
}

std::vector<TradeEngine *> &GetPendingEngines() {
  static std::vector<TradeEngine *> engines;
  return engines;
}

// ========== TradeDispatcherService 非 inline 方法 ==========

bool TradeDispatcherService::Initialize(const Config &cfg) {
  continue_ = true;

  // 处理在 TradeServer::Initialize() 之前通过 RegisterMockEngine() 注册的 engine
  auto &pending = nova::trade::GetPendingEngines();
  for (auto *engine : pending) {
    engine->Initialize(cfg);
    RegisterTradeEngine(engine, cfg);
  }
  pending.clear();

  return true;
}

TradeEngine *
TradeDispatcherService::EngineByInstrument(const InstrumentId &inst) {
  for (auto *engine : trade_engines_) {
    if (engine->Exchange() == inst.exchange &&
        engine->InstType() == inst.inst_type) {
      return engine;
    }
  }
  return default_trade_engine_;
}

const InstrumentBaseInfo *
TradeDispatcherService::GetBaseInfo(const InstrumentId &instrument_id) {
  if (base_info_) {
    return base_info_->get(instrument_id.key());
  }
  return nullptr;
}

bool TradeDispatcherService::RegisterTradeEngine(TradeEngine *engine,
                                                  const Config &) {
  if (!engine) return false;

  for (auto *e : trade_engines_) {
    if (e->Exchange() == engine->Exchange() &&
        e->InstType() == engine->InstType()) {
      return false;
    }
  }

  trade_engines_.push_back(engine);
  if (!default_trade_engine_) {
    default_trade_engine_ = engine;
  }

  INFO_FLOG("[TradeService] Registered engine '{}'", engine->name());
  return true;
}

bool TradeDispatcherService::RegisterStrategy(Strategy *strategy,
                                               SecurityPosition *position,
                                               NOVA_COIN_QUOTE_TYPE,
                                               NOVA_COIN_QUOTE_OPTION_TYPE) {
  strategies_.insert(strategy);
  if (position) {
    security_position_set_.insert(position);
  }
  return true;
}

bool TradeDispatcherService::AddReminder(ClockCallback::CallbackFunc cb_func,
                                          uint64_t nano_time, Strategy *sty,
                                          void *data) {
  (void)cb_func;
  (void)sty;
  MockServiceState::Instance().reminders.push_back({nano_time, data});
  return true;
}

bool TradeDispatcherService::LoadQuote(const Config &) { return true; }
bool TradeDispatcherService::LoadTrade(const Config &) { return true; }
bool TradeDispatcherService::RegisterQuoteEngine(const char *) { return true; }

bool TradeDispatcherService::SetBaseInfo(InstrumentId instrument,
                                          InstrumentBaseInfo *base_info) {
  if (!base_info_) {
    base_info_ = new InstrumentBaseInfoManager();
  }
  return base_info_->set(instrument.key(), base_info);
}

uint64_t TradeDispatcherService::ReceiveQuoteEvent() { return 0; }
uint64_t TradeDispatcherService::ReceiveTradeEvent() { return 0; }

END_NOVA_NAMESPACE(trade)
