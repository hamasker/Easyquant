#pragma once

#include "base/base_async_log.h"
#include "base/base_util.h"
#include <deque>
#include <vector>

BEGIN_NOVA_NAMESPACE(base)

template <typename T> class BlockObjectPool {
public:
  static const int PAGE_M = 4096;

public:
  static BlockObjectPool *ThreadLocalInstance() {
    thread_local BlockObjectPool ret;
    return &ret;
  }
  static_assert(sizeof(T) <= PAGE_M, "larger than 4096");

  BlockObjectPool(int page_size = PAGE_M) : page_size_(page_size) {
    slow_if(page_size_ % PAGE_M != 0) {
      // ERROR_LOG("");
      exit(0);
    }
    page_list_.clear();
    free_list_.clear();
  }
  ~BlockObjectPool() {
    int np = page_list_.size();
    for (int i = 0; i < np; ++i)
      free(page_list_[i]);
    page_list_.clear();
    free_list_.clear();
  }

  T *Alloc() {
    fast_if(!free_list_.empty()) {
      T *ret = free_list_.back();
      free_list_.pop_back();
      return ret;
    }
    void *vblock = CacheLineAlignedAlloc(page_size_);
    slow_if(vblock == nullptr) { return nullptr; }
    page_list_.push_back(vblock);
    T *new_list = reinterpret_cast<T *>(vblock);
    int count = page_size_ / sizeof(T);
    total_space_ += count;
    n_pages_++;
    for (T *it = new_list + count - 1; it > new_list; it--)
      free_list_.push_back(it);
    return new_list;
  }

  void Free(T *t) { free_list_.push_back(t); }

  int Space() const { return free_list_.size(); }

  int TotalSpace() const { return total_space_; }

  int Pages() const { return n_pages_; }

  int DataSize() const { return sizeof(T); }

  int PageSize() const { return page_size_; }

  int CountPerPage() const { return page_size_ / sizeof(T); }

  void PrintStats() {}

protected:
  std::deque<void *> page_list_;
  std::deque<T *> free_list_;
  int page_size_ = PAGE_M;
  int total_space_ = 0;
  int n_pages_ = 0;
};

END_NOVA_NAMESPACE(base)