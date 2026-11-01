#include "diagnostics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <thread>

#include "clock_probe.h"
#include "mobilebench/runner.h"
#include "platform.h"
#include "tests/cpu_kernels.h"

namespace mb::detail {

namespace {

constexpr int kSizes[] = {64, 128, 192, 256, 384, 576, 768};
constexpr size_t kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);
constexpr double kStFlopsPerRun = 4e8;  // シングル 1 回あたりの演算量 (約 0.4 GFLOP)
constexpr int kMtFactor = 8;            // マルチはシングルの 8 倍の総処理量
constexpr int kWarmup = 1;
constexpr int kRuns = 3;

constexpr int kBoostIdleMs[] = {1000, 4000};  // 休止時間
constexpr int kBoostLoadMs = 2000;            // 連続負荷の長さ
constexpr size_t kBoostCount = sizeof(kBoostIdleMs) / sizeof(kBoostIdleMs[0]);

constexpr int kClusterMatmulN = 128;           // キャッシュに収まるサイズ
constexpr int kClusterSieveUnits = 6;          // 1 スレッドあたり (約 0.2〜0.35 秒)
constexpr int kClusterMatmulUnits = 300;       // 1 スレッドあたり (約 0.1〜0.3 秒)
constexpr int kMeasuresPerCluster = 4;         // sieve 1T / sieve NT / matmul 1T / matmul NT

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
                 const StepCallback& on_step) {
  Measured m;
  UnitBasedTest test({"diag.matmul", "diag", "diag", 1}, std::make_unique<MatmulKernel>(n), units, multithread);
  test.enable_inload_probe(true);
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

double average_load_ghz(const std::vector<WorkerTelemetry>& workers) {
  double sum = 0.0;
  int n = 0;
  for (const auto& w : workers) {
    if (w.ghz_load_avg > 0) {
      sum += w.ghz_load_avg;
      ++n;
    }
  }
  return n > 0 ? sum / n : 0.0;
}

}  // namespace

std::vector<std::vector<int>> cpu_clusters(const CpuTopology& t, int cpu_count) {
  if (static_cast<int>(t.capacity.size()) != cpu_count && static_cast<int>(t.max_khz.size()) != cpu_count) return {};
  // (capacity, max_khz) が同じコアを 1 クラスタとする。情報のないコアがあれば諦める
  std::map<std::pair<std::int64_t, std::int64_t>, std::vector<int>> groups;
  for (int cpu = 0; cpu < cpu_count; ++cpu) {
    const std::int64_t cap = cpu < static_cast<int>(t.capacity.size()) ? t.capacity[static_cast<size_t>(cpu)] : -1;
    const std::int64_t khz = cpu < static_cast<int>(t.max_khz.size()) ? t.max_khz[static_cast<size_t>(cpu)] : -1;
    if (cap < 0 && khz < 0) return {};
    groups[{cap, khz}].push_back(cpu);
  }
  std::vector<std::vector<int>> out;
  for (auto it = groups.rbegin(); it != groups.rend(); ++it) out.push_back(it->second);  // 速いクラスタから
  return out;
}

int diagnostics_steps(size_t cluster_count) {
  const size_t groups = std::max<size_t>(cluster_count, 1);
  return static_cast<int>(kBoostCount + groups * kMeasuresPerCluster + kSizeCount * 2 * (kWarmup + kRuns));
}

BoostTrace run_boost_trace(int idle_ms, int load_ms, const std::atomic<bool>& cancelled) {
  BoostTrace trace;
  trace.idle_ms = idle_ms;
  for (int slept = 0; slept < idle_ms && !cancelled.load(); slept += 50) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  SieveKernel kernel;
  kernel.prepare(1);
  const std::int64_t start = now_ns();
  while (!cancelled.load()) {
    const std::int64_t t0 = now_ns();
    if (t0 - start >= static_cast<std::int64_t>(load_ms) * 1'000'000) break;
    BoostSample s;
    s.cpu = platform::current_cpu();
    kernel.run_unit(0);
    const std::int64_t t1 = now_ns();
    s.t_ms = (t0 - start) / 1'000'000;
    s.unit_ms = static_cast<double>(t1 - t0) / 1e6;
    s.ghz = quick_clock_ghz();
    trace.samples.push_back(s);
  }
  kernel.release();
  return trace;
}

ClusterMeasure run_cluster_measure(const std::string& kernel_name, const std::vector<std::vector<int>>& masks,
                                   int units_per_thread) {
  ClusterMeasure m;
  m.kernel = kernel_name;
  m.threads = static_cast<int>(masks.size());
  m.units_per_thread = units_per_thread;
  const bool is_matmul = kernel_name != "sieve";
  std::unique_ptr<UnitKernel> kernel;
  if (is_matmul) {
    kernel = std::make_unique<MatmulKernel>(kClusterMatmulN);
  } else {
    kernel = std::make_unique<SieveKernel>();
  }
  kernel->prepare(m.threads);

  struct alignas(64) PerThread {
    double ms_sum = 0.0;
    double ghz_sum = 0.0;
    int ghz_count = 0;
    std::uint64_t cpu_mask = 0;
    bool pinned = false;
    bool checksum_ok = true;
  };
  std::vector<PerThread> per(static_cast<size_t>(m.threads));
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  for (int i = 0; i < m.threads; ++i) {
    threads.emplace_back([&, i] {
      PerThread& r = per[static_cast<size_t>(i)];
      r.pinned = !masks[static_cast<size_t>(i)].empty() && platform::pin_current_thread(masks[static_cast<size_t>(i)]);
      platform::configure_benchmark_thread();
      ready.fetch_add(1);
      while (!go.load()) std::this_thread::yield();
      kernel->run_unit(i);  // ウォームアップ
      std::int64_t last_probe = 0;
      for (int u = 0; u < units_per_thread; ++u) {
        const int cpu = platform::current_cpu();
        if (cpu >= 0 && cpu < 64) r.cpu_mask |= std::uint64_t{1} << cpu;
        const std::int64_t t0 = now_ns();
        const std::uint64_t cs = kernel->run_unit(i);
        const std::int64_t t1 = now_ns();
        if (cs != kernel->unit_checksum()) r.checksum_ok = false;
        r.ms_sum += static_cast<double>(t1 - t0) / 1e6;
        if (t1 - last_probe >= 5'000'000) {
          r.ghz_sum += quick_clock_ghz();
          ++r.ghz_count;
          last_probe = now_ns();
        }
      }
    });
  }
  while (ready.load() < m.threads) std::this_thread::yield();
  go.store(true);
  for (auto& t : threads) t.join();
  kernel->release();

  const double flops_per_unit = is_matmul ? 2.0 * kClusterMatmulN * kClusterMatmulN * kClusterMatmulN : 0.0;
  double ms_total = 0.0, ghz_total = 0.0;
  int ghz_threads = 0;
  bool all_pinned = true;
  std::uint64_t mask = 0;
  for (const auto& r : per) {
    const double ms = r.ms_sum / units_per_thread;
    ms_total += ms;
    if (r.ghz_count > 0) {
      ghz_total += r.ghz_sum / r.ghz_count;
      ++ghz_threads;
    }
    if (is_matmul && ms > 0) m.gflops += flops_per_unit / (ms * 1e6);
    all_pinned = all_pinned && r.pinned;
    mask |= r.cpu_mask;
    if (!r.checksum_ok) m.error = "checksum mismatch";
  }
  m.pinned = all_pinned;
  m.ms_per_unit = ms_total / m.threads;
  m.ghz_load = ghz_threads > 0 ? ghz_total / ghz_threads : 0.0;
  m.mcycles_per_unit = m.ms_per_unit * m.ghz_load;
  if (is_matmul && m.ghz_load > 0) m.flops_per_cycle = (m.gflops / m.threads) / m.ghz_load;
  for (int c = 0; c < 64; ++c) {
    if (mask & (std::uint64_t{1} << c)) m.cpus.push_back(c);
  }
  return m;
}

std::vector<MatmulSweepPoint> run_matmul_sweep(int threads, const std::atomic<bool>& cancelled,
                                               const StepCallback& on_step) {
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
    p.st_ghz_load = average_load_ghz(st.workers);
    p.mt_ghz_load = average_load_ghz(mt.workers);
    if (!st.workers.empty()) p.st_cpus = st.workers.front().cpus;
    points.push_back(std::move(p));
  }
  return points;
}

void run_diagnostics(int threads, const CpuTopology& topology, const std::vector<std::vector<int>>& clusters,
                     const std::atomic<bool>& cancelled, const StepCallback& on_step, Diagnostics& out) {
  // 1. ブーストの持続 (休止の影響を見るため最初に行う)
  for (int idle : kBoostIdleMs) {
    if (cancelled.load()) return;
    on_step("Diagnostics: boost trace (idle " + std::to_string(idle) + "ms)", false);
    out.boost.push_back(run_boost_trace(idle, kBoostLoadMs, cancelled));
  }

  // 2. コアの種類ごとの性能
  std::vector<std::vector<int>> groups = clusters;
  const bool pin = !groups.empty();
  if (!pin) {  // 固定できない環境: 全コアをまとめて 1 グループ (固定しない)
    std::vector<int> all;
    for (int c = 0; c < threads; ++c) all.push_back(c);
    groups.push_back(all);
  }
  for (const auto& cpus : groups) {
    if (cancelled.load()) return;
    ClusterResult cr;
    cr.cpus = cpus;
    if (!cpus.empty()) {
      const auto idx = static_cast<size_t>(cpus.front());
      if (idx < topology.max_khz.size()) cr.max_khz = topology.max_khz[idx];
      if (idx < topology.capacity.size()) cr.capacity = topology.capacity[idx];
    }
    // 1 スレッド: クラスタ内のどこかで動かす / N スレッド: 1 スレッドずつ別のコアに固定
    const std::vector<std::vector<int>> one{pin ? cpus : std::vector<int>{}};
    std::vector<std::vector<int>> each;
    for (int c : cpus) each.push_back(pin ? std::vector<int>{c} : std::vector<int>{});
    std::string label = "Diagnostics: cluster cpu" + std::to_string(cpus.front()) + "-" + std::to_string(cpus.back());
    on_step(label + " sieve 1T", false);
    cr.measures.push_back(run_cluster_measure("sieve", one, kClusterSieveUnits));
    on_step(label + " sieve " + std::to_string(each.size()) + "T", false);
    cr.measures.push_back(run_cluster_measure("sieve", each, kClusterSieveUnits));
    on_step(label + " matmul 1T", false);
    cr.measures.push_back(run_cluster_measure("matmul128", one, kClusterMatmulUnits));
    on_step(label + " matmul " + std::to_string(each.size()) + "T", false);
    cr.measures.push_back(run_cluster_measure("matmul128", each, kClusterMatmulUnits));
    out.clusters.push_back(std::move(cr));
  }

  // 3. 行列サイズの掃引
  out.matmul_sweep = run_matmul_sweep(threads, cancelled, on_step);
}

}  // namespace mb::detail
