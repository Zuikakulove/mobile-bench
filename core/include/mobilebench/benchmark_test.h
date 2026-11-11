// ベンチマークテストのインターフェース。
//
// 新しいテスト (Phase 2: メモリ帯域・ストレージ等) はこのクラスを実装し、
// src/registry.cpp に登録するだけで Runner・スコア計算・JSON 出力に乗る。
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "mobilebench/telemetry.h"

namespace mb {

struct TestInfo {
  std::string id;        // 例: "cpu.single.sieve"。JSON のキー。変更禁止
  std::string name;      // 表示名
  std::string category;  // "cpu" / 将来 "memory", "storage" ...
  std::int64_t reference_ns = 0;  // 基準時間 (reference_times.h の値)
};

// テスト実行時にプラットフォーム層から与えられる情報。
struct TestContext {
  int threads = 1;          // マルチスレッドテストが使うワーカー数 (論理コア数)
  std::string scratch_dir;  // 一時ファイル用ディレクトリ (Phase 2 のストレージテスト用)
};

class BenchmarkTest {
 public:
  virtual ~BenchmarkTest() = default;

  virtual TestInfo info() const = 0;

  // 実際に使用するスレッド数 (JSON に記録する)
  virtual int threads_used() const { return 1; }

  // 計測対象外の準備 (バッファ確保、入力データ生成、スレッド起動など)
  virtual void setup(const TestContext& /*ctx*/) {}

  // 計測対象。固定の処理量を 1 回実行し、結果から求めたチェックサムを返す。
  // チェックサムは expected_checksum() と照合され、最適化による処理の削除や
  // プラットフォーム間の計算結果の食い違いを検出する。
  virtual std::uint64_t run_once() = 0;

  // 正しいチェックサム。setup() の後に呼ばれる (入力データから求めてよい)
  virtual std::uint64_t expected_checksum() const = 0;

  // 計測区間の直前・直後に呼ばれる (計測時間に含まれない)。クロック推定などに使う
  virtual void before_run() {}
  virtual void after_run() {}

  // 直近の run_once() の内訳 (after_run() の後に呼ばれる)
  virtual RunTelemetry take_telemetry() { return {}; }

  // 計測対象外の後始末
  virtual void teardown() {}
};

using TestPtr = std::unique_ptr<BenchmarkTest>;

}  // namespace mb
