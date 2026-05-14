#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "nova_api_quote_struct.h"
#include "nova_api_trade_position.h"
#include "trade_callback.h"
#include "trade_engine.h"

USE_NOVA_NAMESPACE(base)

BEGIN_NOVA_NAMESPACE(trade)

class TradeServer;

class DispatcherManager;

class TradeDispatcherService {
public:
  using StrategySetTp = std::set<Strategy *>;

public:
  TradeDispatcherService()
      : dispatcher_(nullptr), clock_callback_(), strategies_(),
        trade_engines_(), default_trade_engine_(nullptr), continue_(false) {}

  virtual ~TradeDispatcherService() = default;

  virtual bool Initialize(const Config &cfg);
  virtual void Destroy() {}

  void Stop() { continue_ = false; }
  static bool Run(TradeDispatcherService *service, TradeServer *server);

  bool RegisterStrategy(Strategy *strategy, SecurityPosition *position,
                        NOVA_COIN_QUOTE_TYPE qtype,
                        NOVA_COIN_QUOTE_OPTION_TYPE opt);

public:
  bool AddReminder(ClockCallback::CallbackFunc cb_func, uint64_t nano_time,
                   Strategy *sty, void *data);
  void SetBusyWait(bool b) { busy_wait_ = b; }
  bool SetBaseInfo(InstrumentId instrument, InstrumentBaseInfo *base_info);

public:
  TradeEngine *EngineByInstrument(const InstrumentId &inst);
  const std::vector<TradeEngine *> *trade_engines() const {
    return &trade_engines_;
  }
  const StrategySetTp &strategies() const { return strategies_; }
  InstrumentBaseInfoManager *GetBaseInfoManager() { return base_info_; }
  const std::set<SecurityPosition *> *security_positions() const {
    return &security_position_set_;
  }

  const InstrumentBaseInfo *GetBaseInfo(const InstrumentId &instrument_id);
  int32_t core() const { return core_; }

protected:
  bool LoadQuote(const Config &cfg);
  bool LoadTrade(const Config &cfg);
  bool RegisterTradeEngine(TradeEngine *engine, const Config &cfg);
  bool RegisterQuoteEngine(const char *que_path);

  bool PreRun();
  uint64_t ReceiveQuoteEvent();
  uint64_t ReceiveTradeEvent();

private:
  DispatcherManager *dispatcher_;
  ClockCallback clock_callback_;

  InstrumentBaseInfoManager *base_info_{nullptr};

  StrategySetTp strategies_;
  std::set<SecurityPosition *> security_position_set_;
  std::vector<TradeEngine *> trade_engines_;
  TradeEngine *default_trade_engine_;

  int32_t core_ = -1;
  std::atomic<bool> continue_;
  bool busy_wait_ = true;
};

END_NOVA_NAMESPACE(trade)