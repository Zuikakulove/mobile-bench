// 固定時間方式の計測の部品 (docs/DESIGN.md §5.1。docs/PHASE1_PLAN.md P3.6)。UnitBasedTest の置き換え。
//
// ウォームアップの窓と本計測の窓を、1 回の連続した負荷として実行する。
//   - シングル: 呼び出しスレッドが、自分の単位を最後の窓の境目まで繰り返す
//   - マルチ  : 論理コア数ぶんのワーカーが、それぞれ自分の単位を繰り返す (共有カウンタから取り合わない)
// 窓の数え方は window_recorder.h にある。窓と窓の間では、何も挟まない。
//
// いまは独立したクラスで、既定スイート・Runner・診断モードからは使っていない (スコア対象は UnitBasedTest のまま)。
// 固定時間方式への切り替え (0.3.0) で、BenchmarkTest を実装する形にして既定スイートから使う。
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "mobilebench/benchmark_test.h"
#include "../clock_probe.h"
#include "../platform.h"
#include "../window_recorder.h"
#include "../worker_pool.h"
#include "timed_kernels.h"

namespace mb::detail {

// run_windows の結果
struct WindowedRun {
  std::vector<WindowOutput> windows;  // ウォームアップの窓 + 本計測の窓 (この順)
  bool checksum_ok = false;           // 全ワーカーの照合が通ったか (計測の外で完了させた分を含む)
};

class TimedUnitTest {
 public:
  TimedUnitTest(std::unique_ptr<TimedKernel> kernel, bool multithread)
      : kernel_(std::move(kernel)), multithread_(multithread) {}

  // 実際に使うワーカー数 (setup の後に有効)
  int threads_used() const { return workers_; }

  // 計測の外の準備。ワーカー数は現行と同じ (マルチは ctx.threads、シングルは 1)。コアには固定しない
  void setup(const TestContext& ctx) {
    workers_ = multithread_ ? (ctx.threads > 0 ? ctx.threads : 1) : 1;
    kernel_->prepare(workers_);
    load_.assign(static_cast<size_t>(workers_), LoadClock{});
    if (multithread_) pool_ = std::make_unique<WorkerPool>(workers_);
  }

  // 診断専用: 単位の合間に 5ms ごとに短いクロック推定を挟み、負荷中のクロックを記録する。
  // 推定の時間は次の単位の時間に入る (計測時間に約 0.3% 加わる) ので、スコア対象のテストでは使わない
  void enable_inload_probe(bool enabled) { inload_probe_ = enabled; }

  // warmup_windows + measured_windows 個の窓 (1 つの長さは window_ns) を、止めずに続けて実行する。
  // 最後の窓の境目を過ぎたワーカーは、実行中の単位を完了して止まり、計測の外で途中のまとまりを完了させる。
  // 引数が正しくないとき (window_ns <= 0、窓の合計が 0 以下) は、空の結果 (checksum_ok = false) を返す
  WindowedRun run_windows(std::int64_t window_ns, int warmup_windows, int measured_windows) {
    WindowedRun result;
    if (window_ns <= 0 || warmup_windows < 0 || measured_windows < 0) return result;
    const int windows = warmup_windows + measured_windows;
    if (windows <= 0) return result;

    // 計測の外: ワーカーごとの記録の領域と、境目の配列を用意する
    windows_ = windows;
    records_.assign(static_cast<size_t>(workers_) * static_cast<size_t>(windows), WorkerWindow{});
    bounds_.assign(static_cast<size_t>(windows), 0);
    for (auto& l : load_) l = LoadClock{};
    const std::function<void(int)> job = [this](int worker) { run_worker(worker); };

    // 境目は全ワーカーで共通。t0 は、ワーカーを起こす直前に読む
    const std::int64_t t0 = now_ns();
    for (int k = 0; k < windows; ++k) bounds_[static_cast<size_t>(k)] = t0 + (k + 1) * window_ns;
    if (multithread_) {
      pool_->run(job);
    } else {
      job(0);
    }

    // 計測の外: 窓ごとの集計と、照合の結果
    result.windows.reserve(static_cast<size_t>(windows));
    for (int k = 0; k < windows; ++k) result.windows.push_back(combine_window(records_.data(), workers_, windows, k));
    result.checksum_ok = true;
    for (int w = 0; w < workers_; ++w) result.checksum_ok = result.checksum_ok && kernel_->checksum_ok(w);
    return result;
  }

  // 直近の run_windows の、ワーカー worker の窓 k の記録 (k = 0 が最初のウォームアップの窓)
  const WorkerWindow& worker_window(int worker, int k) const {
    return records_[static_cast<size_t>(worker) * static_cast<size_t>(windows_) + static_cast<size_t>(k)];
  }
  // 直近の run_windows の窓の数
  int window_count() const { return windows_; }

  // 直近の run_windows の、負荷中のクロック [GHz] (enable_inload_probe のときだけ。なければ 0)
  double load_ghz_avg(int worker) const { return load_[static_cast<size_t>(worker)].avg; }
  double load_ghz_max(int worker) const { return load_[static_cast<size_t>(worker)].max; }

  std::uint64_t expected_checksum() const { return kernel_->expected_checksum(); }
  // ワーカーが最後に照合した値。食い違いがあったワーカーがいれば、(番号の若いワーカーが) 最初に食い違った値
  std::uint64_t observed_checksum() const {
    for (int w = 0; w < workers_; ++w) {
      if (!kernel_->checksum_ok(w)) return kernel_->observed_checksum(w);
    }
    return workers_ > 0 ? kernel_->observed_checksum(0) : 0;
  }

  TimedKernel& kernel() { return *kernel_; }

  void teardown() {
    pool_.reset();
    kernel_->release();
  }

 private:
  struct alignas(64) LoadClock {  // ワーカーごと。false sharing を避ける
    double avg = 0.0;
    double max = 0.0;
  };

  // 負荷中のクロック推定 (5ms ごと)。UnitBasedTest のものと同じ
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
    void store(LoadClock& l) const {
      l.avg = count > 0 ? sum / count : 0.0;
      l.max = max;
    }
  };

  static std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  // ワーカー 1 本の処理。時刻と CPU 番号の読み取りは、1 単位につき 1 回 (単位の終わり = 次の単位の始まり)
  void run_worker(int worker) {
    kernel_->begin(worker);
    WindowRecorder recorder;
    LoadProbe probe;
    WorkerWindow* out = records_.data() + static_cast<size_t>(worker) * static_cast<size_t>(windows_);
    recorder.reset(bounds_.data(), windows_, out, now_ns());
    for (;;) {
      const int cpu = platform::current_cpu();
      kernel_->run_unit(worker);
      const std::int64_t now = now_ns();
      if (!recorder.on_unit_done(now, cpu)) break;
      if (inload_probe_) probe.maybe_sample(now);
    }
    // ここから計測の外。ほかのワーカーの最後の単位が終わるのを待たずに進む
    probe.store(load_[static_cast<size_t>(worker)]);
    kernel_->finish(worker);
  }

  std::unique_ptr<TimedKernel> kernel_;
  bool multithread_;
  int workers_ = 1;
  int windows_ = 0;
  std::vector<WorkerWindow> records_;  // records_[worker * windows_ + k]
  std::vector<std::int64_t> bounds_;   // 窓の境目の時刻
  std::vector<LoadClock> load_;
  std::unique_ptr<WorkerPool> pool_;
  bool inload_probe_ = false;
};

}  // namespace mb::detail
