// プラットフォーム層 (JNI / Objective-C++ / CLI) から呼ぶ公開 API。
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "mobilebench/benchmark_test.h"
#include "mobilebench/result.h"

namespace mb {

// 計測の共通ルール (全プラットフォーム共通・変更時は version.h を上げる)
inline constexpr int kWarmupRuns = 1;
inline constexpr int kMeasuredRuns = 5;

struct Progress {
  int test_index = 0;  // 0 始まり
  int test_count = 0;
  std::string test_id;
  std::string test_name;
  int step = 0;        // 完了したステップ数 (全テスト通算)
  int total_steps = 0;
  bool warmup = false;
  double fraction() const { return total_steps > 0 ? static_cast<double>(step) / total_steps : 0.0; }
};

using ProgressCallback = std::function<void(const Progress&)>;

struct RunConfig {
  int threads = 0;  // 0 = 論理コア数を自動検出
  std::string scratch_dir;
  std::map<std::string, std::string> device;  // JSON の "device" にそのまま入る
  std::vector<std::string> only;              // 空 = 全テスト。ID の前方一致で絞り込み
  // 診断モード: スコア対象のテストの後に行列サイズ掃引を行い、
  // 計測中の各コア周波数を定期記録する (スコアには影響しない)
  bool diagnostics = false;
};

class Runner {
 public:
  explicit Runner(RunConfig config = {});

  // 既定スイート (registry.cpp) を実行する。呼び出しスレッドでブロックする。
  SuiteResult run(const ProgressCallback& progress = {});

  // run() の結果を JSON 文字列で返す。UI はこの JSON を表示するだけにする。
  std::string run_json(const ProgressCallback& progress = {});

  // 任意のテスト列を実行する (ユニットテスト用)。
  SuiteResult run_tests(std::vector<TestPtr> tests, const ProgressCallback& progress,
                        int warmup_runs = kWarmupRuns, int measured_runs = kMeasuredRuns);

  // 別スレッドから呼んでよい。現在の 1 回の計測が終わった時点で中断する。
  void cancel() { cancelled_.store(true); }

 private:
  SuiteResult run_impl(std::vector<TestPtr> tests, const ProgressCallback& progress, int warmup_runs,
                       int measured_runs, bool diagnostics);

  RunConfig config_;
  std::atomic<bool> cancelled_{false};
};

// 既定スイートを構築する (src/registry.cpp)
std::vector<TestPtr> create_default_suite(const RunConfig& config);

// 統計 (src/stats.cpp)
std::int64_t median(std::vector<std::int64_t> values);
double compute_score(std::int64_t reference_ns, std::int64_t measured_ns);
double geometric_mean(const std::vector<double>& values);

BuildInfo current_build_info();
CpuTopology current_cpu_topology();
int logical_core_count();

}  // namespace mb
