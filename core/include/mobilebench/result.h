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
  std::vector<TestResult> tests;
  double overall_score = 0.0;  // 各テストスコアの幾何平均
  bool valid = false;          // 全テストが valid か
  bool cancelled = false;
  CpuTopology topology;
  bool diagnostics_enabled = false;
  Diagnostics diagnostics;  // diagnostics_enabled のときのみ JSON に出力
};

std::string to_json(const SuiteResult& result, bool pretty = true);

}  // namespace mb
