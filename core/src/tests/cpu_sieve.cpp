#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "../do_not_optimize.h"
#include "cpu_kernels.h"

namespace mb::detail {

namespace {

std::uint32_t isqrt_u32(std::uint32_t v) {
  auto r = static_cast<std::uint64_t>(std::sqrt(static_cast<double>(v)));
  while (r * r > v) --r;
  while ((r + 1) * (r + 1) <= v) ++r;
  return static_cast<std::uint32_t>(r);
}

}  // namespace

std::uint64_t count_primes_segmented(std::uint32_t limit, std::uint32_t segment_bytes, SieveScratch& s) {
  // 入力を不透明にして、コンパイル時に結果を計算されないようにする
  launder(limit);
  launder(segment_bytes);
  if (limit > (1u << 31)) throw std::invalid_argument("sieve limit too large");
  if (limit <= 2) return 0;
  if (segment_bytes == 0) throw std::invalid_argument("segment size must be > 0");

  // 1) sqrt(limit) 以下の素数を素朴な篩で求める
  const std::uint32_t root = isqrt_u32(limit - 1);
  s.small.assign(static_cast<size_t>(root) + 1, 1);
  s.primes.clear();
  s.next.clear();
  for (std::uint32_t i = 2; i <= root; ++i) {
    if (!s.small[i]) continue;
    s.primes.push_back(i);
    s.next.push_back(i * i);
    for (std::uint32_t j = i * i; j <= root; j += i) s.small[j] = 0;
  }

  // 2) 区間ごとに合成数を消して数える
  if (s.segment.size() < segment_bytes) s.segment.resize(segment_bytes);
  std::uint8_t* seg = s.segment.data();
  const size_t prime_count = s.primes.size();
  const std::uint32_t* primes = s.primes.data();
  std::uint32_t* next = s.next.data();

  std::uint64_t count = 0;
  for (std::uint64_t low = 0; low < limit; low += segment_bytes) {
    const std::uint64_t high = std::min<std::uint64_t>(low + segment_bytes, limit);
    const auto len = static_cast<std::uint32_t>(high - low);
    std::memset(seg, 1, len);
    for (size_t i = 0; i < prime_count; ++i) {
      const std::uint32_t p = primes[i];
      std::uint64_t m = next[i];
      for (; m < high; m += p) seg[m - low] = 0;
      next[i] = static_cast<std::uint32_t>(m);
    }
    // 0 と 1 は素数ではない (区間が小さい場合 1 は 2 番目の区間に入る)
    for (std::uint64_t x = low; x < 2 && x < high; ++x) seg[x - low] = 0;
    std::uint32_t c = 0;
    for (std::uint32_t j = 0; j < len; ++j) c += seg[j];
    count += c;
  }
  clobber_memory();
  return count;
}

std::uint64_t count_primes_simple(std::uint32_t limit) {
  if (limit <= 2) return 0;
  std::vector<std::uint8_t> is_prime(limit, 1);
  is_prime[0] = is_prime[1] = 0;
  for (std::uint64_t i = 2; i * i < limit; ++i) {
    if (!is_prime[i]) continue;
    for (std::uint64_t j = i * i; j < limit; j += i) is_prime[j] = 0;
  }
  std::uint64_t count = 0;
  for (auto v : is_prime) count += v;
  return count;
}

void SieveKernel::prepare(int workers) {
  scratch_.assign(static_cast<size_t>(workers), SieveScratch{});
  for (auto& s : scratch_) {
    s.segment.assign(kSieveSegmentBytes, 0);
    // 計測中の再確保を避けるため予め確保しておく
    const std::uint32_t root = isqrt_u32(limit_);
    s.small.reserve(static_cast<size_t>(root) + 2);
    s.primes.reserve(static_cast<size_t>(root) + 2);
    s.next.reserve(static_cast<size_t>(root) + 2);
  }
}

std::uint64_t SieveKernel::run_unit(int worker) {
  const std::uint64_t count =
      count_primes_segmented(limit_, kSieveSegmentBytes, scratch_[static_cast<size_t>(worker)]);
  do_not_optimize(count);
  return count;
}

}  // namespace mb::detail
