#include "clock_probe.h"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace mb::detail {

namespace {

constexpr int kAddsPerIteration = 100;

// 依存加算チェーン 100 命令 x iterations。戻り値は最適化で消されないためのもの。
//
// 注意: 加算は「即値」ではなく「レジスタ」オペランドにする。
// Intel Golden Cove 以降などは小さな即値加算をリネーム段で消去するため、
// 即値版 (add x, x, #1) では 1 命令 1 サイクルにならず、実際の 5 倍以上のクロックに見える
// (開発環境の Xeon で即値版 19GHz / レジスタ版 3.2GHz、乗算レイテンシ法でも 3.2GHz を確認)。
[[maybe_unused]] std::uint64_t add_chain(std::uint64_t iterations, std::uint64_t one) {
  std::uint64_t x = 0;
  for (std::uint64_t i = 0; i < iterations; ++i) {
#if defined(__aarch64__) || defined(__arm64__)
    asm volatile(".rept 100\n\tadd %0, %0, %1\n\t.endr" : "+r"(x) : "r"(one));
#elif defined(__x86_64__)
    asm volatile(".rept 100\n\taddq %1, %0\n\t.endr" : "+r"(x) : "r"(one));
#endif
  }
  return x;
}

}  // namespace

double estimate_clock_ghz() {
#if defined(__aarch64__) || defined(__arm64__) || defined(__x86_64__)
  using clock = std::chrono::steady_clock;
  constexpr std::uint64_t kIterations = 5'000;  // 50 万加算 (4GHz で約 0.13ms)
  double best = 0.0;
  // 加算する値 (1) をコンパイラから不明にしておく
  std::uint64_t one = 1;
  asm volatile("" : "+r"(one));
  // 割り込み等の影響を除くため数回測って最大値 (= 最も邪魔されなかった回) を採る
  for (int rep = 0; rep < 5; ++rep) {
    const auto t0 = clock::now();
    volatile std::uint64_t sink = add_chain(kIterations, one);
    (void)sink;
    const auto t1 = clock::now();
    const double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    if (ns > 0) best = std::max(best, static_cast<double>(kIterations) * kAddsPerIteration / ns);
  }
  return best;
#else
  return 0.0;
#endif
}

void busy_spin_ms(int ms) {
  using clock = std::chrono::steady_clock;
  const auto end = clock::now() + std::chrono::milliseconds(ms);
  std::uint64_t one = 1;
  asm volatile("" : "+r"(one));
  while (clock::now() < end) {
#if defined(__aarch64__) || defined(__arm64__) || defined(__x86_64__)
    volatile std::uint64_t sink = add_chain(1000, one);
    (void)sink;
#else
    for (volatile int i = 0; i < 100000; ++i) {
    }
#endif
  }
}

CountedSpin busy_spin_counted(int load_ms, int count_from_ms) {
  CountedSpin out;
#if defined(__aarch64__) || defined(__arm64__) || defined(__x86_64__)
  using clock = std::chrono::steady_clock;
  constexpr std::uint64_t kChunkIterations = 1000;  // busy_spin_ms と同じ (1 回 10 万加算)
  const auto t0 = clock::now();
  const auto end = t0 + std::chrono::milliseconds(load_ms);
  const auto from = t0 + std::chrono::milliseconds(count_from_ms);
  std::uint64_t one = 1;
  asm volatile("" : "+r"(one));
  auto now = t0;
  auto count_start = t0;
  bool counting = false;
  while (now < end) {
    if (!counting && now >= from) {
      counting = true;
      count_start = now;
    }
    volatile std::uint64_t sink = add_chain(kChunkIterations, one);
    (void)sink;
    now = clock::now();
    if (counting) out.adds += kChunkIterations * kAddsPerIteration;
  }
  if (counting) out.ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - count_start).count();
#else
  busy_spin_ms(load_ms);
  (void)count_from_ms;
#endif
  return out;
}

double quick_clock_ghz() {
#if defined(__aarch64__) || defined(__arm64__) || defined(__x86_64__)
  using clock = std::chrono::steady_clock;
  constexpr std::uint64_t kIterations = 200;  // 2 万加算 x 3 回 (4GHz で計約 15µs)
  std::uint64_t one = 1;
  asm volatile("" : "+r"(one));
  double best = 0.0;
  for (int rep = 0; rep < 3; ++rep) {  // 割り込みで低く出た回を除くため最大値を採る
    const auto t0 = clock::now();
    volatile std::uint64_t sink = add_chain(kIterations, one);
    (void)sink;
    const auto t1 = clock::now();
    const double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    if (ns > 0) best = std::max(best, static_cast<double>(kIterations) * kAddsPerIteration / ns);
  }
  return best;
#else
  return 0.0;
#endif
}

}  // namespace mb::detail
