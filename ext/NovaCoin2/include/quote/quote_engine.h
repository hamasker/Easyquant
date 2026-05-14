#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "base_common.h"
#include "nova_api_instrument.h"
#include "nova_base.h"

#include "quote_struct.h"

#include "nvws/ws_websocket_client.h"
#include "quote_queue_mgr.h"
#include <functional>

USE_NOVA_NAMESPACE(ws)

BEGIN_NOVA_NAMESPACE(quote)

class NOVA_ALIGNED_CACHE_LINE QuoteEngine {
public:
  using InstrumentMapTp = std::unordered_map<std::string, void *>;

  using QuoteCfg = QuoteBaseConsts;

  using Depth = QuoteCfg::Depth;
  using DepthLVN = QuoteCfg::DepthLVN;
  using Trade = QuoteCfg::Trade;
  using BBO = QuoteCfg::BBO;
  using CoinBar = QuoteCfg::Bar;
  //   using Variant = QuoteCfg::Variant;

  using MetaItem = QuoteMetaItem;

  static constexpr auto FORCE_RESUB_S = 20 * 60;

  using UdpCallback = std::function<void(const char *data, uint32_t len,
                                         const sockaddr_in *addr)>;

public:
  QuoteEngine() = default;
  virtual ~QuoteEngine() = default;

  virtual bool Initialize(const Config &cfg);
  virtual void Destroy() {}

  virtual bool OnUserCommand(const char *msg) {}

public:
  virtual const char *name() const = 0;
  std::string config_root() const { return std::string("quote.") + name(); }

public:
  bool CreateMetaQueue();
  bool LoadInstruments(const Config *cfg);
  bool CheckConfigUpdated(int64_t sec) const;
  bool ReloadConfig(Config &out_cfg) const;
  bool LongtimeIdle(int32_t sec = FORCE_RESUB_S) const;

  InstrumentId
  GetInstrumentFromExchangeTicker(const std::string &ticker) const {
    auto iter = exch_ticker_to_inst_.find(ticker);
    if (iter == exch_ticker_to_inst_.cend())
      return InstrumentId::Invalid();
    return iter->second;
  };

private:
  template <typename ItemT> std::string QueueFullPath(const char *name);
  template <typename ItemT>
  void *CreateQueue(const std::string &full_path, uint32_t version = 0);
  void PushMeta(const MetaItem *mi);

  template <typename ItemT>
  __attribute__((hot)) void *GetOrCreateQueue(QueueManager *mgr,
                                              const InstrumentId &inst) {
    auto *q = mgr->GetQue(inst.GetSymbol());
    slow_if(q == nullptr) {
      auto full_path = QueueFullPath<ItemT>(inst.GetSymbol());
      q = CreateQueue<ItemT>(full_path.c_str());
      if (q == nullptr)
        return nullptr;
      MetaItem mi{inst, ItemT::QuoteType()};
      PushMeta(&mi);
      mgr->InsertQueue(inst.GetSymbol(), q);
    }
    return q;
  }

protected:
  std::pair<void *, Depth *> TryPushDepth(const InstrumentId &inst);
  void EndPushDepth(void *q, const Depth *d);
  std::pair<void *, DepthLVN *> TryPushDepthLVN(const InstrumentId &inst);
  void EndPushDepthLVN(void *q, const DepthLVN *);
  std::pair<void *, Trade *> TryPushTrade(const InstrumentId &inst);
  void EndPushTrade(void *q, const Trade *);
  std::pair<void *, BBO *> TryPushBBO(const InstrumentId &inst);
  void EndPushBBO(void *q, const BBO *);
  std::pair<void *, CoinBar *> TryPushBar(const InstrumentId &inst);
  void EndPushBar(void *q, const CoinBar *);
  //   std::pair<void *, NovaCoinVariant> TryPushVariant(const InstrumentId
  //   &inst); void EndPushVariant(void *q, const NovaCoinVariant *);

private:
  void *meta_que_ = nullptr;
  QueueManager trade_ques_;
  QueueManager depth_ques_;
  QueueManager depth_lvn_ques_;
  QueueManager bbo_ques_;
  QueueManager bar_ques_;
  QueueManager var_ques_;

protected:
  int64_t bind_core_ = -1;
  int64_t last_push_s_ = 0;
  std::vector<InstrumentId> all_instruments_;
  std::unordered_map<std::string, InstrumentId> exch_ticker_to_inst_;
  std::mutex all_instruments_mtx_;
  std::string config_file_;
  std::string enviorment_ = "";
};

#ifdef _WIN32
extern "C" __declspec(dllexport) QuoteEngine *CreateQuoteEngine();
#else
extern "C" QuoteEngine *CreateQuoteEngine();
#endif

#define QUOTE_ENGINE_IMPLEMENT(_quote_engine)                                  \
  QuoteEngine *CreateQuoteEngine() { return new _quote_engine{}; };
#define QUOTE_ENGINE_IMPLEMENT_SINGLETON(_quote_engine)                        \
  QuoteEngine *CreateQuoteEngine() { return _quote_engine::Instance(); };

using CreateQuoteEngineFunc = QuoteEngine *(*)();

END_NOVA_NAMESPACE(quote)