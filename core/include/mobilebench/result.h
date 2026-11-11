#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mobilebench/telemetry.h"

namespace mb {

struct TestResult {
  std::string id;
  std::string name;
  std::string category;
  int threads = 1;
  std::int64_t reference_ns = 0;
  std::int64_t warmup_ns = 0;
  std::vector<std::int64_t> runs_ns;  // 本計測の生の時間
  std::int64_t median_ns = 0;
  double score = 0.0;
  bool valid = false;                 // チェックサムが全回一致したか
  std::uint64_t checksum = 0;
  std::uint64_t expected_checksum = 0;
  std::string error;                  // 例外などで失敗した場合のメッセージ
  std::vector<RunDetail> run_details; // ウォームアップを含む各回の内訳 (切り分け用)
  Conditions conditions_before;       // テスト開始時の端末の状態
  Conditions conditions_after;        // テスト終了時の端末の状態

  // v0.2.0: 段階と状態の判定
  std::string phase;        // "peak" / "sustained"
  int attempts = 1;         // 測った回数 (安定しなければ測り直す)
  double spread = 0.0;      // 本計測の (最大 - 最小) / 中央値
  bool stable = false;      // spread が基準以下か
  double ghz = 0.0;         // 計測時の実効クロック (各回の前後の推定値の中央値。マルチは全スレッド平均)
  int wait_ms = -1;         // peak: 計測前の回復待ち時間 (待っていない場合 -1)
  bool recovered = true;    // peak: 待ち時間内に基準クロックまで回復したか
  double recovery_ghz = 0.0;  // peak: 回復判定時の全コア実効クロック
};

struct PhaseResult {
  std::string name;         // "peak" / "sustained"
  double score = 0.0;       // この段階のテストスコアの幾何平均
  bool valid = false;
  bool stable = false;      // 全テストが安定していたか
};

struct PreconditionResult {
  int ms = 0;               // 事前負荷の時間
  double ghz = 0.0;         // 事前負荷の最後の全コア実効クロック
  bool settled = false;     // クロックが落ち着いたか (false = 上限時間で打ち切り)
};

struct BuildInfo {
  std::string os;
  std::string arch;
  std::string compiler;
  int flags_profile = 0;
  std::string flags;
  bool optimized = false;
};

struct SuiteResult {
  std::string benchmark_version;
  BuildInfo build;
  std::map<std::string, std::string> device;  // プラットフォーム層から渡される機種情報
  int warmup_runs = 0;
  int measured_runs = 0;
  int logical_cores = 0;
  std::vector<TestResult> tests;    // 全段階のテスト (phase で区別)
  std::vector<PhaseResult> phases;
  double overall_score = 0.0;  // 最初の段階 (通常は peak) のスコア
  double retention = 0.0;      // 持続率 = sustained / peak (両方あるとき)
  double baseline_ghz = 0.0;   // 開始時の全コア実効クロック (回復判定の基準)
  PreconditionResult precondition;
  bool valid = false;          // 全テストが valid か
  bool cancelled = false;
  CpuTopology topology;
  bool diagnostics_enabled = false;
  Diagnostics diagnostics;  // diagnostics_enabled のときのみ JSON に出力
  std::string summary_text; // 人が読む / チャットに貼るための要約 (summary.cpp)
};

// 要約テキストを作る (Runner が自動で summary_text に入れる)
std::string make_summary_text(const SuiteResult& result);

std::string to_json(const SuiteResult& result, bool pretty = true);

}  // namespace mb
