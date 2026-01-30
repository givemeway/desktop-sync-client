#pragma once

#include <concepts>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace sync_app {

class ThreadPool {
public:
  explicit ThreadPool(size_t threads = std::thread::hardware_concurrency())
      : stop(false) {
    if (threads == 0) {
      threads = 4; // Fallback if hardware_concurrency returns 0
    }
    for (size_t i = 0; i < threads; ++i)
      workers.emplace_back([this] {
        for (;;) {
          std::function<void()> task;

          {
            std::unique_lock<std::mutex> lock(this->queue_mutex);
            this->condition.wait(
                lock, [this] { return this->stop || !this->tasks.empty(); });
            if (this->stop && this->tasks.empty())
              return;
            task = std::move(this->tasks.front());
            this->tasks.pop();
          }

          task();
        }
      });
  }

  // C++20: Use concepts to constrain the template
  template <class F, class... Args>
    requires std::invocable<F, Args...>
  auto enqueue(F &&f, Args &&...args)
      -> std::future<typename std::invoke_result_t<F, Args...>> {
    using return_type = typename std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();
    {
      std::unique_lock<std::mutex> lock(queue_mutex);

      if (stop)
        throw std::runtime_error("enqueue on stopped ThreadPool");

      tasks.emplace([task]() { (*task)(); });
    }
    condition.notify_one();
    return res;
  }

  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      stop = true;
    }
    condition.notify_all();
    // std::jthread automatically joins on destruction
  }

private:
  std::vector<std::jthread> workers; // C++20 uses jthread
  std::queue<std::function<void()>> tasks;

  std::mutex queue_mutex;
  std::condition_variable condition; // Standard CV works fine with mutex
  bool stop;
};

} // namespace sync_app
