// 実効クロックの推定。
//
// 1 サイクルで完了する整数加算を、前の結果に依存させて (並列実行できないように)
// 連続実行し、その時間から「今このスレッドが動いているコアの実効クロック」を求める。
// sysfs などの OS 依存の情報や権限が不要なため、Android / iOS で同じ方法で使える。
// 対応していないアーキテクチャでは 0 を返す。
#pragma once

#include <cstdint>

namespace mb::detail {

double estimate_clock_ghz();

// 指定時間だけ CPU を 100% 使う (DVFS でクロックを上げる / 事前負荷用)
void busy_spin_ms(int ms);

// busy_spin_ms と同じ負荷をかけ、負荷の開始から count_from_ms 以降に回した加算の数と、その時間を返す。
// adds / ns が、その間の実効クロック [GHz] になる (診断用。docs/DESIGN.md §5.5)。
// 対応していないアーキテクチャでは adds = ns = 0
struct CountedSpin {
  std::uint64_t adds = 0;
  std::int64_t ns = 0;
};
CountedSpin busy_spin_counted(int load_ms, int count_from_ms);

// 負荷中に挟む短い版 (2 万加算 x 3 回の最大値、4GHz で計約 15µs)
double quick_clock_ghz();

}  // namespace mb::detail
