#include <algorithm>
#include <cstring>

#include "../do_not_optimize.h"
#include "cpu_kernels.h"

namespace mb::detail {

namespace {

inline int row_weight(int i) { return (i % 7) + 1; }
inline int col_weight(int j) { return (j % 5) + 1; }

}  // namespace

void matmul_fill_inputs(double* a, double* b, int n) {
  for (int i = 0; i < n; ++i) {
    for (int k = 0; k < n; ++k) {
      a[i * n + k] = static_cast<double>(((i * 7 + k * 13) % 17) - 8);   // [-8, 8]
      b[i * n + k] = static_cast<double>(((i * 5 + k * 11) % 19) - 9);   // [-9, 9]
    }
  }
}

void matmul_blocked(const double* a, const double* b, double* c, int n, int block) {
  launder(n);
  for (int ii = 0; ii < n; ii += block) {
    const int i_end = std::min(ii + block, n);
    for (int kk = 0; kk < n; kk += block) {
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
    }
  }
  clobber_memory();
}

std::uint64_t matmul_checksum(const double* c, int n) {
  double sum = 0.0;
  for (int i = 0; i < n; ++i) {
    double row = 0.0;
    const double* c_row = c + static_cast<size_t>(i) * n;
    for (int j = 0; j < n; ++j) row += c_row[j] * col_weight(j);
    sum += row * row_weight(i);
  }
  // 全ての部分和は 2^53 未満の整数なので double でも厳密
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(sum));
}

std::uint64_t matmul_expected_checksum(const double* a, const double* b, int n) {
  // sum_ij w_i C_ij v_j = sum_k (sum_i w_i A_ik) * (sum_j B_kj v_j)
  std::int64_t total = 0;
  for (int k = 0; k < n; ++k) {
    std::int64_t col = 0;
    std::int64_t row = 0;
    for (int i = 0; i < n; ++i) col += static_cast<std::int64_t>(a[i * n + k]) * row_weight(i);
    for (int j = 0; j < n; ++j) row += static_cast<std::int64_t>(b[k * n + j]) * col_weight(j);
    total += col * row;
  }
  return static_cast<std::uint64_t>(total);
}

void MatmulKernel::prepare(int workers) {
  const size_t elems = static_cast<size_t>(n_) * n_;
  a_.assign(elems, 0.0);
  b_.assign(elems, 0.0);
  matmul_fill_inputs(a_.data(), b_.data(), n_);
  expected_ = matmul_expected_checksum(a_.data(), b_.data(), n_);
  c_.assign(static_cast<size_t>(workers), std::vector<double>(elems, 0.0));
}

std::uint64_t MatmulKernel::run_unit(int worker) {
  auto& c = c_[static_cast<size_t>(worker)];
  std::memset(c.data(), 0, c.size() * sizeof(double));
  matmul_blocked(a_.data(), b_.data(), c.data(), n_, kMatmulBlock);
  const std::uint64_t checksum = matmul_checksum(c.data(), n_);
  do_not_optimize(checksum);
  return checksum;
}

void MatmulKernel::release() {
  a_.clear();
  b_.clear();
  c_.clear();
}

}  // namespace mb::detail
