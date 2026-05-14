#pragma once

#include "base/base_util.h"

BEGIN_NOVA_NAMESPACE(base)

class BlockPool {
  explicit BlockPool();
  ~BlockPool() = default;

public:
  static constexpr size_t BlockSize = 4 * MB;
  static constexpr size_t MaxBlocks = 64 * 1024;
  static constexpr size_t DefaultInitBlocks = 4;

  static BlockPool *Instance();
  static void Destroy();

  void *AllocBlock();

protected:
  void **mem;
  std::atomic<uint32_t> blocks{0};
};

class BlockObjectAllocator {
public:
  BlockObjectAllocator();
  ~BlockObjectAllocator() = default;

  static constexpr auto BLOCK_CAPACITY = BlockPool::BlockSize;

  template <class T> inline T *Alloc() {
    auto size = sizeof(T);
    auto align = alignof(T);
    return reinterpret_cast<T *>(Alloc(size, align));
  }

  void *Alloc(size_t size, size_t align);

private:
  char *buf_begin_;
  char *ava_begin_;
};

END_NOVA_NAMESPACE(base)