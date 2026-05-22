#pragma once

#include "trade/trade_engine.h"

#include <list>
#include <unordered_map>

BEGIN_NOVA_NAMESPACE(trade)

class StrategyApi;

/**
 * MockTradeEngine — 模拟撮合引擎。
 *
 * 维护每个 instrument 的本地订单簿和最新 BBO 行情。
 * SendOrder 时接受订单并尝试以当前 BBO 撮合。
 * 新行情到达时(MatchAgainstBBO)也会检查挂单是否可成交。
 *
 * 撮合规则: 买单价 >= ask → 以 ask 价成交; 卖单价 <= bid → 以 bid 价成交。
 * 支持部分成交和完全成交。
 */
class MockTradeEngine : public TradeEngine {
public:
  MockTradeEngine(NOVA_EXCHANGE_TYPE exch = NOVA_EXCHANGE_KRAKE,
                  NOVA_COIN_INST_TYPE inst = NOVA_COIN_INST_TYPE_SPOT);

  const char *name() const override { return "mock"; }
  ReportQueueTp *message_queue() override;
  FundAssetManager *fund_asset() override;
  AccountPositionManagerTp *AccountPositionInfo() override;

  const OrderResumeMgr *order_resume() const override;
  const TradeResumeMgr *trade_resume() const override;

  bool QueryAccountPosition(uint64_t timeout_nsec, const Config *cfg) override;
  bool QueryAccountFundAsset(uint64_t timeout_nsec, const Config *cfg) override;

  NOVA_EXCHANGE_TYPE account_exchange() const override;
  NOVA_COIN_INST_TYPE target_inst_type() const override;

  double GetRateLimit(const std::string &key) override;

  // 设置策略引用 (用于回调)
  void SetStrategy(StrategyApi *strategy) { strategy_ = strategy; }

  // 更新最新 BBO 行情 (由 feed dispatch 路径调用)
  void UpdateBBO(const InstrumentId &inst, double bid, double ask,
                 double bid_qty = 0, double ask_qty = 0);

  void SetMatchDelayMs(int ms) { match_delay_ms_ = ms; }
  void SetMatchFillRatio(int pct) { match_fill_ratio_ = pct; }
  void SetRecordDir(const std::string &dir) { record_dir_ = dir; }

  // 撮合 match 所有挂单
  void MatchAll();

protected:
  void DoSendOrder(const NovaOrderDetail *order) override;
  void DoCancelOrder(const NovaOrderDetail *order) override;
  void DoBatchSendOrder(const NovaOrderDetail **orders, size_t len) override;
  void DoBatchCancelOrder(const NovaOrderDetail **orders, size_t len) override;
  bool DoAmendOrder(const NovaOrderDetail *order) override;

private:
  struct BBO {
    double bid = 0;
    double ask = 0;
    double bid_qty = 0;
    double ask_qty = 0;
    int64_t update_time = 0;
  };

  struct PendingOrder {
    NovaOrder order{};
    int64_t accept_time = 0;
  };

  // 尝试撮合单个订单
  bool TryMatch(PendingOrder &po);
  void RecordOrder(const NovaOrder &order, const char *event);

  // 回调策略
  void NotifyAccepted(NovaOrder *order);
  void NotifyFilled(NovaOrder *order);
  void NotifyPartialFill(NovaOrder *order);
  void NotifyCancelled(NovaOrder *order);
  void NotifyRejected(NovaOrder *order);

  std::unordered_map<InstrumentId::Key, BBO> bbo_map_;
  std::unordered_map<InstrumentId::Key, std::list<PendingOrder>> order_books_;
  std::unordered_map<const NovaOrderDetail *, PendingOrder *> detail_to_order_;
  std::atomic<uint32_t> next_order_id_{1};

  FundAssetManagerTp fund_assets_;
  AccountPositionManagerTp account_positions_;
  OrderResumeMgr order_resume_;
  TradeResumeMgr trade_resume_;
  ReportQueueTp *report_queue_ = nullptr;

  int match_delay_ms_ = 0;
  int match_fill_ratio_ = 100;
  std::string record_dir_;

protected:
  StrategyApi *strategy_ = nullptr;
};

END_NOVA_NAMESPACE(trade)
