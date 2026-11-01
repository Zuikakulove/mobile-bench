// 計測の内訳 (切り分け用の観測データ)。スコア計算には使わない。
//
// - どの CPU で動いたか / スレッドごとの処理量と時間
// - 実効クロックの推定値 (依存加算チェーン。両 OS 共通の方法)
// - OS が報告する各コアの周波数 (Android / Linux の sysfs。読めない端末では空)
// - CPU 構成 (最大周波数・capacity・キャッシュ)
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mb {

struct WorkerTelemetry {
  int worker = 0;
  int units = 0;               // この回に処理した作業単位数
  std::int64_t busy_ns = 0;    // 作業単位の処理に使った時間の合計
  std::vector<int> cpus;       // 作業単位の開始時に観測した CPU 番号 (昇順・重複なし)。取得不可なら空
  double ghz_before = 0.0;     // 計測直前の実効クロック推定値 [GHz] (0 = 未計測)
  double ghz_after = 0.0;      // 計測直後の実効クロック推定値 [GHz]
  double ghz_load_avg = 0.0;   // 負荷中 (作業単位の合間) の実効クロック平均 [GHz]。診断時のみ、0 = 未計測
  double ghz_load_max = 0.0;
};

struct RunTelemetry {
  std::vector<std::int64_t> unit_ns;  // シングルスレッド: 作業単位ごとの時間
  std::vector<int> unit_cpus;         // シングルスレッド: 作業単位の開始時の CPU 番号 (-1 = 不明)
  std::vector<WorkerTelemetry> workers;
};

struct RunDetail {
  bool warmup = false;
  std::int64_t ns = 0;
  std::int64_t t_start_ms = 0;               // スイート開始からの経過時間
  std::vector<std::int64_t> cpu_khz_before;  // 計測直前の各コア周波数 (OS 報告値, -1 = 読めない)
  std::vector<std::int64_t> cpu_khz_after;   // 計測直後の各コア周波数
  std::vector<std::int64_t> cpu_cap_khz;     // 計測直前の各コアの周波数上限 (scaling_max_freq)
  RunTelemetry telemetry;
};

struct CacheInfo {
  int level = 0;
  std::string type;          // "Data" / "Instruction" / "Unified"
  std::int64_t size_bytes = 0;
  std::string shared_cpus;   // 例: "0-5"
};

struct PerfLevel {  // Apple (hw.perflevelN.*)
  std::string name;
  int logical_cpus = 0;
  std::int64_t l1d_bytes = 0;
  std::int64_t l2_bytes = 0;
  int cpus_per_l2 = 0;
};

struct CpuTopology {
  std::string source;                 // "sysfs" / "sysctl" / "none"
  std::vector<std::int64_t> max_khz;  // CPU 番号順。-1 = 不明
  std::vector<int> capacity;          // cpu_capacity。-1 = 不明
  std::vector<CacheInfo> caches;      // 重複除去済み
  std::vector<PerfLevel> perf_levels;
};

struct MatmulSweepPoint {
  int n = 0;
  int threads = 0;
  int st_units = 0;
  int mt_units = 0;
  std::int64_t st_working_set_bytes = 0;  // A + B + C
  std::int64_t mt_working_set_bytes = 0;  // A + B (共有) + C x threads
  std::vector<std::int64_t> st_runs_ns;
  std::vector<std::int64_t> mt_runs_ns;
  std::int64_t st_median_ns = 0;
  std::int64_t mt_median_ns = 0;
  double st_gflops = 0.0;
  double mt_gflops = 0.0;
  bool valid = false;
  std::vector<WorkerTelemetry> mt_workers;  // MT の最後の回のワーカー内訳
  double st_ghz_load = 0.0;   // 負荷中の実効クロック (ST)
  double mt_ghz_load = 0.0;   // 負荷中の実効クロック (MT、ワーカー平均)
  std::vector<int> st_cpus;   // ST が動いた CPU
};

// コアの種類ごとの性能 (診断)。Android / Linux ではスレッドをそのクラスタのコアに固定して測る。
// 1 クロックあたりの性能 (Mcycles/unit, flops/cycle) で比べると、クロック状態の違いに左右されない。
struct ClusterMeasure {
  std::string kernel;          // "sieve" / "matmul128"
  int threads = 0;
  bool pinned = false;         // コアに固定できたか
  int units_per_thread = 0;
  double ms_per_unit = 0.0;    // スレッド平均
  double ghz_load = 0.0;       // 負荷中の実効クロック (スレッド平均)
  double mcycles_per_unit = 0.0;  // ms_per_unit x ghz_load (= 1 単位あたりの百万サイクル)
  double gflops = 0.0;         // matmul のみ: 全スレッド合計
  double flops_per_cycle = 0.0;   // matmul のみ: 1 スレッドあたり
  std::vector<int> cpus;       // 実際に動いた CPU
  std::string error;
};

struct ClusterResult {
  std::vector<int> cpus;
  std::int64_t max_khz = -1;
  int capacity = -1;
  std::vector<ClusterMeasure> measures;
};

// ブーストの持続 (診断): 休止の後、1 コアに連続で負荷をかけたときの実効クロックの推移
struct BoostSample {
  std::int64_t t_ms = 0;     // 負荷開始からの経過
  double unit_ms = 0.0;      // 篩 1 単位の時間
  int cpu = -1;
  double ghz = 0.0;          // 単位の直後の実効クロック
};

struct BoostTrace {
  int idle_ms = 0;
  std::vector<BoostSample> samples;
};

struct FreqSamples {
  int interval_ms = 0;
  std::vector<std::int64_t> t_ms;                // スイート開始からの経過時間
  std::vector<std::vector<std::int64_t>> khz;    // [sample][cpu]
};

// 端末の状態 (発熱状態・省電力・充電など)。プラットフォーム層が OS の公式 API で取得して渡す
using Conditions = std::map<std::string, std::string>;

struct Diagnostics {
  std::vector<BoostTrace> boost;
  std::vector<ClusterResult> clusters;
  std::vector<MatmulSweepPoint> matmul_sweep;
  FreqSamples freq;
  Conditions conditions_before;
  Conditions conditions_after;
};

}  // namespace mb
