// 計測前に起動しておく固定サイズのスレッドプール。
// スレッド生成コストを計測時間に含めないために使う。
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace mb::detail {

class WorkerPool {
 public:
  // workers 個のワーカーで実行する。呼び出しスレッドがワーカー 0 を担当し、
  // workers-1 本のスレッドを起動する。
  explicit WorkerPool(int workers);
  ~WorkerPool();

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  int size() const { return workers_; }

  // fn(worker_id) を全ワーカーで並列に 1 回ずつ実行し、全員の完了を待つ。
  void run(const std::function<void(int)>& fn);

 private:
  void loop(int worker_id);

  int workers_;
  std::vector<std::thread> threads_;
  std::mutex mutex_;
  std::condition_variable start_cv_;
  std::condition_variable done_cv_;
  const std::function<void(int)>* job_ = nullptr;
  std::uint64_t generation_ = 0;
  int pending_ = 0;
  bool stop_ = false;
};

}  // namespace mb::detail
