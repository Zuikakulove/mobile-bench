#include <atomic>
#include <chrono>
#include <cmath>
#include <set>
#include <thread>

#include "mini_test.h"
#include "mobilebench/runner.h"
#include "tests/cpu_kernels.h"
#include "worker_pool.h"

using namespace mb;

namespace {

class FakeTest : public BenchmarkTest {
 public:
  FakeTest(std::string id, std::int64_t ref, std::uint64_t checksum, std::uint64_t expected, int* runs = nullptr)
      : id_(std::move(id)), ref_(ref), checksum_(checksum), expected_(expected), runs_(runs) {}
  TestInfo info() const override { return {id_, id_, "fake", ref_}; }
  std::uint64_t run_once() override {
    if (runs_) ++*runs_;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return checksum_;
  }
  std::uint64_t expected_checksum() const override { return expected_; }

 private:
  std::string id_;
  std::int64_t ref_;
  std::uint64_t checksum_, expected_;
  int* runs_;
};

}  // namespace

TEST(worker_pool_runs_every_worker_once_per_call) {
  detail::WorkerPool pool(5);
  for (int iter = 0; iter < 50; ++iter) {
    std::vector<std::atomic<int>> hits(5);
    pool.run([&](int w) { hits[static_cast<size_t>(w)].fetch_add(1); });
    for (auto& h : hits) EXPECT_EQ(h.load(), 1);
  }
}

TEST(unit_test_multithread_checksum_independent_of_thread_count) {
  // スレッド数が変わっても総処理量と結果は同じ
  for (int threads : {1, 2, 3, 8}) {
    detail::UnitBasedTest t({"t", "t", "cpu", 1}, std::make_unique<detail::SieveKernel>(100'000, 9'592), 13, true);
    TestContext ctx;
    ctx.threads = threads;
    t.setup(ctx);
    EXPECT_EQ(t.threads_used(), threads);
    EXPECT_EQ(t.run_once(), t.expected_checksum());
    EXPECT_EQ(t.run_once(), 9'592u * 13u);
    t.teardown();
  }
  detail::UnitBasedTest m({"m", "m", "cpu", 1}, std::make_unique<detail::MatmulKernel>(48), 10, true);
  TestContext ctx;
  ctx.threads = 4;
  m.setup(ctx);
  EXPECT_EQ(m.run_once(), m.expected_checksum());
  m.teardown();
}

TEST(runner_follows_warmup_and_measure_rule) {
  int runs = 0;
  std::vector<TestPtr> tests;
  tests.push_back(std::make_unique<FakeTest>("a", 1'000'000, 42, 42, &runs));
  std::vector<int> steps;
  Runner runner;
  SuiteResult r = runner.run_tests(std::move(tests), [&](const Progress& p) { steps.push_back(p.step); });
  EXPECT_EQ(runs, kWarmupRuns + kMeasuredRuns);
  EXPECT_EQ(r.tests.size(), 1u);
  EXPECT_EQ(r.tests[0].runs_ns.size(), static_cast<size_t>(kMeasuredRuns));
  EXPECT_TRUE(r.tests[0].warmup_ns > 0);
  EXPECT_EQ(r.tests[0].median_ns, median(r.tests[0].runs_ns));
  EXPECT_NEAR(r.tests[0].score, compute_score(1'000'000, r.tests[0].median_ns), 1e-9);
  EXPECT_TRUE(r.valid);
  EXPECT_NEAR(r.overall_score, r.tests[0].score, 1e-9);
  // 進捗は単調増加し、最後は 100%
  for (size_t i = 1; i < steps.size(); ++i) EXPECT_TRUE(steps[i] >= steps[i - 1]);
  EXPECT_EQ(steps.back(), kWarmupRuns + kMeasuredRuns);
}

TEST(runner_overall_is_geometric_mean) {
  std::vector<TestPtr> tests;
  tests.push_back(std::make_unique<FakeTest>("a", 2'000'000, 1, 1));
  tests.push_back(std::make_unique<FakeTest>("b", 8'000'000, 1, 1));
  Runner runner;
  SuiteResult r = runner.run_tests(std::move(tests), {});
  EXPECT_EQ(r.tests.size(), 2u);
  EXPECT_NEAR(r.overall_score, std::sqrt(r.tests[0].score * r.tests[1].score), 1e-6);
}

TEST(runner_marks_checksum_mismatch_invalid) {
  std::vector<TestPtr> tests;
  tests.push_back(std::make_unique<FakeTest>("good", 1'000'000, 1, 1));
  tests.push_back(std::make_unique<FakeTest>("bad", 1'000'000, 1, 2));
  Runner runner;
  SuiteResult r = runner.run_tests(std::move(tests), {});
  EXPECT_TRUE(r.tests[0].valid);
  EXPECT_TRUE(!r.tests[1].valid);
  EXPECT_EQ(r.tests[1].score, 0.0);
  EXPECT_TRUE(!r.valid);
  EXPECT_EQ(r.overall_score, 0.0);
  EXPECT_TRUE(to_json(r).find("\"valid\": false") != std::string::npos);
}

TEST(runner_can_be_cancelled) {
  std::vector<TestPtr> tests;
  for (int i = 0; i < 3; ++i) tests.push_back(std::make_unique<FakeTest>("t" + std::to_string(i), 1, 1, 1));
  Runner runner;
  SuiteResult r = runner.run_tests(std::move(tests), [&](const Progress& p) {
    if (p.test_index == 1) runner.cancel();
  });
  EXPECT_TRUE(r.cancelled);
  EXPECT_TRUE(!r.valid);
  EXPECT_EQ(r.tests.size(), 1u);
}

TEST(default_suite_definition) {
  auto suite = create_default_suite({});
  EXPECT_EQ(suite.size(), 4u);
  std::set<std::string> ids;
  for (const auto& t : suite) {
    ids.insert(t->info().id);
    EXPECT_TRUE(t->info().reference_ns > 0);
  }
  EXPECT_EQ(ids.size(), 4u);
  EXPECT_TRUE(ids.count("cpu.single.sieve") == 1);
  EXPECT_TRUE(ids.count("cpu.multi.matmul") == 1);

  RunConfig only;
  only.only = {"cpu.multi"};
  EXPECT_EQ(create_default_suite(only).size(), 2u);
}

TEST(build_info_reports_optimized_profile) {
  BuildInfo b = current_build_info();
  EXPECT_EQ(b.flags_profile, 1);
  EXPECT_TRUE(b.optimized);
  EXPECT_TRUE(!b.os.empty());
  EXPECT_TRUE(logical_core_count() >= 1);
}

namespace {
// expected_checksum が setup() で初めて決まるテスト (行列積と同じ形)
class LateExpectedTest : public BenchmarkTest {
 public:
  TestInfo info() const override { return {"late", "late", "fake", 1'000'000}; }
  void setup(const TestContext&) override { expected_ = 777; }
  std::uint64_t run_once() override { return 777; }
  std::uint64_t expected_checksum() const override { return expected_; }

 private:
  std::uint64_t expected_ = 0;
};
}  // namespace

TEST(runner_reads_expected_checksum_after_setup) {
  std::vector<TestPtr> tests;
  tests.push_back(std::make_unique<LateExpectedTest>());
  Runner runner;
  SuiteResult r = runner.run_tests(std::move(tests), {});
  EXPECT_TRUE(r.tests[0].valid);
  EXPECT_EQ(r.tests[0].expected_checksum, 777u);
}

// ---- 計測内訳 (テレメトリ) -------------------------------------------------------

#include "clock_probe.h"

TEST(single_thread_telemetry_records_each_unit) {
  detail::UnitBasedTest t({"t", "t", "cpu", 1}, std::make_unique<detail::SieveKernel>(100'000, 9'592), 5, false);
  TestContext ctx;
  t.setup(ctx);
  t.before_run();
  EXPECT_EQ(t.run_once(), t.expected_checksum());
  t.after_run();
  RunTelemetry tel = t.take_telemetry();
  EXPECT_EQ(tel.unit_ns.size(), 5u);
  EXPECT_EQ(tel.unit_cpus.size(), 5u);
  EXPECT_EQ(tel.workers.size(), 1u);
  EXPECT_EQ(tel.workers[0].units, 5);
  for (auto ns : tel.unit_ns) EXPECT_TRUE(ns > 0);
#if defined(__linux__)
  EXPECT_TRUE(!tel.workers[0].cpus.empty());
#endif
#if defined(__x86_64__) || defined(__aarch64__) || defined(__arm64__)
  EXPECT_TRUE(tel.workers[0].ghz_before > 0.0);
  EXPECT_TRUE(tel.workers[0].ghz_after > 0.0);
#endif
  t.teardown();
}

TEST(multi_thread_telemetry_accounts_for_all_units) {
  detail::UnitBasedTest t({"t", "t", "cpu", 1}, std::make_unique<detail::SieveKernel>(100'000, 9'592), 23, true);
  TestContext ctx;
  ctx.threads = 3;
  t.setup(ctx);
  t.before_run();
  EXPECT_EQ(t.run_once(), t.expected_checksum());
  t.after_run();
  RunTelemetry tel = t.take_telemetry();
  EXPECT_EQ(tel.workers.size(), 3u);
  int units = 0;
  for (const auto& w : tel.workers) {
    units += w.units;
    EXPECT_TRUE(w.busy_ns >= 0);
  }
  EXPECT_EQ(units, 23);
  EXPECT_TRUE(tel.unit_ns.empty());
  t.teardown();
}

TEST(clock_probe_is_plausible) {
  const double ghz = detail::estimate_clock_ghz();
#if defined(__x86_64__) || defined(__aarch64__) || defined(__arm64__)
  // 即値加算の消去などで値が跳ね上がっていないこと (現実の CPU は 0.3〜7GHz 程度)
  EXPECT_TRUE(ghz > 0.3 && ghz < 7.0);
#else
  EXPECT_EQ(ghz, 0.0);
#endif
}

TEST(runner_records_run_details_including_warmup) {
  std::vector<TestPtr> tests;
  tests.push_back(std::make_unique<FakeTest>("a", 1'000'000, 1, 1));
  Runner runner;
  SuiteResult r = runner.run_tests(std::move(tests), {});
  EXPECT_EQ(r.tests[0].run_details.size(), static_cast<size_t>(kWarmupRuns + kMeasuredRuns));
  EXPECT_TRUE(r.tests[0].run_details[0].warmup);
  EXPECT_TRUE(!r.tests[0].run_details.back().warmup);
  // run_details の本計測分は runs_ns と一致する
  for (int i = 0; i < kMeasuredRuns; ++i) {
    EXPECT_EQ(r.tests[0].run_details[static_cast<size_t>(kWarmupRuns + i)].ns, r.tests[0].runs_ns[static_cast<size_t>(i)]);
  }
  const std::string json = to_json(r, false);
  EXPECT_TRUE(json.find("\"schema_version\":2") != std::string::npos);
  EXPECT_TRUE(json.find("\"run_details\":[{\"warmup\":true") != std::string::npos);
  EXPECT_TRUE(json.find("\"cpu_topology\":{\"source\":") != std::string::npos);
  EXPECT_TRUE(json.find("\"diagnostics\":{") == std::string::npos);  // 診断モードでなければ出さない
}
