// 各テストの基準時間 (ナノ秒) を一元管理するヘッダ。
//
// スコア = 基準時間 / 計測時間(中央値) * 1000
// つまり「基準機」で計測するとスコアが約 1000 になる。
//
// 【暫定値】Phase 1 の値は開発用 Linux コンテナ (x86_64, 論理 4 コア, clang 18, -O2)
// での計測値 (中央値) を丸めたもの。Android (NDK clang) / iOS (Apple clang) と
// 同じ clang 系で測っている。基準となる実機を決めたら、その中央値で差し替えること。
// 差し替えたら version.h の kBenchmarkVersion を上げること。
#pragma once

#include <cstdint>

namespace mb::reference {

// CPU シングルスレッド
inline constexpr std::int64_t kCpuSingleSieveNs = 150'000'000;   // 整数: エラトステネスの篩
inline constexpr std::int64_t kCpuSingleMatmulNs = 180'000'000;  // 浮動小数点: 行列積

// CPU マルチスレッド (総処理量はシングルの kMultiThreadWorkFactor 倍で固定)
inline constexpr std::int64_t kCpuMultiSieveNs = 600'000'000;
inline constexpr std::int64_t kCpuMultiMatmulNs = 770'000'000;

// Phase 2 以降で追加予定:
//   kMemoryCopyNs, kStorageSeqWriteNs, kStorageSeqReadNs, ...

}  // namespace mb::reference
