#include "diagnostics.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "mobilebench/runner.h"
#include "tests/cpu_kernels.h"

namespace mb::detail {

namespace {

constexpr int kSizes[] = {64, 128, 192, 256, 384, 576, 768};
constexpr double kStFlopsPerRun = 4e8;  // シングル 1 回あたりの演算量 (約 0.4 GFLOP)
constexpr int kMtFactor = 8;            // マルチはシングルの 8 倍の総処理量
constexpr int kWarmup = 1;
constexpr int kRuns = 3;

std::int64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct Measured {
  std::vector<std::int64_t> runs_ns;
  bool valid = true;
  std::vector<WorkerTelemetry> workers;
};

Measured measure(int n, int units, int threads, bool multithread, const std::atomic<bool>& cancelled,
                 const std::function<void(const std::string&, bool)>& on_step) {
  Measured m;
  UnitBasedTest test({"diag.matmul", "diag", "diag", 1}, std::make_unique<MatmulKernel>(n), units, multithread);
  TestContext ctx;
  ctx.threads = threads;
  test.setup(ctx);
  const std::string label = "Diagnostics: matmul n=" + std::to_string(n) + (multithread ? " (multi)" : " (single)");
  for (int r = 0; r < kWarmup + kRuns && !cancelled.load(); ++r) {
    on_step(label, r < kWarmup);
    test.before_run();
    const std::int64_t t0 = now_ns();
    const std::uint64_t checksum = test.run_once();
    const std::int64_t elapsed = now_ns() - t0;
    test.after_run();
    if (checksum != test.expected_checksum()) m.valid = false;
    if (r >= kWarmup) m.runs_ns.push_back(elapsed);
    m.workers = test.take_telemetry().workers;
  }
  test.teardown();
  return m;
}

}  // namespace

int matmul_sweep_steps() {
  return static_cast<int>(sizeof(kSizes) / sizeof(kSizes[0])) * 2 * (kWarmup + kRuns);
}

std::vector<MatmulSweepPoint> run_matmul_sweep(int threads, const std::atomic<bool>& cancelled,
                                               const std::function<void(const std::string&, bool)>& on_step) {
  std::vector<MatmulSweepPoint> points;
  for (int n : kSizes) {
    if (cancelled.load()) break;
    const double flops_per_unit = 2.0 * n * n * static_cast<double>(n);
    MatmulSweepPoint p;
    p.n = n;
    p.threads = threads;
    p.st_units = std::max(1, static_cast<int>(std::llround(kStFlopsPerRun / flops_per_unit)));
    // ワーカー間の偏りを抑えるため、最低でもスレッド数 x 4 単位にする (GFLOPS で比較するので総量が違ってよい)
    p.mt_units = std::max(p.st_units * kMtFactor, threads * 4);
    const std::int64_t matrix_bytes = static_cast<std::int64_t>(n) * n * static_cast<std::int64_t>(sizeof(double));
    p.st_working_set_bytes = matrix_bytes * 3;
    p.mt_working_set_bytes = matrix_bytes * (2 + threads);

    const Measured st = measure(n, p.st_units, threads, false, cancelled, on_step);
    const Measured mt = measure(n, p.mt_units, threads, true, cancelled, on_step);
    if (cancelled.load()) break;

    p.st_runs_ns = st.runs_ns;
    p.mt_runs_ns = mt.runs_ns;
    p.st_median_ns = median(st.runs_ns);
    p.mt_median_ns = median(mt.runs_ns);
    if (p.st_median_ns > 0) p.st_gflops = flops_per_unit * p.st_units / static_cast<double>(p.st_median_ns);
    if (p.mt_median_ns > 0) p.mt_gflops = flops_per_unit * p.mt_units / static_cast<double>(p.mt_median_ns);
    p.valid = st.valid && mt.valid;
    p.mt_workers = mt.workers;
    points.push_back(std::move(p));
  }
  return points;
}

}  // namespace mb::detail
