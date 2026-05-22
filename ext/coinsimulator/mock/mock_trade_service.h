#pragma once

#include "trade/trade_service.h"
#include "nova_trader_api.h"

#include <vector>

BEGIN_NOVA_NAMESPACE(trade)

// ========== 轻量状态存储 (替代 MockTradeDispatcherService 子类) ==========

struct MockServiceState {
  StrategyApi *strategy = nullptr;
  std::vector<StrategyApi::SubTopic> subs;
  DataInfoManager *data_info_mgr = nullptr;
  std::vector<TradeEngine *> engines;

  struct ReminderEntry {
    uint64_t nano_time;
    void *data;
  };
  std::vector<ReminderEntry> reminders;
  std::mutex reminder_mutex;

  void AddReminder(uint64_t ns, void *data) {
    std::lock_guard<std::mutex> lk(reminder_mutex);
    reminders.push_back({ns, data});
  }

  static MockServiceState &Instance();
};

// 供 TradeDispatcherService::Initialize() 消费的 pending engine 列表
std::vector<TradeEngine *> &GetPendingEngines();

END_NOVA_NAMESPACE(trade)
