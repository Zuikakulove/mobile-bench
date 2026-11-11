// 診断モード用: 各コアの周波数 (OS 報告値) を一定間隔で記録する。
// sysfs を読めない環境 (iOS、SELinux で拒否された端末) では何もしない。
// 1 回の読み取りは数十 µs 程度で、100ms 間隔なら計測への影響は 0.1% 未満。
#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "mobilebench/telemetry.h"

namespace mb::detail {

class FreqSampler {
 public:
  FreqSampler() = default;
  ~FreqSampler() { stop(); }
  FreqSampler(const FreqSampler&) = delete;
  FreqSampler& operator=(const FreqSampler&) = delete;

  // t0_ns: タイムスタンプの基準 (steady_clock)
  void start(int cpu_count, int interval_ms, std::int64_t t0_ns);
  FreqSamples stop();

 private:
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
  FreqSamples samples_;
};

}  // namespace mb::detail
