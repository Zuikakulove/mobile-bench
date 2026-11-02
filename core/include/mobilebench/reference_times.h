// 各テストの基準時間 (ナノ秒) を一元管理するヘッダ。
//
// スコア = 基準時間 / 計測時間(中央値) * 1000
// つまり「基準機」で計測するとスコアが約 1000 になる。
//
// 【暫定値】開発用 Linux コンテナ (x86_64, 論理 4 コア, clang 18, -O2) での計測値 (中央値) を
// 丸めたもの。Android (NDK clang) / iOS (Apple clang) と同じ clang 系で測っている。
// 基準となる実機を決めたら、その中央値で差し替えること。
// 差し替えたら version.h の kBenchmarkVersion を上げること。
//
// peak / sustained の両段階で同じ基準時間を使う (持続率 = sustained / peak を意味のある値にするため)。
#pragma once

#include <cstdint>

namespace mb::reference {

// CPU シングルスレッド
inline constexpr std::int64_t kCpuSingleSieveNs = 150'000'000;   // 整数: エラトステネスの篩
inline constexpr std::int64_t kCpuSingleMatmulNs = 180'000'000;  // 浮動小数点: 行列積

// CPU マルチスレッド (総処理量はシングルの 4 倍 = 16 単位で固定。v0.2.0 で 64 → 16 単位)
inline constexpr std::int64_t kCpuMultiSieveNs = 170'000'000;
inline constexpr std::int64_t kCpuMultiMatmulNs = 230'000'000;

// Phase 2 以降で追加予定:
//   kMemoryCopyNs, kStorageSeqWriteNs, kStorageSeqReadNs, ...

}  // namespace mb::reference
