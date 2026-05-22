#include "nova_trader_api.h"
#include "mock/mock_server.h"
#include "mock/mock_trade_service.h"
#include "mock/mock_trade_engine.h"

#include "base/base_async_log.h"
#include "nova_api_datainfo.h"
#include "nova_api_quote_struct.h"

#include <atomic>

BEGIN_NOVA_NAMESPACE(trade)

// ========== StrategyApi 非虚方法实现 ==========

void StrategyApi::SetName(const char *name) { (void)name; }

void StrategyApi::SetCore(int cpu) { (void)cpu; }

const char *StrategyApi::GetConfigFile() { return ""; }

bool StrategyApi::SubQuote(const SecurityPosition *, NOVA_COIN_QUOTE_TYPE,
                           NOVA_COIN_QUOTE_OPTION_TYPE) {
  return true;
}

const DataInfoManager *
StrategyApi::SubDataInfo(const std::vector<SubTopic> &subs) {
  if (subs.empty()) return nullptr;

  DataInfoManager::Param di_param{};
  di_param.trigger_type = DataInfoManager::TriggerType::TRIGGER_TYPE_MASK;

  for (const auto &sub : subs) {
    DataInfoManager::DIParam entry{};
    entry.dip.buf_capacity = sub.buf_capacity;
    entry.dip.buf_type = sub.quote_type;
    entry.trigger = sub.trigger;
    entry.dip.overtime = sub.overtime;

    switch (sub.quote_type) {
    case NOVA_COIN_QUOTE_DEPTH:
      entry.dip.buf_col_bytes = sizeof(NovaCoinDepth);
      break;
    case NOVA_COIN_QUOTE_DEPTH_LVN:
      entry.dip.buf_col_bytes = sizeof(NovaCoinDepthLVN);
      break;
    case NOVA_COIN_QUOTE_TRADE:
      entry.dip.buf_col_bytes = sizeof(NovaCoinTrade);
      break;
    case NOVA_COIN_QUOTE_BBO:
      entry.dip.buf_col_bytes = sizeof(NovaCoinBBO);
      break;
    case NOVA_COIN_QUOTE_BAR:
      entry.dip.buf_col_bytes = sizeof(NovaCoinBar);
      break;
    default:
      entry.dip.buf_col_bytes = 64;
      break;
    }

    di_param.datainfo.emplace_back(entry);
  }

  auto *mgr = new DataInfoManager();
  if (!mgr->Init(di_param)) {
    delete mgr;
    return nullptr;
  }

  auto &state = MockServiceState::Instance();
  state.subs = subs;
  state.data_info_mgr = mgr;

  return mgr;
}

SecurityPosition *
StrategyApi::CreateSecurityPosition(InstrumentId instrument_id,
                                     double long_blocked,
                                     double short_blocked) const {
  return new SecurityPosition(instrument_id, nullptr, long_blocked,
                               short_blocked);
}

TradeEngine *
StrategyApi::GetTradeEngineByInstrument(const InstrumentId &inst) const {
  auto *srv = GetMockServer();
  if (!srv || !srv->service()) return nullptr;
  return srv->service()->EngineByInstrument(inst);
}

AccountPosition *
StrategyApi::GetAccountPosition(InstrumentId instrument_id) const {
  auto *srv = GetMockServer();
  if (!srv || !srv->service()) {
    static AccountPosition dummy(instrument_id);
    return &dummy;
  }

  auto *engine = srv->service()->EngineByInstrument(instrument_id);
  if (!engine) {
    static AccountPosition dummy(instrument_id);
    return &dummy;
  }

  return engine->GetAccountPosition(instrument_id);
}

FundAssetManager *
StrategyApi::GetFundAsset(TradeEngine *engine) const {
  if (!engine) return nullptr;
  return engine->fund_asset();
}

AccountPositionManager *StrategyApi::GetAccountPositionManager() const {
  return nullptr;
}

bool StrategyApi::AddReminder(uint64_t nano_time, void *data) {
  MockServiceState::Instance().AddReminder(nano_time, data);
  return true;
}

const InstrumentBaseInfo *
StrategyApi::GetBaseInfo(InstrumentId ins_id) const {
  auto *srv = GetMockServer();
  if (!srv || !srv->service()) return nullptr;
  return srv->service()->GetBaseInfo(ins_id);
}

const InstrumentBaseInfo *StrategyApi::GetBaseInfo(const char *symbol,
                                                    NOVA_EXCHANGE_TYPE exch) const {
  return GetBaseInfo(InstrumentId::Create(symbol, exch));
}

bool StrategyApi::is_simulator() const { return true; }

int StrategyApi::GetUniqReqid() {
  static std::atomic<int> counter{0};
  return ++counter;
}

bool StrategyApi::QueryMarketInfomation(TradeEngine *, const nlohmann::json &,
                                         int, QUERY_PROXY_TYPE) {
  return true;
}

bool StrategyApi::QueryTradeInformation(TradeEngine *, const nlohmann::json &,
                                         int, QUERY_PROXY_TYPE) {
  return true;
}

const StrategyApi::OrderTp *
StrategyApi::CreateOrder(const SecurityPosition *position,
                          NOVA_PRICE_TYPE price_type, NOVA_SIDE_TYPE side,
                          NOVA_POSITION_EFFECT_TYPE pos_effect, double price,
                          double qty, uint64_t user_data, TradeEngine *engine) {
  if (!engine || qty <= 0) return nullptr;

  auto *detail = new NovaOrderDetail();
  detail->position = const_cast<SecurityPosition *>(position);
  detail->order.instrument_id = position ? position->instrument : InstrumentId::Invalid();
  detail->order.exchange = engine->Exchange();
  detail->order.price_type = price_type;
  detail->order.side = side;
  detail->order.position_effect = pos_effect;
  detail->order.price = price;
  detail->order.quantity = qty;
  detail->order.user_data = user_data;
  detail->nova_id.sequence = 0;
  detail->qty_left = qty;
  detail->qty_traded = 0;
  detail->avg_price = 0;
  detail->acc_fee = 0;

  return detail;
}

bool StrategyApi::SendOrder(const OrderTp *order) {
  if (!order) return false;

  // 找对应引擎
  auto *srv = GetMockServer();
  if (!srv || !srv->service()) return false;
  auto *engine =
      srv->service()->EngineByInstrument(order->order.instrument_id);
  if (!engine) {
    // 用默认引擎
    const auto &engines = srv->service()->trade_engines();
    if (!engines || engines->empty()) return false;
    engine = engines->front();
  }

  auto *detail = const_cast<NovaOrderDetail *>(
      static_cast<const NovaOrderDetail *>(order));
  engine->SendOrder(detail);
  return true;
}

bool StrategyApi::CancelOrder(const OrderTp *order) {
  if (!order) return false;
  auto *detail = const_cast<NovaOrderDetail *>(
      static_cast<const NovaOrderDetail *>(order));
  auto *srv = GetMockServer();
  if (!srv || !srv->service()) return false;
  auto *engine =
      srv->service()->EngineByInstrument(order->order.instrument_id);
  if (!engine) return false;
  engine->CancelOrder(detail);
  return true;
}

bool StrategyApi::AmendOrder(const OrderTp *, double) { return false; }
bool StrategyApi::BatchSendOrder(const OrderTp **orders, size_t len) {
  for (size_t i = 0; i < len; ++i) SendOrder(orders[i]);
  return true;
}
bool StrategyApi::BatchCancelOrder(const OrderTp **orders, size_t len) {
  for (size_t i = 0; i < len; ++i) CancelOrder(orders[i]);
  return true;
}

END_NOVA_NAMESPACE(trade)
