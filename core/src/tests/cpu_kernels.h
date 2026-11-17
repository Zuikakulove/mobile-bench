// CPU テストのカーネル。
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "unit_based_test.h"

namespace mb::detail {

// ---- 整数: エラトステネスの篩 (区間篩) ---------------------------------------
//
// 区間 (segment) を L1 キャッシュに収まる 32KiB に固定し、メモリ帯域ではなく
// 整数演算・分岐・L1 アクセス性能を測る (メモリ帯域は Phase 2 で別テスト)。

inline constexpr std::uint32_t kSieveLimit = 40'000'000;       // [0, limit) の素数を数える
inline constexpr std::uint64_t kSievePrimeCount = 2'433'654;   // pi(40,000,000)
inline constexpr std::uint32_t kSieveSegmentBytes = 32 * 1024;

struct SieveScratch {
  std::vector<std::uint8_t> segment;
  std::vector<std::uint8_t> small;
  std::vector<std::uint32_t> primes;
  std::vector<std::uint32_t> next;
};

// [0, limit) の素数の個数を区間篩で数える
std::uint64_t count_primes_segmented(std::uint32_t limit, std::uint32_t segment_bytes, SieveScratch& s);
// 検証用の素朴な篩
std::uint64_t count_primes_simple(std::uint32_t limit);

class SieveKernel final : public UnitKernel {
 public:
  explicit SieveKernel(std::uint32_t limit = kSieveLimit, std::uint64_t expected = kSievePrimeCount)
      : limit_(limit), expected_(expected) {}
  void prepare(int workers) override;
  std::uint64_t run_unit(int worker) override;
  std::uint64_t unit_checksum() const override { return expected_; }
  void release() override { scratch_.clear(); }

 private:
  std::uint32_t limit_;
  std::uint64_t expected_;
  std::vector<SieveScratch> scratch_;
};

// ---- 浮動小数点: 行列積 (double, ブロック化) -----------------------------------
//
// 行列要素は小さな整数値を double で表したもの。積和が全て 2^53 未満の整数に
// 収まるため、FMA の有無やベクトル化の違いに関係なく結果がビット単位で一致し、
// Android / iOS で同一のチェックサムになる。演算自体は通常の倍精度 FP 演算。

inline constexpr int kMatmulSize = 576;  // N x N (ブロックサイズの倍数)
inline constexpr int kMatmulBlock = 64;

// C = A * B (C は事前にゼロであること)
void matmul_blocked(const double* a, const double* b, double* c, int n, int block);
// 重み付き総和 sum_ij w_i * C_ij * v_j を整数として返す
std::uint64_t matmul_checksum(const double* c, int n);
// A, B の内容から重み付き総和の正解を O(n^2) で求める
std::uint64_t matmul_expected_checksum(const double* a, const double* b, int n);
void matmul_fill_inputs(double* a, double* b, int n);

class MatmulKernel final : public UnitKernel {
 public:
  explicit MatmulKernel(int n = kMatmulSize) : n_(n) {}
  void prepare(int workers) override;
  std::uint64_t run_unit(int worker) override;
  std::uint64_t unit_checksum() const override { return expected_; }
  void release() override;

 private:
  int n_;
  std::uint64_t expected_ = 0;
  std::vector<double> a_, b_;           // 全ワーカーで共有 (読み取り専用)
  std::vector<std::vector<double>> c_;  // ワーカーごとの出力
};

}  // namespace mb::detail
