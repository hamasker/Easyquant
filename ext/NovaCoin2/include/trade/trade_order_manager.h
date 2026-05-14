#pragma once

#include "base/base_os_thread.h"
#include "base/base_util.h"
#include "trade/trade_struct.h"

USE_NOVA_NAMESPACE(base)

BEGIN_NOVA_NAMESPACE(trade)
class OrderManager {
public:
  static constexpr int64_t DEFAULT_ORDER_CAPACITY = 4096 * 1024;

protected:
  OrderManager()
      : orders_(nullptr), idx_(1), idx_begin_(1), idx_end_(0),
        capacity_(DEFAULT_ORDER_CAPACITY) {}

public:
  static OrderManager *Instance() {
    static OrderManager *g_inst = nullptr;
    if (g_inst == nullptr) {
      g_inst = new OrderManager();
      if (!g_inst->Initialize())
        return nullptr;
    }
    return g_inst;
  }
  ~OrderManager() = default;

  bool Initialize(int64_t capacity = DEFAULT_ORDER_CAPACITY);
  void Destroy() { delete[] orders_; }

  NovaOrderDetail *AllocOrder();
  void RecycleOrder(NovaOrderDetail *order);
  void PreferchOrder() { Prefetch(&orders_[idx_], _MM_HINT_T0); }

  NovaOrderDetail *GetOrder(int64_t idx) const;
  NovaOrderDetail *GetOrder(NovaOrderId id) const;

  int64_t size() const { return idx_; }
  int64_t capacity() const { return capacity_; }
  std::string info() const {
    char buff[512];
    snprintf(buff, sizeof(buff), "cap:%ld,idx:%ld,begin:%ld,end:%ld(%ld)",
             capacity_, idx_, idx_begin_, idx_end_, idx_end_ + capacity_);
    return buff;
  }
  bool set_idx(int64_t idx) {
    slow_if(idx < idx_) { return false; }
    // INFO_FLOG("[OrderManager.set_index]begin:{},end:{}({}),idx:{},new_idx:{}",
    //           idx_begin_, idx_end_, idx_end_ + capacity_, idx_, idx);
    idx_ = idx;
    idx_begin_ = idx;
    idx_end_ = idx - 1;
    for (int i = 0; i < (idx & cap_mask_); ++i)
      orders_[i].recycled = true;
    return true;
  }

private:
  NovaOrderDetail *orders_;
  int64_t idx_;
  int64_t idx_begin_;
  int64_t idx_end_;
  int64_t capacity_;
  int64_t cap_mask_;
  int64_t cap_warn_;
  SpinLock lock_;
};
END_NOVA_NAMESPACE(trade)