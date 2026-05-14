
#pragma once

#include "nova_base.h"
#include "quote_struct.h"
#include "quote_util.h"
#include <functional>

BEGIN_NOVA_NAMESPACE(quote)

class QueueManager {
public:
  using MapT = base::Trie<void *, CoinCharVal>;

public:
  QueueManager() { ques_ = MapT::Create(); }
  void *GetQue(const char *name) {
    auto *p = ques_->Find(name);
    if (p == nullptr)
      return nullptr;
    return *p;
  }

  bool InsertQueue(const char *name, void *que) {
    std::lock_guard<decltype(lock_)> _l(lock_);
    bool ret = ques_->Insert(name, que);
    return ret;
  }

private:
  MapT *ques_;
  SpinLock lock_;
};

END_NOVA_NAMESPACE(quote)