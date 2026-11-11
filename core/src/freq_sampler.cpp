#include "freq_sampler.h"

#include <chrono>

#include "platform.h"

namespace mb::detail {

namespace {
constexpr size_t kMaxSamples = 5000;

std::int64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

void FreqSampler::start(int cpu_count, int interval_ms, std::int64_t t0_ns) {
  if (thread_.joinable()) return;
  samples_ = FreqSamples{};
  samples_.interval_ms = interval_ms;
  if (platform::read_cpu_cur_khz(cpu_count).empty()) return;  // 読めない環境
  stop_ = false;
  thread_ = std::thread([this, cpu_count, interval_ms, t0_ns] {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
      lock.unlock();
      auto khz = platform::read_cpu_cur_khz(cpu_count);
      const std::int64_t t_ms = (now_ns() - t0_ns) / 1'000'000;
      lock.lock();
      if (samples_.t_ms.size() < kMaxSamples) {
        samples_.t_ms.push_back(t_ms);
        samples_.khz.push_back(std::move(khz));
      }
      cv_.wait_for(lock, std::chrono::milliseconds(interval_ms), [this] { return stop_; });
    }
  });
}

FreqSamples FreqSampler::stop() {
  if (thread_.joinable()) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
  }
  return samples_;
}

}  // namespace mb::detail
