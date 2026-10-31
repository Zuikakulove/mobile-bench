// 「固定数の作業単位 (unit) を処理する」形式のテストの共通実装。
//
// シングルスレッド版とマルチスレッド版は同じカーネル (UnitKernel) を使う。
//   - シングル: units 個を 1 スレッドで順に処理
//   - マルチ  : units 個を論理コア数分のワーカーが取り合って処理
//
// マルチスレッドの総処理量 (units) はコア数に依存しない固定値にしてある。
// こうしないと「コアが多い端末ほど仕事が多い」ことになり、
// 端末間・OS 間でスコアが比較できなくなるため。
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "mobilebench/benchmark_test.h"
#include "../worker_pool.h"

namespace mb::detail {

class UnitKernel {
 public:
  virtual ~UnitKernel() = default;
  // ワーカーごとの作業領域を確保する (計測対象外)
  virtual void prepare(int workers) = 0;
  // 1 単位の処理を worker の作業領域で実行し、チェックサムを返す (計測対象)
  virtual std::uint64_t run_unit(int worker) = 0;
  // 1 単位あたりの正しいチェックサム
  virtual std::uint64_t unit_checksum() const = 0;
  virtual void release() {}
};

class UnitBasedTest final : public BenchmarkTest {
 public:
  UnitBasedTest(TestInfo info, std::unique_ptr<UnitKernel> kernel, int units, bool multithread)
      : info_(std::move(info)), kernel_(std::move(kernel)), units_(units), multithread_(multithread) {}

  TestInfo info() const override { return info_; }
  int threads_used() const override { return workers_; }

  void setup(const TestContext& ctx) override {
    workers_ = multithread_ ? (ctx.threads > 0 ? ctx.threads : 1) : 1;
    kernel_->prepare(workers_);
    partial_.assign(static_cast<size_t>(workers_), Partial{});
    if (multithread_) pool_ = std::make_unique<WorkerPool>(workers_);
  }

  std::uint64_t run_once() override {
    if (!multithread_) {
      std::uint64_t sum = 0;
      for (int u = 0; u < units_; ++u) sum += kernel_->run_unit(0);
      return sum;
    }
    std::atomic<int> next{0};
    pool_->run([&](int worker) {
      std::uint64_t local = 0;
      while (next.fetch_add(1, std::memory_order_relaxed) < units_) local += kernel_->run_unit(worker);
      partial_[static_cast<size_t>(worker)].value = local;
    });
    std::uint64_t sum = 0;
    for (const auto& p : partial_) sum += p.value;
    return sum;
  }

  std::uint64_t expected_checksum() const override {
    return kernel_->unit_checksum() * static_cast<std::uint64_t>(units_);
  }

  void teardown() override {
    pool_.reset();
    kernel_->release();
  }

 private:
  struct alignas(64) Partial {  // false sharing 回避
    std::uint64_t value = 0;
  };

  TestInfo info_;
  std::unique_ptr<UnitKernel> kernel_;
  int units_;
  bool multithread_;
  int workers_ = 1;
  std::vector<Partial> partial_;
  std::unique_ptr<WorkerPool> pool_;
};

}  // namespace mb::detail
