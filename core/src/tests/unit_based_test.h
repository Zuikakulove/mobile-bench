// 「固定数の作業単位 (unit) を処理する」形式のテストの共通実装。
//
// シングルスレッド版とマルチスレッド版は同じカーネル (UnitKernel) を使う。
//   - シングル: units 個を 1 スレッドで順に処理
//   - マルチ  : units 個を論理コア数分のワーカーが取り合って処理
//
// マルチスレッドの総処理量 (units) はコア数に依存しない固定値にしてある。
// こうしないと「コアが多い端末ほど仕事が多い」ことになり、
// 端末間・OS 間でスコアが比較できなくなるため。
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "mobilebench/benchmark_test.h"
#include "../clock_probe.h"
#include "../platform.h"
#include "../worker_pool.h"

namespace mb::detail {

class UnitKernel {
 public:
  virtual ~UnitKernel() = default;
  // ワーカーごとの作業領域を確保する (計測対象外)
  virtual void prepare(int workers) = 0;
  // 1 単位の処理を worker の作業領域で実行し、チェックサムを返す (計測対象)
  virtual std::uint64_t run_unit(int worker) = 0;
  // 1 単位あたりの正しいチェックサム
  virtual std::uint64_t unit_checksum() const = 0;
  virtual void release() {}
};

class UnitBasedTest final : public BenchmarkTest {
 public:
  UnitBasedTest(TestInfo info, std::unique_ptr<UnitKernel> kernel, int units, bool multithread)
      : info_(std::move(info)), kernel_(std::move(kernel)), units_(units), multithread_(multithread) {}

  TestInfo info() const override { return info_; }
  int threads_used() const override { return workers_; }

  void setup(const TestContext& ctx) override {
    workers_ = multithread_ ? (ctx.threads > 0 ? ctx.threads : 1) : 1;
    kernel_->prepare(workers_);
    partial_.assign(static_cast<size_t>(workers_), Partial{});
    // 計測区間内でメモリ確保が起きないよう予め確保しておく
    unit_ns_.reserve(static_cast<size_t>(units_));
    unit_cpus_.reserve(static_cast<size_t>(units_));
    if (multithread_) pool_ = std::make_unique<WorkerPool>(workers_);
  }

  // 診断専用: 作業単位の合間に 5ms ごとに短いクロック推定を挟み、負荷中のクロックを記録する。
  // 計測時間に約 0.3% 加わるため、スコア対象のテストでは使わない
  void enable_inload_probe(bool enabled) { inload_probe_ = enabled; }

  void before_run() override { probe_clocks(/*after=*/false); }
  void after_run() override { probe_clocks(/*after=*/true); }

  std::uint64_t run_once() override {
    if (!multithread_) {
      // シングルスレッド: 作業単位ごとの時間と CPU 番号を記録 (取得コストは数十 ns)
      unit_ns_.clear();
      unit_cpus_.clear();
      LoadProbe probe;
      std::uint64_t sum = 0;
      for (int u = 0; u < units_; ++u) {
        const int cpu = platform::current_cpu();
        const std::int64_t t0 = now_ns();
        sum += kernel_->run_unit(0);
        const std::int64_t t1 = now_ns();
        unit_ns_.push_back(t1 - t0);
        unit_cpus_.push_back(cpu);
        if (inload_probe_) probe.maybe_sample(t1);
      }
      probe.store(partial_[0]);
      return sum;
    }
    std::atomic<int> next{0};
    pool_->run([&](int worker) {
      Partial& p = partial_[static_cast<size_t>(worker)];
      std::uint64_t local = 0;
      int units = 0;
      std::int64_t busy = 0;
      std::uint64_t cpu_mask = 0;
      LoadProbe probe;
      while (next.fetch_add(1, std::memory_order_relaxed) < units_) {
        const int cpu = platform::current_cpu();
        if (cpu >= 0 && cpu < 64) cpu_mask |= (std::uint64_t{1} << cpu);
        const std::int64_t t0 = now_ns();
        local += kernel_->run_unit(worker);
        const std::int64_t t1 = now_ns();
        busy += t1 - t0;
        ++units;
        if (inload_probe_) probe.maybe_sample(t1);
      }
      probe.store(p);
      p.value = local;
      p.units = units;
      p.busy_ns = busy;
      p.cpu_mask = cpu_mask;
    });
    std::uint64_t sum = 0;
    for (const auto& p : partial_) sum += p.value;
    return sum;
  }

  RunTelemetry take_telemetry() override {
    RunTelemetry t;
    if (!multithread_) {
      t.unit_ns = unit_ns_;
      t.unit_cpus = unit_cpus_;
      WorkerTelemetry w;
      w.units = units_;
      for (auto ns : unit_ns_) w.busy_ns += ns;
      for (int cpu : unit_cpus_) {
        if (cpu >= 0 && std::find(w.cpus.begin(), w.cpus.end(), cpu) == w.cpus.end()) w.cpus.push_back(cpu);
      }
      std::sort(w.cpus.begin(), w.cpus.end());
      w.ghz_before = partial_[0].ghz_before;
      w.ghz_after = partial_[0].ghz_after;
      w.ghz_load_avg = partial_[0].ghz_load_avg;
      w.ghz_load_max = partial_[0].ghz_load_max;
      t.workers.push_back(std::move(w));
      return t;
    }
    for (size_t i = 0; i < partial_.size(); ++i) {
      const Partial& p = partial_[i];
      WorkerTelemetry w;
      w.worker = static_cast<int>(i);
      w.units = p.units;
      w.busy_ns = p.busy_ns;
      for (int cpu = 0; cpu < 64; ++cpu) {
        if (p.cpu_mask & (std::uint64_t{1} << cpu)) w.cpus.push_back(cpu);
      }
      w.ghz_before = p.ghz_before;
      w.ghz_after = p.ghz_after;
      w.ghz_load_avg = p.ghz_load_avg;
      w.ghz_load_max = p.ghz_load_max;
      t.workers.push_back(std::move(w));
    }
    return t;
  }

  std::uint64_t expected_checksum() const override {
    return kernel_->unit_checksum() * static_cast<std::uint64_t>(units_);
  }

  void teardown() override {
    pool_.reset();
    kernel_->release();
  }

 private:
  struct alignas(64) Partial {  // false sharing 回避
    std::uint64_t value = 0;
    int units = 0;
    std::int64_t busy_ns = 0;
    std::uint64_t cpu_mask = 0;
    double ghz_before = 0.0;
    double ghz_after = 0.0;
    double ghz_load_avg = 0.0;
    double ghz_load_max = 0.0;
  };

  // 負荷中のクロック推定 (5ms ごと)
  struct LoadProbe {
    std::int64_t last_ns = 0;
    double sum = 0.0;
    double max = 0.0;
    int count = 0;
    void maybe_sample(std::int64_t now) {
      if (now - last_ns < 5'000'000) return;
      const double ghz = quick_clock_ghz();
      sum += ghz;
      max = std::max(max, ghz);
      ++count;
      last_ns = now_ns();
    }
    void store(Partial& p) const {
      p.ghz_load_avg = count > 0 ? sum / count : 0.0;
      p.ghz_load_max = max;
    }
  };

  static std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  // 各ワーカー (シングルなら呼び出しスレッド) で実効クロックを推定する
  void probe_clocks(bool after) {
    auto store = [&](int worker) {
      const double ghz = estimate_clock_ghz();
      Partial& p = partial_[static_cast<size_t>(worker)];
      (after ? p.ghz_after : p.ghz_before) = ghz;
    };
    if (multithread_) {
      pool_->run(store);
    } else {
      store(0);
    }
  }

  TestInfo info_;
  std::unique_ptr<UnitKernel> kernel_;
  int units_;
  bool multithread_;
  int workers_ = 1;
  std::vector<Partial> partial_;
  std::vector<std::int64_t> unit_ns_;
  std::vector<int> unit_cpus_;
  std::unique_ptr<WorkerPool> pool_;
  bool inload_probe_ = false;
};

}  // namespace mb::detail
