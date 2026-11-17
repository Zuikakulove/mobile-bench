// 固定時間方式の窓の数え方 (docs/DESIGN.md §4-Q5 の規則 3、§5.1。docs/PHASE1_PLAN.md P3.2)。
//
// ウォームアップの窓と本計測の窓を、止めずに続けて実行する。各ワーカーは、窓の境目を過ぎたら
// 実行中の単位を完了し、その時刻で窓を区切って、止まらずに次の単位へ進む。
// 窓ごとのスループットは、ワーカーごとの (その窓で完了した単位数 ÷ その窓の実時間) の合計とする。
// 数え方の手本は docs/measurements/tools/simulate_units.py の fixed_time_continuous。
//
// 時刻は呼び出し側が渡す (ユニットテストでは、作った時刻の列を渡す)。このファイルは時計を読まない。
#pragma once

#include <cmath>
#include <cstdint>

namespace mb::detail {

// ワーカー 1 本の、窓 1 つぶんの記録
struct WorkerWindow {
  std::int64_t units = 0;        // この窓で完了した単位数
  std::int64_t ns = 0;           // この窓の実時間 (前の区切りから、この区切りまで)
  std::uint64_t cpu_mask = 0;    // 単位の開始時に見た CPU 番号 (bit i = cpu i。64 以上と不明は入れない)
  std::int64_t unit_ns_min = 0;  // この窓でいちばん短かった 1 単位 (単位がなければ 0)
  std::int64_t unit_ns_max = 0;  // この窓でいちばん長かった 1 単位 (単位がなければ 0)
};

// ワーカー 1 本ぶんの数え方。
//
// 規則:
//   - 単位が終わった時刻 now が bounds[k] 以上なら、窓 k を閉じる。その単位は窓 k に数える。
//     窓 k の実時間は、前の区切りの時刻から now まで。最初の窓の「前の区切り」は start_ns。
//   - 1 つの単位が複数の境目をまたいだら、2 つ目以降の窓は units = 0、ns = 0 で閉じる。
//   - 窓を閉じても止まらない。最後の窓を閉じたら、on_unit_done が false を返す。
//
// 途中の集計はこのオブジェクトの中に持ち、out に書くのは窓を閉じるときだけにする
// (ワーカーの間で、同じキャッシュラインへの書き込みが重ならないようにするため)。
class WindowRecorder {
 public:
  // bounds: 窓の境目の時刻 (昇順、count 個)。out: count 個の出力先 (呼び出し側が事前に確保する)。
  // start_ns: このワーカーが最初の単位を始めた時刻。
  // bounds と out は、最後の on_unit_done が終わるまで有効であること
  void reset(const std::int64_t* bounds, int count, WorkerWindow* out, std::int64_t start_ns) {
    bounds_ = bounds;
    count_ = count > 0 ? count : 0;
    out_ = out;
    next_ = 0;
    split_ns_ = start_ns;
    unit_start_ns_ = start_ns;
    current_ = WorkerWindow{};
  }

  // 単位が 1 つ終わるたびに呼ぶ。now_ns は終わった時刻 (次の単位の始まりの時刻でもある)、
  // cpu はその単位の開始時の CPU 番号 (不明なら負の値)。
  // 最後の境目を越えたら false を返す (ワーカーは止まる)。false を返した後は呼ばない
  bool on_unit_done(std::int64_t now_ns, int cpu) {
    if (next_ >= count_) return false;
    const std::int64_t unit_ns = now_ns - unit_start_ns_;
    unit_start_ns_ = now_ns;
    if (current_.units == 0 || unit_ns < current_.unit_ns_min) current_.unit_ns_min = unit_ns;
    if (current_.units == 0 || unit_ns > current_.unit_ns_max) current_.unit_ns_max = unit_ns;
    ++current_.units;
    if (cpu >= 0 && cpu < 64) current_.cpu_mask |= (std::uint64_t{1} << cpu);
    if (now_ns < bounds_[next_]) return true;

    // 境目をまたいだ単位が終わった。この単位までを、窓 next_ に数える
    current_.ns = now_ns - split_ns_;
    out_[next_++] = current_;
    current_ = WorkerWindow{};
    split_ns_ = now_ns;
    // 同じ単位が、さらに先の境目もまたいでいたら、その窓は空で閉じる
    while (next_ < count_ && now_ns >= bounds_[next_]) out_[next_++] = WorkerWindow{};
    return next_ < count_;
  }

 private:
  const std::int64_t* bounds_ = nullptr;
  int count_ = 0;
  WorkerWindow* out_ = nullptr;
  int next_ = 0;                    // 次に閉じる窓の番号
  std::int64_t split_ns_ = 0;       // 前の区切りの時刻
  std::int64_t unit_start_ns_ = 0;  // 実行中の単位が始まった時刻
  WorkerWindow current_;            // 閉じていない窓の途中の集計
};

// 窓 1 つぶんの、全ワーカーの集計
struct WindowOutput {
  std::int64_t units = 0;        // 完了した単位数 (全ワーカーの合計)
  double units_per_sec = 0.0;    // ワーカーごとの (完了数 ÷ 実時間) の合計
  std::int64_t ns_per_unit = 0;  // round(1e9 ÷ units_per_sec)。0 = 単位が 1 つも終わらなかった
};

// 窓 k を集計する。records は「ワーカー w の窓 k」を records[w * windows + k] に置いた配列
// (ワーカー数 workers、ワーカー 1 本あたり windows 個)。
// 実時間が 0 のワーカー (その窓で単位が 1 つも終わらなかった) は、スループットに 0 として足す。
// WindowRecorder のユニットテストと TimedUnitTest が、同じこの関数を使う
inline WindowOutput combine_window(const WorkerWindow* records, int workers, int windows, int k) {
  WindowOutput out;
  for (int w = 0; w < workers; ++w) {
    const WorkerWindow& r = records[static_cast<size_t>(w) * static_cast<size_t>(windows) + static_cast<size_t>(k)];
    out.units += r.units;
    if (r.ns > 0) out.units_per_sec += static_cast<double>(r.units) / static_cast<double>(r.ns) * 1e9;
  }
  if (out.units_per_sec > 0.0) out.ns_per_unit = std::llround(1e9 / out.units_per_sec);
  return out;
}

}  // namespace mb::detail
