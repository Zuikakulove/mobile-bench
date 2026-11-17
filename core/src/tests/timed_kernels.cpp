#include "timed_kernels.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "../do_not_optimize.h"

namespace mb::detail {

// ---- 篩 ----------------------------------------------------------------------

void SieveTimedKernel::prepare(int workers) {
  // limit の平方根 (切り捨て。SieveKernel::prepare が使う値と同じ)。計測の外なので単純に求める
  std::uint32_t root = 0;
  while (static_cast<std::uint64_t>(root + 1) * (root + 1) <= limit_) ++root;
  state_ = std::vector<State>(static_cast<size_t>(workers));
  for (auto& s : state_) {
    s.scratch.segment.assign(kSieveSegmentBytes, 0);
    // 計測中の再確保を避けるため予め確保しておく (SieveKernel::prepare と同じ大きさ)
    s.scratch.small.reserve(static_cast<size_t>(root) + 2);
    s.scratch.primes.reserve(static_cast<size_t>(root) + 2);
    s.scratch.next.reserve(static_cast<size_t>(root) + 2);
  }
}

void SieveTimedKernel::begin(int worker) {
  State& s = state_[static_cast<size_t>(worker)];
  s.verified = 0;
  s.ok = true;
  s.last = 0;
  s.first_bad = 0;
}

void SieveTimedKernel::run_unit(int worker) {
  State& s = state_[static_cast<size_t>(worker)];
  const std::uint64_t count = count_primes_segmented(limit_, kSieveSegmentBytes, s.scratch);
  do_not_optimize(count);
  if (count != expected_ && s.ok) {
    s.ok = false;
    s.first_bad = count;
  }
  s.last = count;
  ++s.verified;
}

bool SieveTimedKernel::checksum_ok(int worker) const {
  const State& s = state_[static_cast<size_t>(worker)];
  return s.verified > 0 && s.ok;
}

std::uint64_t SieveTimedKernel::observed_checksum(int worker) const {
  const State& s = state_[static_cast<size_t>(worker)];
  return s.ok ? s.last : s.first_bad;
}

// ---- 行列積の片 ----------------------------------------------------------------

// 内側の 4 重ループ (jj、i、k、j) は matmul_blocked と同じ形。入口の launder と出口の clobber_memory も同じ
void matmul_piece(const double* a, const double* b, double* c, int n, int block, int ii, int kk) {
  launder(n);
  const int i_end = std::min(ii + block, n);
  const int k_end = std::min(kk + block, n);
  for (int jj = 0; jj < n; jj += block) {
    const int j_end = std::min(jj + block, n);
    for (int i = ii; i < i_end; ++i) {
      double* c_row = c + static_cast<size_t>(i) * n;
      const double* a_row = a + static_cast<size_t>(i) * n;
      for (int k = kk; k < k_end; ++k) {
        const double aik = a_row[k];
        const double* b_row = b + static_cast<size_t>(k) * n;
        for (int j = jj; j < j_end; ++j) c_row[j] += aik * b_row[j];
      }
    }
  }
  clobber_memory();
}

void MatmulPieceKernel::prepare(int workers) {
  if (n_ <= 0 || n_ % kMatmulBlock != 0) throw std::invalid_argument("matmul piece size must be a multiple of the block");
  blocks_ = n_ / kMatmulBlock;
  pieces_ = blocks_ * blocks_;
  const size_t elems = static_cast<size_t>(n_) * n_;
  a_.assign(elems, 0.0);
  b_.assign(elems, 0.0);
  matmul_fill_inputs(a_.data(), b_.data(), n_);
  expected_ = matmul_expected_checksum(a_.data(), b_.data(), n_);
  state_ = std::vector<State>(static_cast<size_t>(workers));
  for (auto& s : state_) s.c.assign(elems, 0.0);
}

void MatmulPieceKernel::begin(int worker) {
  // 出力はここでゼロにしない。行ブロックの最初の片が、その行をゼロにする
  State& s = state_[static_cast<size_t>(worker)];
  s.piece = 0;
  s.acc = 0;
  s.verified = 0;
  s.ok = true;
  s.last = 0;
  s.first_bad = 0;
}

void MatmulPieceKernel::run_unit(int worker) {
  State& s = state_[static_cast<size_t>(worker)];
  const int bi = s.piece / blocks_;
  const int bk = s.piece % blocks_;
  const int ii = bi * kMatmulBlock;
  const int kk = bk * kMatmulBlock;
  double* c = s.c.data();
  if (bk == 0) {  // この行ブロックの最初の片
    std::memset(c + static_cast<size_t>(ii) * n_, 0, static_cast<size_t>(kMatmulBlock) * n_ * sizeof(double));
  }
  matmul_piece(a_.data(), b_.data(), c, n_, kMatmulBlock, ii, kk);
  if (bk == blocks_ - 1) {  // この行ブロックの最後の片
    s.acc += matmul_checksum_rows(c, n_, ii, ii + kMatmulBlock);
  }
  if (++s.piece == pieces_) {  // 行列積 1 回が完成した
    const std::uint64_t checksum = s.acc;
    do_not_optimize(checksum);
    if (checksum != expected_ && s.ok) {
      s.ok = false;
      s.first_bad = checksum;
    }
    s.last = checksum;
    ++s.verified;
    s.acc = 0;
    s.piece = 0;
  }
}

void MatmulPieceKernel::finish(int worker) {
  while (state_[static_cast<size_t>(worker)].piece != 0) run_unit(worker);
}

bool MatmulPieceKernel::checksum_ok(int worker) const {
  const State& s = state_[static_cast<size_t>(worker)];
  return s.verified > 0 && s.ok;
}

std::uint64_t MatmulPieceKernel::observed_checksum(int worker) const {
  const State& s = state_[static_cast<size_t>(worker)];
  return s.ok ? s.last : s.first_bad;
}

void MatmulPieceKernel::release() {
  a_.clear();
  b_.clear();
  state_.clear();
}

}  // namespace mb::detail
