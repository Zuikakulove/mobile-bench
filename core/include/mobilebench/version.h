// ベンチマークのバージョン。
//
// スコアは「同じバージョン同士」でのみ比較可能。以下のいずれかを変更したら
// 必ずバージョンを上げること:
//   - テストの処理量・アルゴリズム
//   - 基準時間 (reference_times.h)
//   - コンパイルフラグプロファイル (build_profile.h)
//   - 計測手順 (ウォームアップ回数・本計測回数・代表値の取り方)
#pragma once

namespace mb {

inline constexpr const char* kBenchmarkVersion = "0.1.0";

}  // namespace mb
