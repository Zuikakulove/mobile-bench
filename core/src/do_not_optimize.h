// コンパイラ最適化で計測対象の処理が消されたり、定数畳み込みされたりするのを防ぐ。
// Android NDK (clang) / Apple clang / GCC で同じ動作をする。
#pragma once

#include <type_traits>

namespace mb::detail {

// 値を「外部から観測される」ものとして扱わせる (計算結果の削除を防ぐ)
template <class T>
inline void do_not_optimize(const T& value) {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "r,m"(value) : "memory");
#else
  static volatile const T* sink;
  sink = &value;
#endif
}

// 値をコンパイラから「不明」に見せる (入力の定数伝播を防ぐ)。整数・ポインタ専用。
// 注意: "+r,m" のような複数候補の入出力制約は GCC の一部の条件 (ASan 有効時など) で
// 値が壊れることを確認したため、単一のレジスタ制約 "+r" を使う。
template <class T>
inline void launder(T& value) {
  static_assert(std::is_integral<T>::value || std::is_pointer<T>::value, "launder() is for integers/pointers");
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : "+r"(value) : : "memory");
#else
  volatile T tmp = value;
  value = tmp;
#endif
}

inline void clobber_memory() {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : : "memory");
#endif
}

}  // namespace mb::detail
