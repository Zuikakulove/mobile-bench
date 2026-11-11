// 既定スイートの登録。テストを追加するときはここに 1 行足す。
// 表示順・JSON の並び順はこの順になる。
#include <algorithm>

#include "mobilebench/reference_times.h"
#include "mobilebench/runner.h"
#include "tests/cpu_kernels.h"

namespace mb {

namespace {

// 1 回の計測あたりの作業単位数 (処理量の定義。変更したら version.h を上げる)
constexpr int kSingleThreadUnits = 4;
// マルチはシングルの 4 倍の総処理量 (16 単位)。v0.1 は 16 倍だったが、全コア負荷が約 1 秒を超えると
// 電力制御で大きく制限される端末があり、ピーク段階を短時間で終えるため v0.2.0 で 4 倍にした
constexpr int kMultiThreadWorkFactor = 4;
constexpr int kMultiThreadUnits = kSingleThreadUnits * kMultiThreadWorkFactor;

bool selected(const RunConfig& config, const std::string& id) {
  if (config.only.empty()) return true;
  return std::any_of(config.only.begin(), config.only.end(),
                     [&](const std::string& prefix) { return id.rfind(prefix, 0) == 0; });
}

}  // namespace

std::vector<TestPtr> create_default_suite(const RunConfig& config) {
  using detail::MatmulKernel;
  using detail::SieveKernel;
  using detail::UnitBasedTest;

  std::vector<TestPtr> suite;
  auto add = [&](TestInfo info, std::unique_ptr<detail::UnitKernel> kernel, int units, bool mt) {
    if (!selected(config, info.id)) return;
    suite.push_back(std::make_unique<UnitBasedTest>(std::move(info), std::move(kernel), units, mt));
  };

  add({"cpu.single.sieve", "CPU Single-thread Integer (Sieve)", "cpu", reference::kCpuSingleSieveNs},
      std::make_unique<SieveKernel>(), kSingleThreadUnits, false);
  add({"cpu.single.matmul", "CPU Single-thread Float (Matrix multiply)", "cpu", reference::kCpuSingleMatmulNs},
      std::make_unique<MatmulKernel>(), kSingleThreadUnits, false);
  add({"cpu.multi.sieve", "CPU Multi-thread Integer (Sieve)", "cpu", reference::kCpuMultiSieveNs},
      std::make_unique<SieveKernel>(), kMultiThreadUnits, true);
  add({"cpu.multi.matmul", "CPU Multi-thread Float (Matrix multiply)", "cpu", reference::kCpuMultiMatmulNs},
      std::make_unique<MatmulKernel>(), kMultiThreadUnits, true);

  // Phase 2 以降: memory.copy / storage.seq_write / storage.seq_read ...
  return suite;
}

}  // namespace mb
