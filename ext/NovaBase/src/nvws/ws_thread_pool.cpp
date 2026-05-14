#include "nvws/ws_thread_pool.h"
#include "base/base_os_thread.h"
#include <chrono>

BEGIN_NOVA_NAMESPACE(ws)

WSThreadPool::WSThreadPool()
    : delete_task_if_exception_(false), continue_(true), core_(-1), loop_ms_(0),
      spi_thread_(nullptr) {}

/**
 * 获取线程池单例实例
 */
WSThreadPool *WSThreadPool::Instance() {
  static WSThreadPool instance;
  return &instance;
}

/**
 * 运行线程池主循环
 * 该函数会创建一个新线程来执行任务队列中的任务
 */
void WSThreadPool::Run() {
  if (spi_thread_ != nullptr) {
    // 线程已经在运行
    return;
  }

  continue_.store(true, std::memory_order_release);

  spi_thread_ = new std::thread([this]() {
    // 如果指定了CPU核心，可以在此绑定；当前实现不强制绑定，避免外部符号依赖
    // if (core_.load(std::memory_order_acquire) >= 0) {
    //   // 预留：BindCore(static_cast<uint32_t>(core_.load(std::memory_order_acquire)));
    // }

    auto loop_interval_ms = loop_ms_.load(std::memory_order_acquire);

    while (continue_.load(std::memory_order_acquire)) {
      // 从推送队列中获取新任务
      {
        std::lock_guard<std::mutex> lock(pushing_task_lock_);
        if (!pushing_tasks_.empty()) {
          // 将新任务移动到工作队列
          pool_tasks_.insert(pool_tasks_.end(),
                             std::make_move_iterator(pushing_tasks_.begin()),
                             std::make_move_iterator(pushing_tasks_.end()));
          pushing_tasks_.clear();
        }
      }

      // 执行任务队列中的所有任务
      if (!pool_tasks_.empty()) {
        auto it = pool_tasks_.begin();
        while (it != pool_tasks_.end()) {
          bool keep_task = true;

          try {
            // 执行任务，如果任务返回false，则从队列中移除
            keep_task = (*it)();
          } catch (const std::exception &e) {
            // 任务执行出现异常
            if (delete_task_if_exception_) {
              keep_task = false;
            }
          } catch (...) {
            // 未知异常
            if (delete_task_if_exception_) {
              keep_task = false;
            }
          }

          if (keep_task) {
            ++it;
          } else {
            // 移除已完成的任务
            it = pool_tasks_.erase(it);
          }
        }
      }

      // 如果设置了循环间隔，则休眠
      if (loop_interval_ms > 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(loop_interval_ms));
      } else {
        // 没有设置间隔，让出CPU时间片
        std::this_thread::yield();
      }
    }
  });
}

END_NOVA_NAMESPACE(ws)
