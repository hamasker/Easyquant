#pragma once

#include <queue>

#include "trade_util.h"

USE_NOVA_NAMESPACE(base)

BEGIN_NOVA_NAMESPACE(trade)

class ClockCallback {
public:
  using CallbackFunc = void (*)(Strategy *, void *, uint64_t cur_ns);

  struct CallbackUnit {
    uint64_t nano;
    Strategy *sty;
    void *data;
    CallbackFunc func;

    CallbackUnit() : nano(0), sty(nullptr), data(nullptr), func(nullptr) {}
    CallbackUnit(uint64_t n, Strategy *s, void *d, CallbackFunc f)
        : nano(n), sty(s), data(d), func(f) {}

    bool operator()(CallbackUnit &l, CallbackUnit &r) {
      return l.nano > r.nano;
    }
  };

  using CallbackMinHeap =
      std::priority_queue<CallbackUnit, std::vector<CallbackUnit>,
                          CallbackUnit>;

public:
  ClockCallback() : heap_() {}
  ~ClockCallback() = default;

  bool AddCallback(CallbackFunc f, uint64_t nano_time, Strategy *sty,
                   void *data);

  size_t size() const { return heap_.size(); }
  size_t empty() const { return heap_.empty(); }
  uint64_t front_ns() const { return heap_.top().nano; };

public:
  int32_t Call(uint64_t cur_nano);

private:
  CallbackMinHeap heap_;
};

END_NOVA_NAMESPACE(trade)