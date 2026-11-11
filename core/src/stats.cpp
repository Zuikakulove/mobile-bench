#include <algorithm>
#include <cmath>

#include "mobilebench/runner.h"

namespace mb {

std::int64_t median(std::vector<std::int64_t> values) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const size_t n = values.size();
  if (n % 2 == 1) return values[n / 2];
  // 偶数個の場合は中央 2 値の平均 (本計測は 5 回なので通常は使わない)
  return values[n / 2 - 1] + (values[n / 2] - values[n / 2 - 1]) / 2;
}

double compute_score(std::int64_t reference_ns, std::int64_t measured_ns) {
  if (reference_ns <= 0 || measured_ns <= 0) return 0.0;
  return static_cast<double>(reference_ns) / static_cast<double>(measured_ns) * 1000.0;
}

double geometric_mean(const std::vector<double>& values) {
  if (values.empty()) return 0.0;
  double log_sum = 0.0;
  for (double v : values) {
    if (!(v > 0.0)) return 0.0;  // 0 や NaN が含まれる場合は 0
    log_sum += std::log(v);
  }
  return std::exp(log_sum / static_cast<double>(values.size()));
}

}  // namespace mb
