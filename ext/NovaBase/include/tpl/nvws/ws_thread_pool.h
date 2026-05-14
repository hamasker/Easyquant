#include "nvws/ws_util.h"
#include <deque>

BEGIN_NOVA_NAMESPACE(ws)

class WSThreadPool {
  WSThreadPool();

public:
  static WSThreadPool *Instance();

public:
  template <class Callable, typename... Args>
  bool AddTask(Callable &&f, Args &&...args) {
    try {
      std::lock_guard<std::mutex> _{pushing_task_lock_};
      pushing_tasks_.push_back(
          std::bind(std::forward<Callable>(f), std::forward<Args>(args)...));
    } catch (...) {
      return false;
    }
    return true;
  }

public:
  void Run();
  void Stop() { continue_ = false; }
  void SetCore(int32_t c) {
    if (c >= 0)
      core_ = c;
  }
  void SetLoopMS(int32_t ms) {
    if (ms < loop_ms_)
      loop_ms_ = ms;
  }
  void SetDeleteTaskIfException(bool b) { delete_task_if_exception_ = b; }

private:
  bool delete_task_if_exception_ = false;
  std::atomic<bool> continue_{true};
  std::atomic<int32_t> core_{-1};
  std::atomic<int32_t> loop_ms_{0};

  std::deque<std::function<bool()>> pool_tasks_;

  std::mutex pushing_task_lock_;
  std::vector<std::function<bool()>> pushing_tasks_;

  std::thread *spi_thread_ = nullptr;
};

END_NOVA_NAMESPACE(ws)