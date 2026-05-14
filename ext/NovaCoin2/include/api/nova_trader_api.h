#pragma once

#include "nova_api_trade_position.h"
#include "nova_api_util.h"

#include "nova_api_datainfo.h"
#include "nova_api_struct.h"

#include "base_async_log.h"
#include "base_config.h"
#include "base_hash.h"

#include <unordered_map>

#include "nlohmann_json/json.hpp"

USE_NOVA_NAMESPACE(base)
USE_NOVA_NAMESPACE(quote)
BEGIN_NOVA_NAMESPACE(trade)

class StrategyApi {
public:
  using OrderTp = NovaOrder;
  using PriceTp = double;

  using Depth = NovaCoinDepth;
  using DepthLVN = NovaCoinDepthLVN;
  using Trade = NovaCoinTrade;
  using BBO = NovaCoinBBO;
  using Bar = NovaCoinBar;

public:
  StrategyApi() : impl_(nullptr) {}
  virtual ~StrategyApi() = default;
  void SetImplement(void *p) { impl_ = p; }
  void SetName(const char *name);
  void SetCore(int cpu);

public:
  static const char *GetConfigFile();

  bool SubQuote(const SecurityPosition *position, NOVA_COIN_QUOTE_TYPE dtype,
                NOVA_COIN_QUOTE_OPTION_TYPE apt);

  struct SubTopic {
    const SecurityPosition *position;
    NOVA_COIN_QUOTE_TYPE quote_type = NOVA_COIN_QUOTE_INIT;

    bool trigger = false;
    int32_t buf_capacity = 512;
    int64_t overtime = -1;
  };

  const DataInfoManager *SubDataInfo(const std::vector<SubTopic> &subs);

  SecurityPosition *CreateSecurityPosition(InstrumentId instrument_id,
                                           double long_blocked = 0,
                                           double short_blocked = 0) const;

  TradeEngine *GetTradeEngineByInstrument(const InstrumentId &) const;

  AccountPosition *GetAccountPosition(InstrumentId instrument_id) const;

  FundAssetManager *GetFundAsset(TradeEngine *engine) const;
  AccountPositionManager *GetAccountPositionManager() const;

public:
  bool AddReminder(uint64_t nano_time, void *data);

  const InstrumentBaseInfo *GetBaseInfo(InstrumentId ins_id) const;
  const InstrumentBaseInfo *GetBaseInfo(const char *symbol,
                                        NOVA_EXCHANGE_TYPE exch) const;

  bool is_simulator() const;

public:
  const OrderTp *CreateOrder(const SecurityPosition *securitposition,
                             NOVA_PRICE_TYPE price_type, NOVA_SIDE_TYPE side,
                             NOVA_POSITION_EFFECT_TYPE position_effect,
                             double qty, double price, uint64_t user_data = 0,
                             TradeEngine *engine = nullptr);

  bool SendOrder(const OrderTp *order);
  bool CancelOrder(const OrderTp *order);
  bool AmendOrder(const OrderTp *order, double new_price);

  bool BatchSendOrder(const OrderTp **orders, size_t len);
  bool BatchCancelOrder(const OrderTp **orders, size_t len);

public:
  virtual bool on_init(const Config *cfg) = 0;
  virtual void on_stop() {}
  virtual void on_user_command(const char *msg) {}

public:
  virtual void on_depth(const Depth *d, const SecurityPosition *position) {}
  virtual void on_depth_lvn(const DepthLVN *d,
                            const SecurityPosition *position) {}
  virtual void on_trade(const Trade *trade, const SecurityPosition *position) {}
  virtual void on_bbo(const BBO *bbo, const SecurityPosition *position) {}
  virtual void on_bar(const Bar *bar, const SecurityPosition *position) {}

  virtual void on_datainfo(const DataInfoManager *datainfo, int32_t di,
                           const SecurityPosition *position) {}

  virtual void on_poll(int64_t local_ns) {}

  virtual void on_order_accepted(const OrderTp *, const SecurityPosition *) {}
  virtual void on_order_update(const OrderTp *, const SecurityPosition *) {}
  virtual void on_order_cancelled(const OrderTp *,
                                  const SecurityPosition *position,
                                  double qty_cancelled) {}
  virtual void on_order_rejected(const OrderTp *order,
                                 const SecurityPosition *position,
                                 int32_t reason) {}
  virtual void on_order_cancel_failed(const OrderTp *,
                                      const SecurityPosition *position,
                                      int32_t reason) {}

  virtual void on_order_amended(const OrderTp *,
                                const SecurityPosition *position,
                                int32_t reason) {}

  virtual void on_order_done(const OrderTp *order,
                             const SecurityPosition *position,
                             NOVA_ORDER_STATUS done_status) {}
  virtual void on_reminder(void *data, uint64_t cur_ns) {}

public:
  int GetUniqReqid();

  bool QueryMarketInfomation(TradeEngine *engine, const nlohmann::json &req,
                             int req_id,
                             QUERY_PROXY_TYPE proxy = QUERY_HTTP_REST);
  virtual void OnRspMarketInformation(TradeEngine *engine, nlohmann::json &rsp,
                                      int req_id) {}

  bool QueryTradeInformation(TradeEngine *engine, const nlohmann::json &req,
                             int req_id,
                             QUERY_PROXY_TYPE proxy = QUERY_HTTP_REST);
  virtual void OnRspTradeInformation(TradeEngine *engine, nlohmann::json &rsp,
                                     int req_id) {}

private:
  void *impl_;
};
class StrategyApiManager {
public:
  virtual bool Initialize(const Config *cfg) = 0;
  virtual bool before_init(StrategyApi **all_sty, size_t size) = 0;
  virtual bool after_init(StrategyApi **all_sty, size_t size) = 0;
  virtual const std::vector<StrategyApi *> *strategies() const {
    return nullptr;
  };
};

class MessageSender {
protected:
  MessageSender() = default;
  virtual ~MessageSender() = default;

public:
  static MessageSender *Create();
  static void Destory(MessageSender *);

public:
  struct Dst {
    std::string dst_ip;
    int64_t dst_port;

    std::string src_ip;
    int64_t src_port;
  };

  virtual bool Init(const std::vector<Dst> &) = 0;

  virtual bool Send(int32_t dst_i, const char *msg, uint32_t bytes) = 0;
};

class MessageReceiver {
protected:
  MessageReceiver() = default;
  virtual ~MessageReceiver() = default;

public:
  static MessageReceiver *Create();
  static void Destory(MessageReceiver *);

public:
  struct Src {
    std::string src_ip;
    int64_t src_port;
  };

  using OnMessageT = std::function<void(const char *msg, size_t bytes,
                                        const sockaddr_in *src)>;

  virtual bool Init(const std::vector<Src> &, OnMessageT cb,
                    int32_t cb_thread_core, int32_t cb_loop_ms = 0) = 0;

  virtual bool Stop() = 0;
};

END_NOVA_NAMESPACE(trade)

extern "C" ::nova::trade::StrategyApi *NovaCreateStrategyApi();
extern "C" ::nova::trade::StrategyApiManager *NovaCreateStrategyApiManager();
extern "C" ::nova::base::version_t NovaStrategyApiVersion();

#define STRATEGY_API_EXPORT_VERSION                                            \
  ::nova::base::version_t NovaStrategyApiVersion() {                           \
    return ::nova::base::str_to_version(NOVA_VERSION);                         \
  }

#define STRATEGY_API_IMPLEMENT(_m_sty)                                         \
  ::nova::trade::StrategyApi *NovaCreateStrategyApi() { return new _m_sty{}; } \
  STRATEGY_API_EXPORT_VERSION
#define STRATEGY_API_MANAGER_IMPLEMENT(_m_sty)                                 \
  ::nova::trade::StrategyApiManager *NovaCreateStrategyApiManager() {          \
    return new _m_sty{};                                                       \
  }                                                                            \
  STRATEGY_API_EXPORT_VERSION

using NovaCreateStrategyApiFunc = ::nova::trade::StrategyApi *(*)();
using NovaCreateStrategyApiManagerFunc =
    ::nova::trade::StrategyApiManager *(*)();