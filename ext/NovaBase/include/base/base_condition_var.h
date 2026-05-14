#pragma once

#include <condition_variable>
#include <functional>
#include <unordered_map>

#include "base/base_common.h"

BEGIN_NOVA_NAMESPACE(base)

template <typename T = bool> class cv4sync {
public:
  cv4sync() : wait_f(false), called(false) {}

  ~cv4sync() = default;

  void reinit(const T &v = T(), int times = 1);

public:
  using dfunc = std::function<bool(T &)>;
  const T &wait_for(int time_out, dfunc &&);
  const T &wait_for(int time_out, const T &des);
  const T &wait_for(int time_out);

  using vfunc = std::function<void(T &)>;
  void notify_one(const vfunc &);
  void notify_one(const T &v);

  void notify_all(const vfunc &);
  void notify_all(const T &v);

  class cv_guard;
  cv_guard guard(const vfunc &f) { return cv_guard{*this, f}; }
  cv_guard guard(const T &f) { return cv_guard{*this, f}; }

public:
  class cv_guard {};

private:
  T ret;
  bool wait_f;
  bool called;
  std::mutex mut;
  std::condition_variable cv;
  int target_times = 1;
  std::atomic<int> notify_times{0};
};

END_NOVA_NAMESPACE(base)