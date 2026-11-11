#include "worker_pool.h"

#include "platform.h"

namespace mb::detail {

WorkerPool::WorkerPool(int workers) : workers_(workers < 1 ? 1 : workers) {
  threads_.reserve(static_cast<size_t>(workers_ - 1));
  for (int i = 1; i < workers_; ++i) threads_.emplace_back([this, i] { loop(i); });
}

WorkerPool::~WorkerPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  start_cv_.notify_all();
  for (auto& t : threads_) t.join();
}

void WorkerPool::run(const std::function<void(int)>& fn) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    job_ = &fn;
    pending_ = workers_ - 1;
    ++generation_;
  }
  start_cv_.notify_all();
  fn(0);
  std::unique_lock<std::mutex> lock(mutex_);
  done_cv_.wait(lock, [this] { return pending_ == 0; });
  job_ = nullptr;
}

void WorkerPool::loop(int worker_id) {
  platform::configure_benchmark_thread();
  std::uint64_t seen = 0;
  for (;;) {
    const std::function<void(int)>* job = nullptr;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      start_cv_.wait(lock, [&] { return stop_ || generation_ != seen; });
      if (stop_) return;
      seen = generation_;
      job = job_;
    }
    (*job)(worker_id);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (--pending_ == 0) done_cv_.notify_one();
    }
  }
}

}  // namespace mb::detail
