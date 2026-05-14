#pragma once

#include "nova_api_data_type.h"
#include "nova_api_exch.h"
#include "nova_api_inst_type.h"
#include "nova_api_trade_position.h"
#include "nova_base.h"

#include "quote_data_type.h"

#include "trade_order_manager.h"
#include "trade_order_resume.h"
#include "trade_service.h"
#include "trade_strategy.h"
#include "trade_struct.h"
#include "trade_util.h"

#include "nvws/ws_web_client.h"
#include "nvws/ws_websocket_client.h"

#include "nlohmann_json/json.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>

USE_NOVA_NAMESPACE(ws)
USE_NOVA_NAMESPACE(base)
USE_NOVA_NAMESPACE(quote)

BEGIN_NOVA_NAMESPACE(trade)

class InfomationTp;
class DispatcherManager;

class TradeEngine {
public:
  enum TRADE_ENGINE_HELPER_EVENT_TYPE {
    TRADE_ENGINE_HELPER_EVENT_INIT = 0,
    TRADE_ENGINE_HELPER_EVENT_DISCONNECT = 101,
    TRADE_ENGINE_HELPER_EVENT_UNKNOWN = 1000,
  };

  struct TradeEngineHelperEvent;
  using HelperFunc = void (*)(const TradeEngineHelperEvent *);
  struct TradeEngineHelperEvent {
    TRADE_ENGINE_HELPER_EVENT_TYPE event_type;
    TradeEngine *engine;
    HelperFunc callback;
    union {
      uint64_t u64;
      void *data;
    };
  };

  using HelperQueueTp = IntrusiveSpscQueue<TradeEngineHelperEvent, 1024>;

public:
  using AccountPositionManagerTp =
      std::unordered_map<InstrumentId::Key, AccountPosition *>;
  using FundAssetManagerTp = std::unordered_map<InstrumentId::Key, FundAsset *>;

  using OrderResumeMgr = NovaOrderResumeMgr;
  using TradeResumeMgr = NovaTradeResumeMgr;

  using TradeCfg = TradeBaseConsts;
  using OrderEventTp = NovaOrderEvent;
  using ReportQueueTp =
      IntrusiveSpscQueue<OrderEventTp, TradeCfg::QUEUE_CAPACITY>;

public:
  TradeEngine(){};
  virtual ~TradeEngine() = default;
  virtual bool Initialize(const Config &cfg);
  virtual void Destroy();

  virtual bool PreRun() { return true; }

public:
  const NovaOrderDetail *
  CreateOrder(Strategy *stg, const SecurityPosition *position,
              NOVA_PRICE_TYPE price_type, NOVA_SIDE_TYPE sizde,
              NOVA_POSITION_EFFECT_TYPE position_effect, double qty,
              double price, uint64_t user_data = 0);

public:
  void SendOrder(const NovaOrderDetail *order);
  void CancelOrder(const NovaOrderDetail *order);
  bool AmendOrder(const NovaOrderDetail *order, double new_price);

  void BatchSendOrder(const NovaOrderDetail **orders, size_t len);
  void BatchCancelOrder(const NovaOrderDetail **orders, size_t len);

  AccountPosition *GetOrCreateAccountPosition(const InstrumentId &);
  AccountPosition *GetAccountPosition(const InstrumentId &) const;
  bool UpdateAccountPosition(const InstrumentId &inst, double vlong,
                             double vshort, double long_frozen,
                             double short_frozen, uint64_t ns);
  static bool UpdateAccountPosition(AccountPosition *acc_pos, double vlong,
                                    double vshort, double long_frozen,
                                    double short_frozen, uint64_t ns);

  virtual bool AsyncQueryAccountPosition(const SecurityPosition *pos) {
    return false;
  }
  virtual bool AsyncQueryFundAsset() { return false; }

  virtual const char *name() const = 0;
  virtual ReportQueueTp *message_queue() = 0;

  virtual FundAssetManager *fund_asset() = 0;
  virtual AccountPositionManagerTp *AccountPositionInfo() = 0;

  virtual const OrderResumeMgr *order_resume() const = 0;
  virtual const TradeResumeMgr *trade_resume() const = 0;

  virtual bool QueryAccountPosition(uint64_t timeour_nsec,
                                    const Config *cfg) = 0;
  virtual bool QueryAccountFundAsset(uint64_t timeour_nsec,
                                     const Config *cfg) = 0;

  virtual NOVA_EXCHANGE_TYPE account_exchange() const = 0;
  virtual NOVA_COIN_INST_TYPE target_inst_type() const = 0;
  virtual const char *config_root() const { return config_root_; }

  NOVA_EXCHANGE_TYPE Exchange() const { return exchange_; }
  NOVA_COIN_INST_TYPE InstType() const { return inst_type_; }

  virtual double GetRateLimit(const std::string &key) { return 0; }

protected:
  virtual void DoSendOrder(const NovaOrderDetail *order) = 0;
  virtual void DoCancelOrder(const NovaOrderDetail *order) = 0;
  virtual bool DoAmendOrder(const NovaOrderDetail *order) { return false; }
  virtual void DoBatchSendOrder(const NovaOrderDetail **orders, size_t len) = 0;
  virtual void DoBatchCancelOrder(const NovaOrderDetail **orders,
                                  size_t len) = 0;

  void InnerReject(NovaOrderDetail *order, uint64_t time, uint32_t reason);

public:
  static void StrategyOnOrderEvent(TradeEngine *engine,
                                   const NovaOrderEvent *event);

  static void StrategyOnInformation(Strategy *, InfomationTp *msg);

protected:
  static constexpr auto PAD_SIZE = NOVA_CACHE_LINE - 2 * sizeof(void *);
  char config_root_[PAD_SIZE];
  OrderManager *ord_mgr_;
  NOVA_EXCHANGE_TYPE exchange_{NOVA_EXCHANGE_UNKNOWN};
  NOVA_COIN_INST_TYPE inst_type_{NOVA_COIN_INST_TYPE_UNKNOWN};
  bool is_mock_ = false;
  double initial_leverage_ = 1;

private:
  DispatcherManager *dis_manager_ = nullptr;

public:
  bool is_mock() const { return is_mock_; }
  virtual void FeedDepth(const NovaCoinDepth *) {}
  virtual void FeedDepthLVN(const NovaCoinDepthLVN *) {}
  virtual void FeedBBO(const NovaCoinBBO *) {}
  virtual void FeedTrade(const NovaCoinTrade *) {}
  virtual void FeedBar(const NovaCoinBar *) {}

public:
  void RegisterDispatcherManager(DispatcherManager *m) { dis_manager_ = m; }

public:
  virtual bool DoQueryInformation(const nlohmann::json &req, int req_id,
                                  bool public_information,
                                  QUERY_PROXY_TYPE proxy = QUERY_HTTP_REST) {
    return false;
  }

protected:
  void CallOnRspInformation(nlohmann::json &&rsp, int req_id,
                            bool public_information);
};

#ifdef _WIN32

#else
extern "C" TradeEngine *CreateTradeEngine();
#endif

#define TRADE_ENGINE_IMPLEMENT(_trade_engine)                                  \
  TradeEngine *CreateTradeEngine() { return new _trade_engine; };
#define TRADE_ENGINE_IMPLEMENT_SINGLETON(_trade_engine)                        \
  TradeEngine *CreateTradeEngine() { return _trade_engine::Instance(); }

using CreateTradeEngineFunc = TradeEngine *(*)();

END_NOVA_NAMESPACE(trade)