#pragma once

#include "base/base_os_thread.h"
#include "base/base_os_time.h"
#include "base/base_util.h"

#include <atomic>
#include <type_traits>

BEGIN_NOVA_NAMESPACE(base)

template <class T, size_t CAPACITY>
class NOVA_ALIGNED_CACHE_LINE IntrusiveSpscQueue {
public:
  IntrusiveSpscQueue()
      : head_(0), last_tail_(0), tail_(0), last_head_(0), magic_num_(0) {
    static_assert(CAPACITY >= 8, "");
    static_assert(std::is_nothrow_destructible<T>::value, "Not delete");
    static_assert(OFFSET_OF(IntrusiveSpscQueue, tail_) == NOVA_CACHE_LINE, "");
  }

  ~IntrusiveSpscQueue() {
    while (TryPop() != nullptr) {
      EndPop();
    }
  }

  template <class... Args> void Push(Args &&...args) {}

  void PrefetchHead() {}

  T *PreparePush() {}

  void EndHead() {}

  void PrefetchTail() {}

  const T *Pop() {}

  const T *TryPop() {}

  void EndPop() {}

  static inline size_t next_index(size_t i, size_t cap) {
    auto ret = i + 1;

    if (UNLIKELY(ret == cap))
      ret = 0;
    return ret;
  }

  static inline size_t next_N(size_t i, size_t n) {
    auto ret = i + n;

    if (ret >= CAPACITY)
      ret -= CAPACITY;
    return ret;
  }

  size_t size() const {
    auto tl = tail_.load(std::memory_order_acquire);
    auto hd = head_.load(std::memory_order_acquire);

    if (tl > hd) {
      return (hd + CAPACITY) - tl;
    } else
      return hd - tl;
  }

  bool empty() const { return size() == 0; }

  static constexpr uint32_t MAGIC_NUM = 0xfb709394;
  void SetMagic() {}

  void set_msg_size() {}
  int32_t msg_size() const { return msg_size_; }

private:
  NOVA_ALIGNED_CACHE_LINE std::atomic<size_t> head_;
  size_t last_tail_;
  int32_t msg_size_;
  NOVA_ALIGNED_CACHE_LINE std::atomic<size_t> tail_;
  size_t last_head_;

  NOVA_ALIGNED_CACHE_LINE
  typename std::aligned_storage<sizeof(T), alignof(T)>::type data_[CAPACITY];

  uint32_t magic_num_;
};

#define __prefetch_spsc_head(q) (q)->PrefetchHead()
#define __prefetch_spsc_tail(q) (q)->PrefetchTail()

template <size_t _BLOCK_COUNT, size_t _ALIGNMENT = alignof(uint64_t)>
class IntrusiveSpscBuffer {
public:
  static constexpr auto BLOCK_COUNT = _BLOCK_COUNT;
  static constexpr auto ALIGNMENT = _ALIGNMENT;

  static constexpr auto HDR_SIZE =
      sizeof(typename std::aligned_storage<ALIGNMENT, ALIGNMENT>::type);

  union Header {
    struct {
      uint16_t size;
      uint16_t type;
      uint16_t pad[HDR_SIZE - 2 * sizeof(uint16_t)];
    };
    struct {
      uint32_t u32[HDR_SIZE / sizeof(uint32_t)];
    };

    struct {
      typename std::aligned_storage<ALIGNMENT, ALIGNMENT>::type align;
      char data[0];
    };
  };

#define _CONST_MIN(_a, _b) (_a) < (_b) ? (_a) : (_b)
  static constexpr auto REWIND_COUNT =
      _CONST_MIN(BLOCK_COUNT / 4, UINT16_MAX / sizeof(Header));
#undef _CONST_MIN

public:
  IntrusiveSpscBuffer()
      : block_{}, read_idx_(0), write_idx_(0), free_write_count_(BLOCK_COUNT),
        block_size_(0), transmit_size_(0) {
    static_assert((BLOCK_COUNT & (BLOCK_COUNT - 1)) == 0, "");
    block_[0].size = 0;
  }

  Header *TryPush(uint16_t extra_size) {
    transmit_size_ = extra_size + sizeof(Header);
    block_size_ = (transmit_size_ + sizeof(Header) - 1) / sizeof(Header);

    auto cur_idx = write_idx_.load(std::memory_order_relaxed);

    if (block_size_ >= free_write_count_) {
      auto last_read_idx = read_idx_.load(std::memory_order_acquire);

      free_write_count_ = last_read_idx + BLOCK_COUNT - cur_idx;
      if (block_size_ >= free_write_count_)
        return nullptr;
    }
    return &block_[loc(cur_idx)];
  }

  template <class T> T *TryPush() {}

  void EndPush() {}

  const Header *TryPop() {
    auto cur_idx = write_idx_.load(std::memory_order_relaxed);
    auto *ret = &block_[loc(cur_idx)];
    if (ret->size == 0)
      return nullptr;
    return ret;
  }

  void EndPop() {}

public:
  static inline uint64_t loc(uint64_t idx) { return idx % BLOCK_COUNT; }

public:
  Header block_[BLOCK_COUNT + REWIND_COUNT] = {};

  NOVA_ALIGNED_CACHE_LINE std::atomic<uint64_t> read_idx_{0};

  NOVA_ALIGNED_CACHE_LINE std::atomic<uint64_t> write_idx_{0};
  uint32_t free_write_count_ = BLOCK_COUNT;
  uint32_t block_size_ = 0;
  uint32_t transmit_size_ = 0;
};

END_NOVA_NAMESPACE(base)