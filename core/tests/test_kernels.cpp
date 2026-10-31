#include <vector>

#include "mini_test.h"
#include "tests/cpu_kernels.h"

using namespace mb::detail;

TEST(sieve_segmented_matches_simple) {
  SieveScratch s;
  for (std::uint32_t seg : {1u, 2u, 7u, 64u, 1000u, kSieveSegmentBytes}) {
    for (std::uint32_t n = 0; n <= 3000; n += (n < 50 ? 1 : 37)) {
      EXPECT_EQ(count_primes_segmented(n, seg, s), count_primes_simple(n));
    }
  }
}

TEST(sieve_known_prime_counts) {
  SieveScratch s;
  EXPECT_EQ(count_primes_segmented(10, kSieveSegmentBytes, s), 4u);
  EXPECT_EQ(count_primes_segmented(100, kSieveSegmentBytes, s), 25u);
  EXPECT_EQ(count_primes_segmented(1'000'000, kSieveSegmentBytes, s), 78'498u);
  EXPECT_EQ(count_primes_segmented(kSieveLimit, kSieveSegmentBytes, s), kSievePrimeCount);
  EXPECT_EQ(count_primes_simple(kSieveLimit), kSievePrimeCount);
}

static std::vector<double> naive_matmul(const std::vector<double>& a, const std::vector<double>& b, int n) {
  std::vector<double> c(static_cast<size_t>(n) * n, 0.0);
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) {
      double acc = 0.0;
      for (int k = 0; k < n; ++k) acc += a[i * n + k] * b[k * n + j];
      c[i * n + j] = acc;
    }
  return c;
}

TEST(matmul_blocked_matches_naive) {
  for (int n : {1, 5, 37, 64, 100}) {
    for (int block : {1, 8, 64}) {
      std::vector<double> a(static_cast<size_t>(n) * n), b(a.size()), c(a.size(), 0.0);
      matmul_fill_inputs(a.data(), b.data(), n);
      matmul_blocked(a.data(), b.data(), c.data(), n, block);
      const auto ref = naive_matmul(a, b, n);
      EXPECT_TRUE(c == ref);  // 整数値なので完全一致する
      EXPECT_EQ(matmul_checksum(c.data(), n), matmul_expected_checksum(a.data(), b.data(), n));
    }
  }
}

TEST(matmul_checksum_detects_errors) {
  const int n = 32;
  std::vector<double> a(n * n), b(n * n), c(n * n, 0.0);
  matmul_fill_inputs(a.data(), b.data(), n);
  matmul_blocked(a.data(), b.data(), c.data(), n, 8);
  c[5 * n + 7] += 1.0;
  EXPECT_TRUE(matmul_checksum(c.data(), n) != matmul_expected_checksum(a.data(), b.data(), n));
}

TEST(matmul_kernel_full_size) {
  MatmulKernel k;
  k.prepare(1);
  EXPECT_EQ(k.run_unit(0), k.unit_checksum());
  EXPECT_EQ(k.run_unit(0), k.unit_checksum());  // 2 回目も同じ (C の初期化漏れがない)
  k.release();
}
