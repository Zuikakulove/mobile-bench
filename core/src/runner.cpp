#include "mobilebench/runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <memory>
#include <thread>

#include "mobilebench/build_profile.h"
#include "mobilebench/version.h"
#include "clock_probe.h"
#include "diagnostics.h"
#include "freq_sampler.h"
#include "platform.h"
#include "worker_pool.h"

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

namespace {

double mean_positive(const std::vector<double>& v) {
  double sum = 0.0;
  int n = 0;
  for (double x : v) {
    if (x > 0) {
      sum += x;
      ++n;
    }
  }
  return n > 0 ? sum / n : 0.0;
}

// 全コアに load_ms だけ負荷をかけてクロックを上げ、その直後の実効クロック (全スレッド平均) を返す。
// 状態の判定に使う。OS に依存しない (iOS でも同じ方法で判定できる)
double loaded_probe(detail::WorkerPool& pool, int load_ms) {
  std::vector<double> ghz(static_cast<size_t>(pool.size()), 0.0);
  pool.run([&](int w) {
    detail::busy_spin_ms(load_ms);
    ghz[static_cast<size_t>(w)] = detail::estimate_clock_ghz();
  });
  return mean_positive(ghz);
}

// cancelled を見ながら眠る
void sleep_ms(int ms, const std::atomic<bool>& cancelled) {
  for (int slept = 0; slept < ms && !cancelled.load(); slept += 50) {
    std::this_thread::sleep_for(std::chrono::milliseconds(std::min(50, ms - slept)));
  }
}

double spread_of(const std::vector<std::int64_t>& runs) {
  if (runs.empty()) return 0.0;
  const auto [lo, hi] = std::minmax_element(runs.begin(), runs.end());
  const std::int64_t med = median(runs);
  return med > 0 ? static_cast<double>(*hi - *lo) / static_cast<double>(med) : 0.0;
}

// 各回 (本計測) の実効クロック = 前後の推定値のスレッド平均。その中央値
double test_ghz(const TestResult& r) {
  std::vector<double> per_run;
  for (const auto& d : r.run_details) {
    if (d.warmup) continue;
    std::vector<double> v;
    for (const auto& w : d.telemetry.workers) {
      v.push_back(w.ghz_before);
      v.push_back(w.ghz_after);
    }
    const double g = mean_positive(v);
    if (g > 0) per_run.push_back(g);
  }
  if (per_run.empty()) return 0.0;
  std::sort(per_run.begin(), per_run.end());
  return per_run[per_run.size() / 2];
}

struct PhasePlan {
  std::string name;
  bool gate = false;          // テスト前に回復を待つ
  bool precondition = false;  // 段階の最初に事前負荷をかける
};

}  // namespace

SuiteResult Runner::run(const ProgressCallback& progress) {
  return run_impl([this] { return create_default_suite(config_); }, progress, kWarmupRuns, kMeasuredRuns,
                  /*full_procedure=*/true, config_.diagnostics);
}

std::string Runner::run_json(const ProgressCallback& progress) { return to_json(run(progress)); }

SuiteResult Runner::run_with_factory(const SuiteFactory& factory, const ProgressCallback& progress) {
  return run_impl(factory, progress, kWarmupRuns, kMeasuredRuns, /*full_procedure=*/true, /*diagnostics=*/false);
}

SuiteResult Runner::run_tests(std::vector<TestPtr> tests, const ProgressCallback& progress,
                              int warmup_runs, int measured_runs) {
  auto shared = std::make_shared<std::vector<TestPtr>>(std::move(tests));
  return run_impl([shared] { return std::move(*shared); }, progress, warmup_runs, measured_runs,
                  /*full_procedure=*/false, /*diagnostics=*/false);
}

SuiteResult Runner::run_impl(const SuiteFactory& factory, const ProgressCallback& progress, int warmup_runs,
                             int measured_runs, bool full_procedure, bool diagnostics) {
  const std::int64_t suite_t0 = now_ns();
  const RunPolicy& policy = config_.policy;
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

  std::vector<PhasePlan> plans;
  if (full_procedure) {
    if (policy.peak_phase) plans.push_back({kPhasePeak, /*gate=*/true, /*precondition=*/false});
    if (policy.sustained_phase) plans.push_back({kPhaseSustained, /*gate=*/false, /*precondition=*/true});
  } else {
    plans.push_back({kPhasePeak, false, false});
  }

  // 段階ごとにテストを作る (同じ定義のテストを別インスタンスで測る)
  std::vector<std::vector<TestPtr>> phase_tests;
  for (size_t i = 0; i < plans.size(); ++i) phase_tests.push_back(factory());

  const int runs_per_test = warmup_runs + measured_runs;
  const auto clusters = detail::cpu_clusters(suite.topology, suite.logical_cores);
  Progress p;
  p.total_steps = 0;
  for (size_t i = 0; i < plans.size(); ++i) {
    const int n = static_cast<int>(phase_tests[i].size());
    p.total_steps += n * runs_per_test + (plans[i].gate ? n : 0) + (plans[i].precondition ? 1 : 0);
  }
  if (full_procedure) p.total_steps += 1;  // 基準クロックの測定
  if (diagnostics) p.total_steps += detail::diagnostics_steps(clusters.size());

  auto report = [&] {
    if (progress) progress(p);
  };

  // 状態判定・事前負荷用のスレッドプール (計測区間では使わない)
  std::unique_ptr<detail::WorkerPool> load_pool;
  if (full_procedure) {
    load_pool = std::make_unique<detail::WorkerPool>(ctx.threads);
    p.phase = "prepare";
    p.test_name = "Measuring baseline clock";
    report();
    suite.baseline_ghz = loaded_probe(*load_pool, policy.probe_load_ms);
    ++p.step;
  }

  bool aborted = false;
  for (size_t pi = 0; pi < plans.size() && !aborted && !cancelled_.load(); ++pi) {
    const PhasePlan& plan = plans[pi];
    std::vector<TestPtr>& tests = phase_tests[pi];
    p.phase = plan.name;
    p.test_count = static_cast<int>(tests.size());
    p.test_index = 0;
    p.wait_ms = -1;
    p.attempt = 1;

    // sustained: 全コアに負荷をかけ、クロックが落ち着くまで続ける
    if (plan.precondition && load_pool) {
      p.test_id = "precondition";
      p.test_name = "Preconditioning (all-core load)";
      double prev = 0.0;
      int elapsed = 0;
      while (!cancelled_.load()) {
        p.wait_ms = elapsed;
        report();
        const double g = loaded_probe(*load_pool, policy.precondition_chunk_ms);
        elapsed += policy.precondition_chunk_ms;
        suite.precondition.ghz = g;
        const bool settled = prev > 0 && g > 0 && std::abs(g - prev) / prev <= policy.precondition_settle;
        if (elapsed >= policy.precondition_min_ms && (settled || g <= 0)) {
          suite.precondition.settled = settled;
          break;
        }
        if (elapsed >= policy.precondition_max_ms) break;
        prev = g;
      }
      suite.precondition.ms = elapsed;
      p.wait_ms = -1;
      ++p.step;
    }

    std::vector<double> scores;
    bool phase_valid = !tests.empty();
    bool phase_stable = true;
    for (size_t ti = 0; ti < tests.size() && !cancelled_.load(); ++ti) {
      BenchmarkTest& test = *tests[ti];
      const TestInfo info = test.info();
      p.test_index = static_cast<int>(ti);
      p.test_id = info.id;
      const int step_at_test_start = p.step;

      TestResult best;
      bool have_best = false;
      int attempts = 0;
      for (int attempt = 1; attempt <= 1 + policy.max_retries && !cancelled_.load(); ++attempt) {
        attempts = attempt;
        p.attempt = attempt;
        p.step = step_at_test_start;

        // peak: 状態の回復を待つ (最低 min_rest、以後 check_interval ごとに確認)
        int waited = -1;
        bool recovered = true;
        double recovery_ghz = 0.0;
        if (plan.gate && load_pool) {
          p.test_name = info.name;
          waited = 0;
          recovered = false;
          p.wait_ms = 0;
          report();
          sleep_ms(policy.min_rest_ms, cancelled_);
          waited = policy.min_rest_ms;
          while (!cancelled_.load()) {
            recovery_ghz = loaded_probe(*load_pool, policy.probe_load_ms);
            if (suite.baseline_ghz <= 0 || recovery_ghz >= suite.baseline_ghz * policy.recovered_ratio) {
              recovered = true;
              break;
            }
            if (waited >= policy.max_wait_ms) break;
            p.wait_ms = waited;
            report();
            sleep_ms(policy.check_interval_ms, cancelled_);
            waited += policy.check_interval_ms;
          }
          p.wait_ms = -1;
          ++p.step;
        }

        TestResult r;
        r.id = info.id;
        r.name = info.name;
        r.category = info.category;
        r.reference_ns = info.reference_ns;
        r.phase = plan.name;
        r.wait_ms = waited;
        r.recovered = recovered;
        r.recovery_ghz = recovery_ghz;
        p.test_name = info.name;

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
            test.after_run();  // クロック推定 (計測区間外)
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

        if (!completed && r.error.empty()) break;  // 中断

        r.median_ns = median(r.runs_ns);
        r.valid = all_match && completed;
        r.score = r.valid ? compute_score(r.reference_ns, r.median_ns) : 0.0;
        r.spread = spread_of(r.runs_ns);
        r.stable = r.valid && r.spread <= policy.stable_spread;
        r.ghz = test_ghz(r);

        // 最も安定していた回 (spread が最小) を採用する
        if (!have_best || (r.valid && !best.valid) || (r.valid == best.valid && r.spread < best.spread)) {
          best = std::move(r);
          have_best = true;
        }
        if (best.stable || !best.error.empty() || !full_procedure) break;
      }

      if (!have_best) {  // 中断された
        aborted = true;
        break;
      }
      best.attempts = attempts;
      phase_valid = phase_valid && best.valid;
      phase_stable = phase_stable && best.stable;
      scores.push_back(best.score);
      suite.tests.push_back(std::move(best));
    }

    PhaseResult pr;
    pr.name = plan.name;
    pr.valid = phase_valid && scores.size() == tests.size() && !cancelled_.load();
    pr.stable = phase_stable;
    pr.score = pr.valid ? geometric_mean(scores) : 0.0;
    suite.phases.push_back(pr);
  }
  load_pool.reset();

  // 診断モード: スコア対象外の計測
  if (diagnostics && !cancelled_.load()) {
    p.phase = "diagnostics";
    p.test_index = 0;
    p.test_count = 0;
    p.test_id = "diagnostics";
    p.wait_ms = -1;
    p.attempt = 1;
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
  bool all_valid = !suite.phases.empty() && suite.phases.size() == plans.size();
  for (const auto& ph : suite.phases) all_valid = all_valid && ph.valid;
  suite.valid = all_valid && !suite.cancelled;
  suite.overall_score = !suite.phases.empty() && suite.phases.front().valid ? suite.phases.front().score : 0.0;
  if (suite.phases.size() >= 2 && suite.phases[0].score > 0 && suite.phases[1].valid) {
    suite.retention = suite.phases[1].score / suite.phases[0].score;
  }
  suite.summary_text = make_summary_text(suite);

  p.step = p.total_steps;
  p.warmup = false;
  p.wait_ms = -1;
  if (!suite.cancelled) report();
  return suite;
}

}  // namespace mb
