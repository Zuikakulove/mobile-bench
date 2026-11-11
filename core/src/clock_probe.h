// 実効クロックの推定。
//
// 1 サイクルで完了する整数加算を、前の結果に依存させて (並列実行できないように)
// 連続実行し、その時間から「今このスレッドが動いているコアの実効クロック」を求める。
// sysfs などの OS 依存の情報や権限が不要なため、Android / iOS で同じ方法で使える。
// 対応していないアーキテクチャでは 0 を返す。
#pragma once

namespace mb::detail {

double estimate_clock_ghz();

// 指定時間だけ CPU を 100% 使う (DVFS でクロックを上げる / 事前負荷用)
void busy_spin_ms(int ms);

// 負荷中に挟む短い版 (2 万加算 x 3 回の最大値、4GHz で計約 15µs)
double quick_clock_ghz();

}  // namespace mb::detail
