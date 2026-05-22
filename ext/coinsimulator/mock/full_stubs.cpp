/**
 * full_stubs.cpp — 补齐所有缺失的框架/策略符号。
 */
#include "data_process.h"
#include "markout.h"
#include "taking_all_multi.h"

#include "base/base_block_object_pool.h"
#include "base/base_server.h"
#include "base/base_util.h"
#include "trade/trade_engine.h"

#include <string_view>

// ========== TradeEngine 非 inline 方法 ==========

namespace nova {
namespace trade {

bool TradeEngine::Initialize(const Config &) {
  is_mock_ = true;
  return true;
}

void TradeEngine::Destroy() {}

AccountPosition *
TradeEngine::GetAccountPosition(const InstrumentId &inst) const {
  auto *info = const_cast<TradeEngine *>(this)->AccountPositionInfo();
  auto it = info->find(inst.key());
  if (it != info->end())
    return it->second;
  // 不存在则创建空仓位
  auto *pos = new AccountPosition(inst);
  (*const_cast<TradeEngine *>(this)->AccountPositionInfo())[inst.key()] = pos;
  return pos;
}

AccountPosition *
TradeEngine::GetOrCreateAccountPosition(const InstrumentId &inst) {
  auto *existing = GetAccountPosition(inst);
  if (existing)
    return existing;
  auto *pos = new AccountPosition(inst);
  (*AccountPositionInfo())[inst.key()] = pos;
  return pos;
}

} // namespace trade
} // namespace nova

// ========== Server 虚方法 (提供 key function 以生成 vtable) ==========

namespace nova {
namespace base {

bool Server::Initialize() { return true; }

void Server::Destroy() {}

void Server::OnShowHelp() {}

void Server::OnShowVersion() {}

void Server::SignalHandler(int) {}

void Server::FailureHandler(int) {}

bool Server::SetSignalHandler() { return true; }

} // namespace base
} // namespace nova

// ========== BlockObjectAllocator ==========

namespace nova {
namespace base {

BlockObjectAllocator::BlockObjectAllocator() = default;

void *CacheLineAlignedAlloc(unsigned long) { return nullptr; }

} // namespace base
} // namespace nova

// ========== str_to_version ==========

namespace nova {
namespace base {

version_t str_to_version(std::string_view) {
  version_t v{};
  v.u32 = 0;
  return v;
}

std::string version_to_str(version_t) { return "0.0.0.0"; }

} // namespace base
} // namespace nova

// ========== DataProcess::extract_bar_data (by-value wrapper) ==========

namespace DataProcess {

void extract_bar_data(
    const nova::quote::NovaCoinBar &md, data::bar_data dd,
    std::unordered_map<uint16_t, data::delay_data> /*&delay_map*/) {
  if (md.update_time < dd.server_ts)
    return;
  dd.server_ts = md.update_time;
  dd.local_ts = md.local_time;
  dd.valid = md.instrument_id.Valid();
  dd.high = md.high;
  dd.low = md.low;
  dd.open = md.open;
  dd.close = md.close;
}

} // namespace DataProcess

// ========== MarkoutTable stubs ==========

bool MarkoutTable::load_edges_csv(const std::string &) { return true; }

bool MarkoutTable::load_markout_csv(const std::string &) { return true; }

double MarkoutTable::calculate_drift_cost_micro(int, double, double, double,
                                                double) const {
  return 0.0;
}

// ========== TradeEngine::SendOrder / CancelOrder ==========

namespace nova {
namespace trade {

void TradeEngine::SendOrder(const NovaOrderDetail *order) {
  if (order)
    const_cast<TradeEngine *>(this)->DoSendOrder(order);
}

void TradeEngine::CancelOrder(const NovaOrderDetail *order) {
  if (order)
    const_cast<TradeEngine *>(this)->DoCancelOrder(order);
}

void TradeEngine::BatchSendOrder(const NovaOrderDetail **orders, size_t len) {
  if (orders)
    const_cast<TradeEngine *>(this)->DoBatchSendOrder(orders, len);
}

void TradeEngine::BatchCancelOrder(const NovaOrderDetail **orders, size_t len) {
  if (orders)
    const_cast<TradeEngine *>(this)->DoBatchCancelOrder(orders, len);
}

} // namespace trade
} // namespace nova

// ========== TakingDemo::OnRspTradeInformation (stub -- 声明已移除) ==========
