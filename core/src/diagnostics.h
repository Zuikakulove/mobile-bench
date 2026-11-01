// 診断モード: スコアに含めない切り分け用の計測。
//
// 1. ブーストの持続: 休止後に 1 コアへ連続負荷をかけ、実効クロックの推移を記録
// 2. コアの種類ごとの性能: クラスタごとにスレッドを固定 (Android/Linux) し、
//    1 クロックあたりの性能 (Mcycles/unit, flops/cycle) を 1 スレッド / 全スレッドで測る
// 3. 行列サイズの掃引: キャッシュに収まる / 収まらないサイズでの GFLOPS
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "mobilebench/telemetry.h"

namespace mb::detail {

using StepCallback = std::function<void(const std::string& label, bool warmup)>;

// CPU 構成からクラスタ (同じ capacity / 最大周波数のコア群) を求める。情報がなければ空
std::vector<std::vector<int>> cpu_clusters(const CpuTopology& topology, int cpu_count);

int diagnostics_steps(size_t cluster_count);

// clusters が空なら、コアを固定しない「全コア」1 グループで測る (iOS など)
void run_diagnostics(int threads, const CpuTopology& topology, const std::vector<std::vector<int>>& clusters,
                     const std::atomic<bool>& cancelled, const StepCallback& on_step, Diagnostics& out);

// 個別の診断 (テスト用に公開)
BoostTrace run_boost_trace(int idle_ms, int load_ms, const std::atomic<bool>& cancelled);
ClusterMeasure run_cluster_measure(const std::string& kernel, const std::vector<std::vector<int>>& masks,
                                   int units_per_thread);
std::vector<MatmulSweepPoint> run_matmul_sweep(int threads, const std::atomic<bool>& cancelled,
                                               const StepCallback& on_step);

}  // namespace mb::detail
