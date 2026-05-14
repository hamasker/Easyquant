#pragma once

#include "base/base_async_log.h"
#include "base/base_block_object_pool.h"
#include "base/base_util.h"

BEGIN_NOVA_NAMESPACE(base)

struct DefaultCharVal {
  static int Get(char ch) {
    if ('0' <= ch && ch <= '9')
      return ch - '0';
    else if ('A' <= ch && ch <= 'Z')
      return ch - 'A' + 10;
    else if ('a' <= ch && ch <= 'z')
      return ch - 'a' + 36;
    return -1;
  }
  static int Char(int x) {
    if (x < 0)
      return '?';
    else if (x < 10)
      return '0' + x;
    else if (x < 36)
      return 'A' + x - 10;
    else if (x < 62)
      return 'a' + x - 36;
    return '?';
  }
  static constexpr int CharSetN = 62;
};

template <typename DataT, typename CharVal = DefaultCharVal> class Trie {

protected:
  struct TrieNode {
    TrieNode *Kids[CharVal::CharSetN];
    DataT Data;
    bool HasData = false;
    TrieNode() { Init(); }
    void Init() {
      memset(Kids, 0, sizeof(Kids));
      HasData = false;
    }
  };

public:
  using NodeT = TrieNode;
  using AllocatorT = BlockObjectPool<NodeT>;

protected:
  Trie(AllocatorT *alloc, bool private_alloc)
      : alloc_(alloc), private_allocator_(private_alloc) {
    root_ = alloc->Alloc();
    root_->Init();
  }

public:
  Trie() = delete;
  ~Trie() {
    if (private_allocator_)
      delete alloc_;
  }

  static Trie *Create(AllocatorT *alloc = nullptr) {
    bool private_alloc = (alloc == nullptr);
    if (private_alloc)
      alloc = new AllocatorT();
    return new Trie(alloc, private_alloc);
  }

  bool Insert(const char *text, const DataT &data) {
    return Insert(text, strlen(text), data);
  }
  bool Insert(const char *text, int len, const DataT &data) {
    NodeT *x = root_;
    for (int p = 0; p < len; p++) {
      int i = CharVal::Get(text[p]);
      slow_if(i < 0) { return false; }
      NodeT *&y = x->Kids[i];
      if (y == nullptr) {
        y = alloc_->Alloc();
        y->Init();
      }
      x = y;
    }
    if (!x->HasData)
      count_ += 1;
    x->HasData = true;
    x->Data = data;
    return true;
  }

  bool Remove(const char *text) { return Remove(text, strlen(text)); }
  bool Remove(const char *text, int len) {
    NodeT *x = root_;
    for (int p = 0; p < len; p++) {
      int i = CharVal::Get(text[p]);
      slow_if(i < 0) return false;
      NodeT *y = x->Kids[i];
      if (y == nullptr)
        return false;
      x = y;
    }
    bool ret = false;
    if (x->HasData) {
      ret = true;
      x->HasData = false;
      count_ -= 1;
    }
    return ret;
  }

  DataT *Find(const char *text) { return Find(text, strlen(text)); }
  DataT *Find(const char *text, int len) {
    NodeT *x = root_;
    for (int p = 0; p < len; p++) {
      int i = CharVal::Get(text[p]);
      slow_if(i < 0) return nullptr;
      NodeT *y = x->Kids[i];
      if (y == nullptr)
        return nullptr;
      x = y;
    }
    return (x->HasData ? &x->Data : nullptr);
  }

  void AllData(std::vector<std::pair<std::string, DataT>> &res) {
    res.clear();
    walk(root_, "", [&res](const std::string &key, DataT data) {
      res.emplace_back(key, data);
    });
  }

  template <typename Func> void Walk(Func &&func) { walk(root_, "", func); }

  int Count() const { return count_; }

protected:
  template <typename Func> void walk(NodeT *x, std::string key, Func &&func) {
    if (x->HasData)
      func(key, x->Data);
    for (int i = 0; i < CharVal::CharSetN; i++) {
      if (x->Kids[i] != nullptr) {
        key.push_back(CharVal::Char(i));
        walk(x->Kids[i], key, func);
        key.pop_back();
      }
    }
  }

protected:
  NodeT *root_ = nullptr;
  AllocatorT *alloc_ = nullptr;
  int count_ = 0;
  bool private_allocator_ = false;
};

END_NOVA_NAMESPACE(base)