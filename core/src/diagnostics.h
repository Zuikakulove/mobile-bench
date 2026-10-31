// 診断モード: スコアに含めない切り分け用の計測。
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "mobilebench/telemetry.h"

namespace mb::detail {

// 行列積のサイズを変えながら、シングル / マルチスレッドの GFLOPS と作業領域の大きさを測る。
// キャッシュに収まるサイズと収まらないサイズで性能がどう変わるかを見るためのもの。
int matmul_sweep_steps();
std::vector<MatmulSweepPoint> run_matmul_sweep(int threads, const std::atomic<bool>& cancelled,
                                               const std::function<void(const std::string&, bool)>& on_step);

}  // namespace mb::detail
