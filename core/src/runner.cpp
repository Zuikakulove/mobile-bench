#include "mobilebench/runner.h"

#include <chrono>
#include <exception>

#include "mobilebench/build_profile.h"
#include "mobilebench/version.h"
#include "diagnostics.h"
#include "freq_sampler.h"
#include "platform.h"

namespace mb {

namespace {

std::int64_t now_ns() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count();
}

}  // namespace

BuildInfo current_build_info() {
  BuildInfo b;
  b.os = platform::os_name();
  b.arch = platform::arch_name();
  b.compiler = platform::compiler_name();
  b.flags_profile = kFlagsProfile;
  b.flags = kFlagsProfileDescription;
#if defined(__OPTIMIZE__) && !defined(__OPTIMIZE_SIZE__)
  b.optimized = true;
#else
  b.optimized = false;
#endif
  return b;
}

int logical_core_count() { return platform::logical_cpu_count(); }

CpuTopology current_cpu_topology() { return platform::read_cpu_topology(platform::logical_cpu_count()); }

Runner::Runner(RunConfig config) : config_(std::move(config)) {}

Conditions Runner::probe_conditions() {
  if (!condition_probe_) return {};
  try {
    return condition_probe_();
  } catch (...) {
    return {{"error", "condition probe failed"}};
  }
}

SuiteResult Runner::run(const ProgressCallback& progress) {
  return run_impl(create_default_suite(config_), progress, kWarmupRuns, kMeasuredRuns, config_.diagnostics);
}

std::string Runner::run_json(const ProgressCallback& progress) { return to_json(run(progress)); }

SuiteResult Runner::run_tests(std::vector<TestPtr> tests, const ProgressCallback& progress,
                              int warmup_runs, int measured_runs) {
  return run_impl(std::move(tests), progress, warmup_runs, measured_runs, /*diagnostics=*/false);
}

SuiteResult Runner::run_impl(std::vector<TestPtr> tests, const ProgressCallback& progress, int warmup_runs,
                             int measured_runs, bool diagnostics) {
  const std::int64_t suite_t0 = now_ns();
  SuiteResult suite;
  suite.benchmark_version = kBenchmarkVersion;
  suite.build = current_build_info();
  suite.device = config_.device;
  suite.warmup_runs = warmup_runs;
  suite.measured_runs = measured_runs;
  suite.logical_cores = logical_core_count();
  suite.topology = platform::read_cpu_topology(suite.logical_cores);
  suite.diagnostics_enabled = diagnostics;

  detail::FreqSampler sampler;
  if (diagnostics) sampler.start(suite.logical_cores, /*interval_ms=*/100, suite_t0);

  TestContext ctx;
  ctx.threads = config_.threads > 0 ? config_.threads : suite.logical_cores;
  ctx.scratch_dir = config_.scratch_dir;

  const int runs_per_test = warmup_runs + measured_runs;
  Progress p;
  p.test_count = static_cast<int>(tests.size());
  const auto clusters = detail::cpu_clusters(suite.topology, suite.logical_cores);
  p.total_steps = p.test_count * runs_per_test + (diagnostics ? detail::diagnostics_steps(clusters.size()) : 0);

  auto report = [&] {
    if (progress) progress(p);
  };

  for (size_t ti = 0; ti < tests.size() && !cancelled_.load(); ++ti) {
    BenchmarkTest& test = *tests[ti];
    const TestInfo info = test.info();

    TestResult r;
    r.id = info.id;
    r.name = info.name;
    r.category = info.category;
    r.reference_ns = info.reference_ns;

    p.test_index = static_cast<int>(ti);
    p.test_id = info.id;
    p.test_name = info.name;
    p.step = static_cast<int>(ti) * runs_per_test;

    bool all_match = true;
    bool completed = false;
    try {
      test.setup(ctx);
      r.threads = test.threads_used();
      r.expected_checksum = test.expected_checksum();  // setup 後に確定する
      r.conditions_before = probe_conditions();
      int run = 0;
      for (; run < runs_per_test; ++run) {
        if (cancelled_.load()) break;
        p.warmup = run < warmup_runs;
        report();

        RunDetail detail;
        detail.warmup = run < warmup_runs;
        test.before_run();  // クロック推定 (計測区間外)
        detail.cpu_cap_khz = platform::read_cpu_cap_khz(suite.logical_cores);
        detail.cpu_khz_before = platform::read_cpu_cur_khz(suite.logical_cores);

        const std::int64_t start = now_ns();
        const std::uint64_t checksum = test.run_once();
        const std::int64_t elapsed = now_ns() - start;

        detail.cpu_khz_after = platform::read_cpu_cur_khz(suite.logical_cores);
        test.after_run();   // クロック推定 (計測区間外)
        detail.ns = elapsed;
        detail.t_start_ms = (start - suite_t0) / 1'000'000;
        detail.telemetry = test.take_telemetry();
        r.run_details.push_back(std::move(detail));

        r.checksum = checksum;
        if (checksum != r.expected_checksum) all_match = false;
        if (run < warmup_runs) {
          if (run == 0) r.warmup_ns = elapsed;
        } else {
          r.runs_ns.push_back(elapsed);
        }
        ++p.step;
      }
      completed = run == runs_per_test;
      r.conditions_after = probe_conditions();
      test.teardown();
    } catch (const std::exception& e) {
      r.error = e.what();
      all_match = false;
    } catch (...) {
      r.error = "unknown error";
      all_match = false;
    }

    if (!completed && r.error.empty()) {
      // 中断されたテストは結果に含めない
      break;
    }

    r.median_ns = median(r.runs_ns);
    r.valid = all_match && completed;
    r.score = r.valid ? compute_score(r.reference_ns, r.median_ns) : 0.0;
    suite.tests.push_back(std::move(r));
  }

  // 診断モード: スコア対象外の行列サイズ掃引
  if (diagnostics && !cancelled_.load()) {
    p.test_index = p.test_count;
    p.test_id = "diagnostics";
    suite.diagnostics.conditions_before = probe_conditions();
    try {
      detail::run_diagnostics(ctx.threads, suite.topology, clusters, cancelled_,
                              [&](const std::string& label, bool warmup) {
                                p.test_name = label;
                                p.warmup = warmup;
                                report();
                                ++p.step;
                              },
                              suite.diagnostics);
    } catch (...) {
      // 診断の失敗はスコアに影響させない
    }
    suite.diagnostics.conditions_after = probe_conditions();
  }
  suite.diagnostics.freq = sampler.stop();

  suite.cancelled = cancelled_.load();
  std::vector<double> scores;
  bool all_valid = !suite.tests.empty() && suite.tests.size() == tests.size();
  for (const auto& t : suite.tests) {
    scores.push_back(t.score);
    all_valid = all_valid && t.valid;
  }
  suite.valid = all_valid && !suite.cancelled;
  suite.overall_score = suite.valid ? geometric_mean(scores) : 0.0;
  suite.summary_text = make_summary_text(suite);

  p.step = p.total_steps;
  p.warmup = false;
  if (!suite.cancelled) report();
  return suite;
}

}  // namespace mb
