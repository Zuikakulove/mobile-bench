// 要約の分割 (docs/DESIGN.md §5.6) と、0.2.1 で診断に足した計測のテスト。
#include <algorithm>
#include <atomic>
#include <set>
#include <string>
#include <vector>

#include "clock_probe.h"
#include "diagnostics.h"
#include "mini_test.h"
#include "mobilebench/runner.h"
#include "mobilebench/version.h"
#include "summary.h"
#include "tests/cpu_kernels.h"

using namespace mb;

namespace {

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    lines.push_back(text.substr(pos, nl - pos));
    pos = nl + 1;
  }
  return lines;
}

bool starts_with(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

WorkerTelemetry worker(int id, int units, int cpu) {
  WorkerTelemetry w;
  w.worker = id;
  w.units = units;
  w.busy_ns = 123'456'789;
  w.cpus = {cpu, (cpu + 3) % 16};
  w.ghz_before = 2.345;
  w.ghz_after = 2.456;
  return w;
}

// §5.6 の 7: テスト 9 本 x 2 段階、診断あり、論理コア 16 個 (周波数がすべて違う)
SuiteResult large_result() {
  constexpr int kCores = 16;
  SuiteResult r;
  r.benchmark_version = kBenchmarkVersion;
  r.started_utc = "2026-11-15T01:23:45Z";
  r.device = {{"app_version", "0.2.1"}, {"app_build", "123"}, {"git_sha", "abcdef0"}, {"manufacturer", "Maker"},
              {"model", "Model-X"}, {"soc_model", "SOC1234"}, {"platform", "android"}, {"os_version", "16"},
              {"os_build", "BP2A.250605.031.A3 release-keys"}, {"os_build_id", "BP2A.250605.031.A3"},
              {"os_incremental", "1234567890"}, {"os_security_patch", "2026-10-05"},
              {"os_fingerprint", "maker/model_x/model_x:16/BP2A.250605.031.A3/1234567890:user/release-keys"}};
  r.build.arch = "arm64";
  r.build.compiler = "clang 18.0.3 (https://android.googlesource.com/toolchain/llvm-project)";
  r.build.flags = "-O2 -ffp-contract=on";
  r.build.optimized = true;
  r.warmup_runs = 1;
  r.measured_runs = 5;
  r.logical_cores = kCores;
  r.diagnostics_enabled = true;
  r.phases = {{"peak", 1234.5, true, true}, {"sustained", 1100.0, true, false}};
  for (int c = 0; c < kCores; ++c) {
    r.topology.max_khz.push_back(1'000'000 + c * 123'400);
    r.topology.capacity.push_back(100 + c * 50);
  }
  const Conditions base{{"battery_level", "68"}, {"battery_temp_c", "25.0"}, {"charging", "false"},
                        {"power_save", "false"}, {"thermal_headroom_10s", "0.31"}, {"thermal_status", "NONE"}};
  for (const char* phase : {"peak", "sustained"}) {
    for (int i = 0; i < 9; ++i) {
      TestResult t;
      t.phase = phase;
      t.id = "cpu.test" + std::to_string(i) + (i % 2 ? ".multi" : ".single");
      t.threads = i % 2 ? kCores : 1;
      t.median_ns = 250'200'000;
      t.score = 999.9;
      t.spread = 0.123;
      t.attempts = 3;
      t.ghz = 2.51;
      t.wait_ms = std::string(phase) == "peak" ? 2000 : -1;
      t.recovered = i != 3;
      t.recovery_ghz = 2.2;
      t.valid = true;
      t.conditions_before = base;
      t.conditions_after = base;
      t.conditions_after["thermal_headroom_10s"] = "0.3" + std::to_string(i);
      t.conditions_after["battery_temp_c"] = "26." + std::to_string(i);
      for (int run = 0; run < 6; ++run) {
        RunDetail d;
        d.warmup = run == 0;
        d.ns = 251'100'000 + run;
        for (int c = 0; c < kCores; ++c) {
          d.cpu_khz_after.push_back(900'000 + c * 100'000);
          // 半分の回だけ、上限が最大周波数と違う
          d.cpu_cap_khz.push_back(run % 2 ? r.topology.max_khz[static_cast<size_t>(c)] : 800'000 + c * 10'000);
        }
        if (t.threads == 1) {
          d.telemetry.unit_ns = {64'600'000, 62'400'000, 62'400'000, 62'400'000};
          d.telemetry.workers.push_back(worker(0, 4, 7));
        } else {
          for (int w = 0; w < kCores; ++w) d.telemetry.workers.push_back(worker(w, 1 + w % 3, w));
        }
        t.run_details.push_back(std::move(d));
      }
      r.tests.push_back(std::move(t));
    }
  }
  auto& d = r.diagnostics;
  d.conditions_before = base;
  d.conditions_after = base;
  d.conditions_after["battery_temp_c"] = "28.0";
  for (int idle : {1000, 4000}) {
    BoostTrace b;
    b.idle_ms = idle;
    for (int t = 0; t < 2000; t += 60) b.samples.push_back({t, 62.4, 7, 2.51});
    d.boost.push_back(b);
  }
  d.clock_probe.workers = kCores;
  d.clock_probe.rest_ms = 2000;
  d.clock_probe.load_ms = 100;
  d.clock_probe.count_from_ms = 50;
  for (int i = 0; i < 12; ++i) d.clock_probe.samples.push_back({i * 2100, 2.27, 1.80, 2.52, 2.11, 1.80, 2.51});
  for (const auto& cpus : detail::cpu_clusters(r.topology, kCores)) {
    ClusterResult c;
    c.cpus = cpus;
    c.max_khz = r.topology.max_khz[static_cast<size_t>(cpus.front())];
    c.capacity = r.topology.capacity[static_cast<size_t>(cpus.front())];
    for (const char* k : {"sieve", "matmul128", "sieve1m", "matmul576"}) {
      for (int threads : {1, static_cast<int>(cpus.size())}) {
        ClusterMeasure m;
        m.kernel = k;
        m.threads = threads;
        m.pinned = true;
        m.ms_per_unit = 62.61;
        m.ghz_load = 2.5;
        m.mcycles_per_unit = 156.6;
        if (k[0] == 'm') {
          m.gflops = 5.76;
          m.flops_per_cycle = 2.3;
        }
        m.cpus = cpus;
        c.measures.push_back(m);
      }
    }
    d.clusters.push_back(c);
  }
  for (int n : {64, 128, 192, 256, 384, 576, 768}) {
    MatmulSweepPoint p;
    p.n = n;
    p.threads = kCores;
    p.st_working_set_bytes = 98304;
    p.mt_working_set_bytes = 589824;
    p.st_gflops = 7.3;
    p.mt_gflops = 30.1;
    p.valid = true;
    for (int w = 0; w < kCores; ++w) p.mt_workers.push_back(worker(w, 40 + w, w));
    p.st_ghz_load = 2.51;
    p.mt_ghz_load = 2.2;
    p.st_cpus = {7};
    d.matmul_sweep.push_back(p);
  }
  d.freq.interval_ms = 100;
  for (int s = 0; s < 50; ++s) {
    std::vector<std::int64_t> row;
    for (int c = 0; c < kCores; ++c) row.push_back(500'000 + c * 100'000 + s * 1000);
    d.freq.khz.push_back(row);
    d.freq.t_ms.push_back(s * 100);
  }
  return r;
}

// パートを検査し、見出しと終端の行を除いた本文の行を返す
std::vector<std::string> check_parts(const std::vector<std::string>& parts, const std::string& header,
                                     std::size_t max_chars) {
  std::vector<std::string> body;
  const std::string n = std::to_string(parts.size());
  for (size_t i = 0; i < parts.size(); ++i) {
    const std::string& part = parts[i];
    const std::string label = "part " + std::to_string(i + 1) + "/" + n;
    EXPECT_TRUE(part.size() <= max_chars);
    EXPECT_TRUE(!part.empty() && part.back() == '\n');
    auto lines = split_lines(part);
    EXPECT_TRUE(lines.size() >= 3);
    if (lines.size() < 3) continue;
    EXPECT_EQ(lines.front(), header + " | " + label);
    // 終端の行の chars は、見出しから終端の行の直前まで (改行を含む) の文字数
    const std::string& end = lines.back();
    const std::string end_prefix = "END " + label + " chars=";
    EXPECT_TRUE(starts_with(end, end_prefix));
    const std::size_t before_end = part.size() - end.size() - 1;
    EXPECT_EQ(end.substr(end_prefix.size()), std::to_string(detail::utf8_char_count(part.substr(0, before_end))));
    body.insert(body.end(), lines.begin() + 1, lines.end() - 1);
  }
  return body;
}

}  // namespace

TEST(summary_parts_fit_limit_and_keep_every_line) {
  const SuiteResult r = large_result();
  const auto parts = make_summary_parts(r);
  EXPECT_TRUE(parts.size() >= 2);
  const auto body = check_parts(parts, detail::summary_run_header(r), kSummaryPartMaxChars);
  // つなぐと全部の行がそろう (順番も同じ)
  std::vector<std::string> expected;
  for (const auto& b : detail::summary_blocks(r)) expected.insert(expected.end(), b.begin(), b.end());
  EXPECT_TRUE(body == expected);
  // テストの行と診断の各項目がそろっている
  int test_heads = 0, clusters = 0;
  for (const auto& l : body) {
    if (starts_with(l, "[peak ") || starts_with(l, "[sustained ")) ++test_heads;
    if (starts_with(l, "  cpu") && l.back() == ':') ++clusters;
  }
  EXPECT_EQ(test_heads, 18);
  EXPECT_EQ(clusters, 16);
  EXPECT_TRUE(std::find(body.begin(), body.end(), "[diagnostics] cond = -> battery_temp_c=28.0") != body.end());
  EXPECT_TRUE(starts_with(body.back(), "  freq min/max MHz (50 samples @100ms)"));
  EXPECT_EQ(make_summary_text(r), [&] {
    std::string s;
    for (const auto& p : parts) s += p;
    return s;
  }());
}

TEST(summary_blocks_are_not_split_across_parts) {
  const SuiteResult r = large_result();
  const auto parts = make_summary_parts(r);
  // 本文の各行が、どのパートに入ったか (見出しと終端の行を除く)
  std::vector<size_t> part_of_line;
  for (size_t i = 0; i < parts.size(); ++i) {
    const auto lines = split_lines(parts[i]);
    for (size_t k = 1; k + 1 < lines.size(); ++k) part_of_line.push_back(i);
  }
  // まとまり (テスト 1 本、診断の 1 項目) の行は、すべて同じパートにある
  size_t offset = 0;
  for (const auto& block : detail::summary_blocks(r)) {
    EXPECT_TRUE(offset + block.size() <= part_of_line.size());
    if (offset + block.size() > part_of_line.size()) break;
    for (size_t k = 1; k < block.size(); ++k) EXPECT_EQ(part_of_line[offset + k], part_of_line[offset]);
    offset += block.size();
  }
  EXPECT_EQ(offset, part_of_line.size());
}

TEST(summary_splits_oversized_block_by_lines) {
  const SuiteResult r = large_result();
  constexpr std::size_t kSmall = 900;  // テスト 1 本ぶんより小さい上限
  const auto parts = make_summary_parts(r, kSmall);
  const auto body = check_parts(parts, detail::summary_run_header(r), kSmall);
  std::vector<std::string> expected;
  for (const auto& b : detail::summary_blocks(r)) expected.insert(expected.end(), b.begin(), b.end());
  EXPECT_TRUE(body == expected);
}

TEST(summary_compresses_conditions_and_cap) {
  const SuiteResult r = large_result();
  const std::string text = make_summary_text(r);
  EXPECT_TRUE(text.find("\ncond(base) battery_level=68 battery_temp_c=25.0 charging=false power_save=false "
                        "thermal_headroom_10s=0.31 thermal_status=NONE\n") != std::string::npos);
  EXPECT_TRUE(text.find("\n  cond = -> battery_temp_c=26.0 thermal_headroom_10s=0.30\n") != std::string::npos);
  // cap は最大周波数と違う回 (ウォームアップ = 0 番目など) だけ出る
  int with_cap = 0, runs = 0;
  for (const auto& l : split_lines(text)) {
    if (!starts_with(l, "  W ") && !starts_with(l, "  #")) continue;
    ++runs;
    if (l.find(" cap ") != std::string::npos) ++with_cap;
  }
  EXPECT_EQ(runs, 18 * 6);
  EXPECT_EQ(with_cap, 18 * 3);
  // OS のビルドの情報
  EXPECT_TRUE(text.find(" os_version") == std::string::npos);
  EXPECT_TRUE(text.find("android 16 build=BP2A.250605.031.A3 release-keys | arm64") != std::string::npos);
  EXPECT_TRUE(text.find("\nos build_id=BP2A.250605.031.A3 incremental=1234567890 security_patch=2026-10-05 "
                        "fingerprint=maker/") != std::string::npos);
}

TEST(summary_conditions_diff_marks_missing_keys) {
  SuiteResult r;
  r.benchmark_version = kBenchmarkVersion;
  TestResult a;
  a.phase = "peak";
  a.id = "a";
  a.conditions_before = {{"x", "1"}, {"y", "2"}};
  a.conditions_after = {{"x", "1"}};
  r.tests.push_back(a);
  const std::string text = make_summary_text(r);
  EXPECT_TRUE(text.find("\n  cond = -> y=(none)\n") != std::string::npos);
  // 状態が記録されていなければ cond の行は出さない
  SuiteResult empty;
  empty.tests.push_back(TestResult{});
  EXPECT_TRUE(make_summary_text(empty).find("\ncond(base)") == std::string::npos);
  EXPECT_TRUE(make_summary_text(empty).find("\n  cond ") == std::string::npos);
  // 見出しに実行の時刻が入る (取れなければ ?)
  EXPECT_TRUE(starts_with(make_summary_text(empty), "MobileBench v | app ? build ? (?) | run ? | part 1/1\n"));
}

TEST(summary_ends_with_end_line_for_single_part) {
  SuiteResult r;
  r.benchmark_version = kBenchmarkVersion;
  r.started_utc = "2026-11-15T00:00:00Z";
  const auto parts = make_summary_parts(r);
  EXPECT_EQ(parts.size(), 1u);
  check_parts(parts, detail::summary_run_header(r), kSummaryPartMaxChars);
}

TEST(utf8_char_count_counts_code_points) {
  EXPECT_EQ(detail::utf8_char_count("abc"), 3u);
  EXPECT_EQ(detail::utf8_char_count("\xE6\x97\xA5\xE6\x9C\xAC"), 2u);  // 日本
  EXPECT_EQ(detail::utf8_char_count(""), 0u);
}

// ---- 診断に足した計測 ---------------------------------------------------------------

TEST(sieve_one_million_prime_count) {
  detail::SieveScratch s;
  EXPECT_EQ(detail::count_primes_segmented(1'000'000, detail::kSieveSegmentBytes, s), 78'498u);
  EXPECT_EQ(detail::count_primes_simple(1'000'000), 78'498u);
}

TEST(cluster_measure_supports_unit_candidates) {
  ClusterMeasure s = detail::run_cluster_measure("sieve1m", {{}}, 3);
  EXPECT_TRUE(s.error.empty());
  EXPECT_EQ(s.kernel, std::string("sieve1m"));
  EXPECT_TRUE(s.ms_per_unit > 0);
  EXPECT_EQ(s.gflops, 0.0);
  ClusterMeasure m = detail::run_cluster_measure("matmul576", {{}}, 1);
  EXPECT_TRUE(m.error.empty());
  EXPECT_TRUE(m.ms_per_unit > 0);
  EXPECT_TRUE(m.gflops > 0);
  ClusterMeasure bad = detail::run_cluster_measure("nope", {{}}, 1);
  EXPECT_EQ(bad.error, std::string("unknown kernel"));
}

TEST(busy_spin_counted_counts_second_half) {
  const detail::CountedSpin c = detail::busy_spin_counted(40, 20);
#if defined(__x86_64__) || defined(__aarch64__) || defined(__arm64__)
  EXPECT_TRUE(c.adds > 0);
  EXPECT_TRUE(c.ns >= 15'000'000 && c.ns < 200'000'000);  // 約 20 ms (共有環境なので上は緩く)
  const double ghz = static_cast<double>(c.adds) / static_cast<double>(c.ns);
  EXPECT_TRUE(ghz > 0.3 && ghz < 7.0);
#else
  EXPECT_EQ(c.adds, 0u);
#endif
}

TEST(clock_probe_comparison_records_both_methods) {
  std::atomic<bool> cancelled{false};
  int steps = 0;
  const ClockProbeComparison cp =
      detail::run_clock_probe_comparison(2, 3, 10, 20, cancelled, [&](const std::string&, bool) { ++steps; });
  EXPECT_EQ(steps, 3);
  EXPECT_EQ(cp.workers, 2);
  EXPECT_EQ(cp.count_from_ms, 10);
  EXPECT_EQ(cp.samples.size(), 3u);
  for (size_t i = 1; i < cp.samples.size(); ++i) EXPECT_TRUE(cp.samples[i].t_ms >= cp.samples[i - 1].t_ms + 10);
#if defined(__x86_64__) || defined(__aarch64__) || defined(__arm64__)
  for (const auto& s : cp.samples) {
    EXPECT_TRUE(s.current_ghz > 0.3 && s.current_ghz < 7.0);
    EXPECT_TRUE(s.counted_ghz > 0.3 && s.counted_ghz < 7.0);
    EXPECT_TRUE(s.counted_min <= s.counted_ghz + 1e-9 && s.counted_ghz <= s.counted_max + 1e-9);
    EXPECT_TRUE(s.current_min <= s.current_ghz + 1e-9 && s.current_ghz <= s.current_max + 1e-9);
  }
#endif
  // 取り消されたら途中で止まる
  std::atomic<bool> stop{true};
  EXPECT_TRUE(detail::run_clock_probe_comparison(2, 5, 10, 20, stop, [](const std::string&, bool) {}).samples.empty());
}

TEST(diagnostics_steps_include_new_measures) {
  detail::DiagnosticsOptions o;
  o.probe_samples = 12;
  // ブースト 2 + クロック比較 12 + クラスタ 3 x 8 + 掃引 7 x 2 x 4
  EXPECT_EQ(detail::diagnostics_steps(3, o), 2 + 12 + 24 + 56);
  EXPECT_EQ(detail::diagnostics_steps(0, o), 2 + 12 + 8 + 56);
}
