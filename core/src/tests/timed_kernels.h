// 固定時間方式 (docs/DESIGN.md D3、§4-Q5、§5.1) のカーネル。
//
// 1 単位は、最も遅いコアで窓の 1/20 以下、最も速いコアで 100 µs 以上の大きさにする (§4-Q5 の規則 4・5)。
// 照合はカーネルの中で行い、テストは「全部合ったか」だけを受け取る (docs/PHASE1_PLAN.md P3.3)。
//
// 現行の UnitKernel (unit_based_test.h) は、スコア対象の手順が使っているので残してある。
// 固定時間方式への切り替え (0.3.0) で、こちらに一本化する。
//
// このファイルと timed_kernels.cpp は、切り替えまで、アプリのどこからも参照されない。
// 既存のソース (cpu_kernels.h、cpu_matmul.cpp、cpu_sieve.cpp) には手を入れていない。
// 0.2.2 のままのビルドで、スコア対象のカーネルの機械語と位置が変わらないようにするためである
// (位置が変わると、遅いコアの篩が約 3 割動いた例がある。docs/DESIGN.md §1.8 (4))。
#pragma once

#include <cstdint>
#include <vector>

#include "cpu_kernels.h"

namespace mb::detail {

class TimedKernel {
 public:
  virtual ~TimedKernel() = default;
  // 計測の外: 作業領域の確保、入力の生成
  virtual void prepare(int workers) = 0;
  // 計測の外: 連続実行の前に、worker の状態を初期化する
  virtual void begin(int worker) = 0;
  // 計測区間: 1 単位。照合できるまとまりが完成したら、その場で照合する。メモリを確保しない
  virtual void run_unit(int worker) = 0;
  // 計測の外: 途中のまとまりを最後まで実行して、照合する
  virtual void finish(int worker) = 0;
  // begin 以降に 1 回以上照合し、すべて一致したか
  virtual bool checksum_ok(int worker) const = 0;
  // 照合 1 回ぶんの正しい値 (記録用)
  virtual std::uint64_t expected_checksum() const = 0;
  // 最後に照合した値。食い違いがあったら、最初に食い違った値 (記録用)
  virtual std::uint64_t observed_checksum(int worker) const = 0;
  virtual void release() {}
};

// ---- 整数: エラトステネスの篩 (区間篩) ---------------------------------------
//
// 1 単位 = [0, limit) の素数を数える 1 回。1 単位が、そのまま照合 1 回になる。
// スコア対象は limit = kSieveUnitLimit (1,000,000)。区間の大きさ (32 KiB) は、現行の単位と同じ。

// 固定時間方式の単位 (docs/DESIGN.md §4-Q5 の判断 1。docs/PHASE1_PLAN.md P3.1)
inline constexpr std::uint32_t kSieveUnitLimit = 1'000'000;
inline constexpr std::uint64_t kSieveUnitPrimeCount = 78'498;  // pi(1,000,000)

class SieveTimedKernel final : public TimedKernel {
 public:
  explicit SieveTimedKernel(std::uint32_t limit = kSieveUnitLimit, std::uint64_t expected = kSieveUnitPrimeCount)
      : limit_(limit), expected_(expected) {}
  void prepare(int workers) override;
  void begin(int worker) override;
  void run_unit(int worker) override;
  void finish(int /*worker*/) override {}
  bool checksum_ok(int worker) const override;
  std::uint64_t expected_checksum() const override { return expected_; }
  std::uint64_t observed_checksum(int worker) const override;
  void release() override { state_.clear(); }

 private:
  struct alignas(64) State {  // ワーカーごと。false sharing を避ける
    SieveScratch scratch;
    std::int64_t verified = 0;    // 照合した回数
    bool ok = true;               // すべて一致したか
    std::uint64_t last = 0;       // 最後に照合した値
    std::uint64_t first_bad = 0;  // 最初に食い違った値
  };

  std::uint32_t limit_;
  std::uint64_t expected_;
  std::vector<State> state_;
};

// ---- 浮動小数点: 行列積の片 ---------------------------------------------------
//
// 1 単位 = n x n の行列積 1 回を、外側 2 重のブロックの 1 組 (行 block 本ぶん × 中間の添字 block 個ぶん) に
// 分けた 1 片。片の番号 p から、行ブロック bi = p / (n / block) と、中間のブロック bk = p % (n / block) が決まる。
// スコア対象は n = kMatmulSize (576)、block = kMatmulBlock (64) の 81 片。
//
// 出力のゼロ埋めと照合は、行ブロックごとに置く (docs/PHASE1_PLAN.md J4。片の重さの差を小さくするため):
//   - 行ブロックの最初の片 (bk = 0) の前に、その行 block 本ぶんをゼロにする
//   - 行ブロックの最後の片の後に、その行 block 本ぶんの重み付き総和を足し込む
//   - 行列積 1 回が完成したら、足し込んだ値を正解と照合する
// 窓の終わりで途中の行列積は、finish が計測の外で最後まで実行して照合する。

// 固定時間方式の単位 (§4-Q5 の判断 2。P3.1): n=576 では 9 × 9 = 81 片
inline constexpr int kMatmulPieceCount = (kMatmulSize / kMatmulBlock) * (kMatmulSize / kMatmulBlock);
static_assert(kMatmulSize % kMatmulBlock == 0 && kMatmulPieceCount == 81, "matmul unit is 1/81 of n=576");

// 重み付き総和 sum_ij w_i * C_ij * v_j (matmul_checksum と同じもの) を、行 [i0, i1) だけについて求める。
// 行の範囲を重ならないように分けて足すと (符号なし 64 ビットの加算)、matmul_checksum の値と一致する
std::uint64_t matmul_checksum_rows(const double* c, int n, int i0, int i1);

// C の行 [ii, ii + block) に、A の列 [kk, kk + block) と B の行 [kk, kk + block) の積を足し込む。
// 内側のループは matmul_blocked (cpu_matmul.cpp) と同じ形にしてある。
// kk を 0 から block ずつ進めて全部実行すると、C のその行は A * B の行になる (C のその行が事前にゼロであること)
void matmul_piece(const double* a, const double* b, double* c, int n, int block, int ii, int kk);

class MatmulPieceKernel final : public TimedKernel {
 public:
  // n は kMatmulBlock の倍数であること (違えば prepare が std::invalid_argument を投げる)
  explicit MatmulPieceKernel(int n = kMatmulSize) : n_(n) {}
  void prepare(int workers) override;
  void begin(int worker) override;
  void run_unit(int worker) override;
  void finish(int worker) override;
  bool checksum_ok(int worker) const override;
  std::uint64_t expected_checksum() const override { return expected_; }
  std::uint64_t observed_checksum(int worker) const override;
  void release() override;

  // 行列積 1 回ぶんの片の数 ((n / kMatmulBlock)^2)。prepare の後に有効
  int piece_count() const { return pieces_; }
  // 次に実行する片の番号 (0 = 行列積の区切り)
  int next_piece(int worker) const { return state_[static_cast<size_t>(worker)].piece; }
  // begin 以降に照合した回数 (= 完成した行列積の数)
  std::int64_t verified_count(int worker) const { return state_[static_cast<size_t>(worker)].verified; }
  // worker の出力 (n * n 個)。ユニットテスト用
  const double* output(int worker) const { return state_[static_cast<size_t>(worker)].c.data(); }
  // 正解を置き換える (prepare の後に呼ぶ)。照合が食い違いを検出することを確かめる、ユニットテスト用
  void override_expected_checksum(std::uint64_t value) { expected_ = value; }

 private:
  struct alignas(64) State {  // ワーカーごと。false sharing を避ける
    std::vector<double> c;        // 出力
    int piece = 0;                // 次に実行する片
    std::uint64_t acc = 0;        // 完成した行ブロックの重み付き総和の累計
    std::int64_t verified = 0;    // 照合した回数
    bool ok = true;               // すべて一致したか
    std::uint64_t last = 0;       // 最後に照合した値
    std::uint64_t first_bad = 0;  // 最初に食い違った値
  };

  int n_;
  int blocks_ = 0;  // n / kMatmulBlock
  int pieces_ = 0;  // blocks_ * blocks_
  std::uint64_t expected_ = 0;
  std::vector<double> a_, b_;  // 全ワーカーで共有 (読み取り専用)
  std::vector<State> state_;
};

}  // namespace mb::detail
