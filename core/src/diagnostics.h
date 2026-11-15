// 診断モード: スコアに含めない切り分け用の計測。
//
// 1. ブーストの持続: 休止後に 1 コアへ連続負荷をかけ、実効クロックの推移を記録
// 2. 状態を確かめるクロックの比較: 同じ全コア負荷から、現行の求め方と案の求め方 (docs/DESIGN.md §5.5)
//    の値を並べて記録する (§6 V22)。ピーク段階の判定には使わない
// 3. コアの種類ごとの性能: クラスタごとにスレッドを固定 (Android/Linux) し、
//    1 クロックあたりの性能 (Mcycles/unit, flops/cycle) を 1 スレッド / 全スレッドで測る。
//    固定時間方式の単位の案 (篩 上限 100 万、行列積 n=576) も測る (§6 V21)
// 4. 行列サイズの掃引: キャッシュに収まる / 収まらないサイズでの GFLOPS
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "mobilebench/telemetry.h"

namespace mb::detail {

using StepCallback = std::function<void(const std::string& label, bool warmup)>;

struct DiagnosticsOptions {
  // 状態を確かめるクロックの比較 (§6 V22 は 10 個以上を求める)。休みと負荷の長さは
  // ピーク段階の確認 (RunPolicy の min_rest_ms / probe_load_ms) と同じ値を Runner が渡す
  int probe_samples = 12;
  int probe_rest_ms = 2000;
  int probe_load_ms = 100;
};

// CPU 構成からクラスタ (同じ capacity / 最大周波数のコア群) を求める。情報がなければ空
std::vector<std::vector<int>> cpu_clusters(const CpuTopology& topology, int cpu_count);

int diagnostics_steps(size_t cluster_count, const DiagnosticsOptions& options = {});

// clusters が空なら、コアを固定しない「全コア」1 グループで測る (iOS など)
void run_diagnostics(int threads, const CpuTopology& topology, const std::vector<std::vector<int>>& clusters,
                     const DiagnosticsOptions& options, const std::atomic<bool>& cancelled,
                     const StepCallback& on_step, Diagnostics& out);

// 個別の診断 (テスト用に公開)
BoostTrace run_boost_trace(int idle_ms, int load_ms, const std::atomic<bool>& cancelled);
// samples 回、rest_ms 休んでから全コアに load_ms の負荷をかけ、2 つの求め方のクロックを記録する
ClockProbeComparison run_clock_probe_comparison(int threads, int samples, int rest_ms, int load_ms,
                                                const std::atomic<bool>& cancelled, const StepCallback& on_step);
// kernel: "sieve" (上限 4000 万) / "sieve1m" (上限 100 万) / "matmul128" / "matmul576"
ClusterMeasure run_cluster_measure(const std::string& kernel, const std::vector<std::vector<int>>& masks,
                                   int units_per_thread);
std::vector<MatmulSweepPoint> run_matmul_sweep(int threads, const std::atomic<bool>& cancelled,
                                               const StepCallback& on_step);

}  // namespace mb::detail
