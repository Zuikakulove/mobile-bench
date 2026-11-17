// プラットフォーム層 (JNI / Objective-C++ / CLI) から呼ぶ公開 API。
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "mobilebench/benchmark_test.h"
#include "mobilebench/result.h"

namespace mb {

// 計測の共通ルール (全プラットフォーム共通・変更時は version.h を上げる)
inline constexpr int kWarmupRuns = 1;
inline constexpr int kMeasuredRuns = 5;

// 計測の段階 (v0.2.0〜)。同じテスト・同じ基準時間を 2 つの条件で測る。
//   peak      : 各テストの前に端末の状態が回復するのを待ってから測る (短時間の最高性能)
//   sustained : 全コアに負荷をかけて状態を落ち着かせた後、休まず続けて測る (持続性能)
inline constexpr const char* kPhasePeak = "peak";
inline constexpr const char* kPhaseSustained = "sustained";

// Runner::interrupt の理由 (JSON の interruption、要約の INTERRUPTED(…))
inline constexpr const char* kInterruptLeftForeground = "left_foreground";

struct Progress {
  std::string phase;   // "prepare" / "peak" / "sustained" / "diagnostics"
  int test_index = 0;  // 段階内のテスト番号 (0 始まり)
  int test_count = 0;  // 段階内のテスト数
  std::string test_id;
  std::string test_name;
  int step = 0;        // 完了したステップ数 (全体通算)
  int total_steps = 0;
  bool warmup = false;
  int wait_ms = -1;    // 回復待ち / 事前負荷の経過時間 (それ以外は -1)
  int attempt = 1;     // 安定しなかった場合の測り直しの回数 (1 始まり)
  double fraction() const {
    if (total_steps <= 0) return 0.0;
    const double f = static_cast<double>(step) / total_steps;
    return f > 1.0 ? 1.0 : f;
  }
};

// 計測手順のパラメータ。既定値が正式な手順 (変更したら version.h を上げる)。
// ユニットテストでのみ短い値にする。
struct RunPolicy {
  // peak: 回復待ち
  int min_rest_ms = 2000;          // 各テストの前に最低この時間は休む
  int check_interval_ms = 3000;    // 回復したかの確認間隔
  int max_wait_ms = 60000;         // これ以上は待たない (回復しなかったと記録して測る)
  int probe_load_ms = 100;         // 確認時に全コアへかける負荷の長さ (クロックを上げてから測るため)
  double recovered_ratio = 0.95;   // 基準クロックのこの割合以上で「回復した」とみなす
  // sustained: 事前負荷
  int precondition_min_ms = 3000;
  int precondition_max_ms = 10000;
  int precondition_chunk_ms = 500;
  double precondition_settle = 0.03;  // 直前の区間とのクロック差がこれ以下なら「落ち着いた」
  // 共通: 安定性の判定
  double stable_spread = 0.06;     // 本計測の (最大 - 最小) / 中央値 がこれ以下なら安定
  int max_retries = 2;             // 安定しなかったときの測り直し回数
  bool peak_phase = true;
  bool sustained_phase = true;
};

using ProgressCallback = std::function<void(const Progress&)>;

// 端末の状態を返すコールバック (各テストの開始・終了時に run() を呼んだスレッドで呼ばれる)
using ConditionProbe = std::function<Conditions()>;

struct RunConfig {
  int threads = 0;  // 0 = 論理コア数を自動検出
  std::string scratch_dir;
  std::map<std::string, std::string> device;  // JSON の "device" にそのまま入る
  std::vector<std::string> only;              // 空 = 全テスト。ID の前方一致で絞り込み
  // 診断モード: スコア対象のテストの後に行列サイズ掃引を行い、
  // 計測中の各コア周波数を定期記録する (スコアには影響しない)
  bool diagnostics = false;
  RunPolicy policy;
};

class Runner {
 public:
  explicit Runner(RunConfig config = {});

  // 既定スイート (registry.cpp) を peak → sustained の順に実行する。呼び出しスレッドでブロックする。
  SuiteResult run(const ProgressCallback& progress = {});

  // run() の結果を JSON 文字列で返す。UI はこの JSON を表示するだけにする。
  std::string run_json(const ProgressCallback& progress = {});

  // 任意のテスト列を 1 段階 (回復待ち・事前負荷なし) で実行する (ユニットテスト用)。
  SuiteResult run_tests(std::vector<TestPtr> tests, const ProgressCallback& progress,
                        int warmup_runs = kWarmupRuns, int measured_runs = kMeasuredRuns);

  // 正式な手順 (peak → sustained) を任意のテストで実行する (ユニットテスト用)。
  // factory は段階ごとに呼ばれ、新しいテスト列を返すこと
  using SuiteFactory = std::function<std::vector<TestPtr>()>;
  SuiteResult run_with_factory(const SuiteFactory& factory, const ProgressCallback& progress = {});

  // 別スレッドから呼んでよい。現在の 1 回の計測が終わった時点で中断する (利用者による中止)。
  void cancel() {
    cancelled_.store(true);
    stop_.store(true);
  }

  // 別スレッドから呼んでよい。cancel() と同じ箇所で計測を中止し、結果を無効にする (docs/PHASE1_PLAN.md P3.10)。
  // 結果は interrupted = true、interruption = reason になる。cancelled は立てない (利用者の中止と区別する)。
  // プラットフォーム層は、計測中にアプリが前面を離れたら reason = kInterruptLeftForeground で呼ぶ
  void interrupt(const std::string& reason);

  // 別スレッドから呼んでよい。画面へのタッチ (指が触れた時点) を記録する (docs/PHASE1_PLAN.md P3.11)。
  // run() の実行中だけ、スイートの開始からの時刻を記録する。実行中でなければ無視する。判定とスコアには使わない
  void note_touch();

  // run() の前に設定する。nullptr なら状態は記録しない
  void set_condition_probe(ConditionProbe probe) { condition_probe_ = std::move(probe); }

 private:
  SuiteResult run_impl(const SuiteFactory& factory, const ProgressCallback& progress, int warmup_runs,
                       int measured_runs, bool full_procedure, bool diagnostics);

  Conditions probe_conditions();
  int touch_count();  // これまでに記録したタッチの数 (計測区間の外でだけ読む)

  RunConfig config_;
  ConditionProbe condition_probe_;
  std::atomic<bool> stop_{false};       // cancel() か interrupt() で立つ。計測の各所はこれを見て止まる
  std::atomic<bool> cancelled_{false};  // cancel() が呼ばれた
  // interrupt() とタッチの記録 (UI のスレッドから呼ばれる)。計測スレッドは計測区間の外でだけ読む
  std::mutex input_mutex_;
  bool interrupted_ = false;
  std::string interruption_;
  bool running_ = false;
  std::int64_t run_t0_ns_ = 0;
  int touches_ = 0;
  std::vector<std::int64_t> touch_ms_;
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
