// 固定時間方式の計測の部品のテスト (docs/PHASE1_PLAN.md P4.3 の作業 5)。
//
// 書き方は P2.4 に従う:
//   - 数え方は、作った時刻の列を WindowRecorder に渡して確かめる (実時間を使わない)
//   - 実スレッドと実時間を使うテストは、不変条件だけを検査する。絶対値は、広い許容差 (±20%) で 1 つだけ検査する
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "mini_test.h"
#include "platform.h"
#include "tests/timed_kernels.h"
#include "tests/timed_unit_test.h"
#include "window_recorder.h"

using namespace mb::detail;

namespace {

constexpr std::int64_t kMs = 1'000'000;  // 1 ms [ns]
constexpr std::int64_t kUs = 1'000;      // 1 µs [ns]

// 境目: t0 + (k + 1) * window (k = 0 … count - 1)
std::vector<std::int64_t> make_bounds(std::int64_t t0, std::int64_t window_ns, int count) {
  std::vector<std::int64_t> b;
  for (int k = 0; k < count; ++k) b.push_back(t0 + (k + 1) * window_ns);
  return b;
}

// 単位が終わる時刻の列を WindowRecorder に渡す。false が返ったところで止める。渡した単位の数を返す
int feed(WindowRecorder& rec, const std::vector<std::int64_t>& done_ns) {
  int fed = 0;
  for (std::int64_t t : done_ns) {
    ++fed;
    if (!rec.on_unit_done(t, 0)) break;
  }
  return fed;
}

// 手本 (simulate_units.py の fixed_time_continuous) と同じ求め方: 境目をまたいだ単位が終わった時刻と、
// そこまでの累計の単位数を並べ、隣どうしの差を取る。WindowRecorder とは別の書き方で同じ値になることを見る
std::vector<std::pair<std::int64_t, std::int64_t>> reference_windows(const std::vector<std::int64_t>& done_ns,
                                                                     const std::vector<std::int64_t>& bounds,
                                                                     std::int64_t start_ns) {
  std::vector<std::pair<std::int64_t, std::int64_t>> crossed;  // (時刻, 累計)
  std::int64_t count = 0;
  for (std::int64_t t : done_ns) {
    ++count;
    while (crossed.size() < bounds.size() && t >= bounds[crossed.size()]) crossed.emplace_back(t, count);
    if (crossed.size() == bounds.size()) break;
  }
  std::vector<std::pair<std::int64_t, std::int64_t>> out;  // (単位数, 実時間)
  std::int64_t prev_t = start_ns;
  std::int64_t prev_c = 0;
  for (const auto& c : crossed) {
    out.emplace_back(c.second - prev_c, c.first - prev_t);
    prev_t = c.first;
    prev_c = c.second;
  }
  return out;
}

// ---- 作った時刻の列: コアを分け合うワーカーの模型 ------------------------------
//
// ワーカーは「run_ns だけ動いて wait_ns だけ待つ」を繰り返す (wait_ns = 0 なら動き続ける)。
// 動いている間だけ単位が進み、1 単位は動いている時間で unit_ns かかる。phase_ns は、時刻 0 での周期の中の位置。
// スレッドがコアの間を移動しない形の模型である (PHASE1_PLAN.md P3.21 (1))。
struct SimWorker {
  std::int64_t unit_ns = 0;
  std::int64_t run_ns = 0;
  std::int64_t wait_ns = 0;
  std::int64_t phase_ns = 0;
};

// ワーカー 1 本を、最後の境目を越えるまで進め、窓ごとの記録を out (count 個) に入れる
void simulate_worker(const SimWorker& w, const std::vector<std::int64_t>& bounds, WorkerWindow* out) {
  std::int64_t t = 0;
  std::int64_t pos = w.phase_ns;  // 周期の中の位置。[0, run_ns) が動いている区間
  if (w.wait_ns > 0 && pos >= w.run_ns) {  // 待っている途中から始まる: 最初に動けるのは、次の周期の頭
    t += (w.run_ns + w.wait_ns) - pos;
    pos = 0;
  }
  WindowRecorder rec;
  rec.reset(bounds.data(), static_cast<int>(bounds.size()), out, t);
  for (;;) {
    std::int64_t need = w.unit_ns;  // この単位に要る、動いている時間の残り
    while (need > 0) {
      if (w.wait_ns > 0 && pos == w.run_ns) {  // 動ける区間を使い切った: 待つ
        t += w.wait_ns;
        pos = 0;
      }
      const std::int64_t step = w.wait_ns > 0 ? std::min(need, w.run_ns - pos) : need;
      t += step;
      pos += step;
      need -= step;
    }
    if (!rec.on_unit_done(t, 0)) break;
  }
}

double true_units_per_sec(const std::vector<SimWorker>& workers) {
  double rate = 0.0;
  for (const auto& w : workers) {
    const double duty = w.wait_ns > 0 ? static_cast<double>(w.run_ns) / static_cast<double>(w.run_ns + w.wait_ns) : 1.0;
    rate += duty / static_cast<double>(w.unit_ns) * 1e9;
  }
  return rate;
}

// 窓 200 ms × 6 (ウォームアップ 1 + 本計測 5) を模擬し、本計測の窓ごとの
// 「計測したスループット ÷ 真のスループット」を返す
std::vector<double> simulate_ratios(const std::vector<SimWorker>& workers) {
  const int windows = 6;
  const auto bounds = make_bounds(0, 200 * kMs, windows);
  std::vector<WorkerWindow> records(workers.size() * static_cast<size_t>(windows));
  for (size_t w = 0; w < workers.size(); ++w) simulate_worker(workers[w], bounds, &records[w * static_cast<size_t>(windows)]);
  const double truth = true_units_per_sec(workers);
  std::vector<double> ratios;
  for (int k = 1; k < windows; ++k) {
    const WindowOutput o = combine_window(records.data(), static_cast<int>(workers.size()), windows, k);
    ratios.push_back(o.units_per_sec / truth);
  }
  return ratios;
}

// n 本のワーカーが同じ周期で動き、周期の中の位置を等間隔にずらす。
// どの時刻でも同じ本数が動いている (= コアが空かない) 置き方である
std::vector<SimWorker> shared_cores(int n, std::int64_t unit_ns, std::int64_t run_ns, std::int64_t wait_ns) {
  std::vector<SimWorker> workers;
  for (int i = 0; i < n; ++i) workers.push_back({unit_ns, run_ns, wait_ns, (run_ns + wait_ns) * i / n});
  return workers;
}

// 本計測の 5 窓の中央値 (スコアに使う代表値) が median_tol 以内、どの窓も window_tol 以内で、真の値と合う
void expect_unbiased(const std::vector<SimWorker>& workers, double median_tol, double window_tol) {
  std::vector<double> ratios = simulate_ratios(workers);
  EXPECT_EQ(ratios.size(), size_t{5});
  for (double r : ratios) EXPECT_NEAR(r, 1.0, window_tol);
  std::sort(ratios.begin(), ratios.end());
  EXPECT_NEAR(ratios[ratios.size() / 2], 1.0, median_tol);
}

// ---- 実スレッドのテスト用の、偽のカーネル ----------------------------------------
//
// ワーカー w の単位は、begin の時刻から数えて period_ns[w] ごとの時刻まで、時計を見ながら待つ
// (j 個目の単位は、begin + (j + 1) * period の時刻に終わる)。スレッドが途中で止められて遅れた分は、
// 次の単位が短くなって取り戻す。共有のランナーで時間が揺れても、窓の中の単位数が大きくは変わらないようにするため。
class FakeTimedKernel final : public TimedKernel {
 public:
  explicit FakeTimedKernel(std::vector<std::int64_t> period_ns) : period_ns_(std::move(period_ns)) {}

  void prepare(int workers) override { state_ = std::vector<State>(static_cast<size_t>(workers)); }
  void begin(int worker) override {
    State& s = at(worker);
    ++s.begins;
    s.units = 0;
    s.due_ns = now_ns() + period(worker);
  }
  void run_unit(int worker) override {
    State& s = at(worker);
    while (now_ns() < s.due_ns) {
    }
    s.due_ns += period(worker);
    ++s.units;
  }
  void finish(int worker) override { ++at(worker).finishes; }
  bool checksum_ok(int worker) const override { return at(worker).units > 0 && worker != bad_worker_; }
  std::uint64_t expected_checksum() const override { return 42; }
  std::uint64_t observed_checksum(int worker) const override { return worker == bad_worker_ ? 41 : 42; }

  void set_bad_worker(int worker) { bad_worker_ = worker; }
  std::int64_t units(int worker) const { return at(worker).units; }  // 直近の begin 以降に実行した単位数
  int begins(int worker) const { return at(worker).begins; }
  int finishes(int worker) const { return at(worker).finishes; }

 private:
  struct alignas(64) State {
    std::int64_t units = 0;
    std::int64_t due_ns = 0;
    int begins = 0;
    int finishes = 0;
  };
  static std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
  std::int64_t period(int worker) const {
    return period_ns_[static_cast<size_t>(worker) % period_ns_.size()];
  }
  State& at(int worker) { return state_[static_cast<size_t>(worker)]; }
  const State& at(int worker) const { return state_[static_cast<size_t>(worker)]; }

  std::vector<std::int64_t> period_ns_;
  std::vector<State> state_;
  int bad_worker_ = -1;
};

// テストの中だけで使う、短い窓 (P2.4)。ウォームアップ 1 + 本計測 3
constexpr std::int64_t kTestWindowNs = 25 * kMs;
constexpr int kTestWarmup = 1;
constexpr int kTestMeasured = 3;

mb::TestContext context(int threads) {
  mb::TestContext ctx;
  ctx.threads = threads;
  return ctx;
}

// 実時間に依存しない不変条件: 窓の数、単位数の合計、集計の式、照合、begin と finish の回数。
// runs は、このテストで run_windows を呼んだ回数 (begin と finish は、ワーカーごとに 1 回ずつ呼ばれる)
void expect_invariants(TimedUnitTest& test, const FakeTimedKernel& kernel, const WindowedRun& run, int workers,
                       int runs = 1) {
  const int windows = kTestWarmup + kTestMeasured;
  EXPECT_EQ(test.threads_used(), workers);
  EXPECT_EQ(test.window_count(), windows);
  EXPECT_EQ(run.windows.size(), static_cast<size_t>(windows));
  EXPECT_TRUE(run.checksum_ok);
  if (run.windows.size() != static_cast<size_t>(windows)) return;
  for (int w = 0; w < workers; ++w) {
    std::int64_t units = 0;
    for (int k = 0; k < windows; ++k) {
      const WorkerWindow& r = test.worker_window(w, k);
      units += r.units;
      EXPECT_TRUE(r.units >= 0 && r.ns >= 0);
      EXPECT_TRUE(r.units > 0 || r.ns == 0);  // 窓は単位の終わりで閉じるので、実時間があるのに単位が 0、はない
      if (r.units > 0) EXPECT_TRUE(r.unit_ns_min <= r.unit_ns_max && r.unit_ns_max <= r.ns);
    }
    // ワーカーが窓に数えた単位数の合計は、カーネルが実行した単位数と同じ (数え落としも、二重の数えもない)
    EXPECT_EQ(units, kernel.units(w));
    EXPECT_TRUE(kernel.units(w) >= 1);
    EXPECT_EQ(kernel.begins(w), runs);
    EXPECT_EQ(kernel.finishes(w), runs);
  }
  for (int k = 0; k < windows; ++k) {
    std::int64_t units = 0;
    double rate = 0.0;
    for (int w = 0; w < workers; ++w) {
      const WorkerWindow& r = test.worker_window(w, k);
      units += r.units;
      if (r.ns > 0) rate += static_cast<double>(r.units) / static_cast<double>(r.ns) * 1e9;
    }
    const WindowOutput& o = run.windows[static_cast<size_t>(k)];
    EXPECT_EQ(o.units, units);
    EXPECT_NEAR(o.units_per_sec, rate, rate * 1e-12);
    EXPECT_EQ(o.ns_per_unit, rate > 0.0 ? std::llround(1e9 / rate) : 0);
  }
}

}  // namespace

// ==== 数え方 (作った時刻の列) ====================================================

TEST(window_recorder_counts_uniform_units) {
  // 7 ms の単位を、開始 0 から続ける。窓は 200 ms × 6
  const auto bounds = make_bounds(0, 200 * kMs, 6);
  std::vector<WorkerWindow> out(6);
  WindowRecorder rec;
  rec.reset(bounds.data(), 6, out.data(), 0);
  std::vector<std::int64_t> done;
  for (int j = 1; j <= 400; ++j) done.push_back(j * 7 * kMs);
  const int fed = feed(rec, done);
  // 窓 0 は 203 ms (29 個) で閉じる。以後、境目をまたいだ単位が終わるのは 406、602、805、1001、1204 ms
  const std::int64_t expected_units[6] = {29, 29, 28, 29, 28, 29};
  const std::int64_t expected_ms[6] = {203, 203, 196, 203, 196, 203};
  for (int k = 0; k < 6; ++k) {
    EXPECT_EQ(out[k].units, expected_units[k]);
    EXPECT_EQ(out[k].ns, expected_ms[k] * kMs);
    EXPECT_EQ(out[k].unit_ns_min, 7 * kMs);
    EXPECT_EQ(out[k].unit_ns_max, 7 * kMs);
    // どの窓も、単位数 ÷ 実時間は 1 単位 7 ms ぶんになる (窓の長さで割らないので、端数で偏らない)
    EXPECT_EQ(out[k].ns, out[k].units * 7 * kMs);
  }
  EXPECT_EQ(fed, 172);  // 1204 ms = 172 個目で最後の境目を越え、false が返る
}

TEST(window_recorder_boundary_unit_belongs_to_that_window) {
  // 10 ms の単位。20 個目が、境目 (200 ms) ちょうどで終わる。その単位は窓 0 に入る
  const auto bounds = make_bounds(0, 200 * kMs, 3);
  std::vector<WorkerWindow> out(3);
  WindowRecorder rec;
  rec.reset(bounds.data(), 3, out.data(), 0);
  for (int j = 1; j <= 19; ++j) EXPECT_TRUE(rec.on_unit_done(j * 10 * kMs, 0));
  EXPECT_EQ(out[0].units, 0);                      // まだ閉じていない
  EXPECT_TRUE(rec.on_unit_done(200 * kMs, 0));     // 境目ちょうど: 窓 0 を閉じる
  EXPECT_EQ(out[0].units, 20);
  EXPECT_EQ(out[0].ns, 200 * kMs);
  for (int j = 21; j <= 39; ++j) EXPECT_TRUE(rec.on_unit_done(j * 10 * kMs, 0));
  EXPECT_TRUE(rec.on_unit_done(400 * kMs, 0));
  EXPECT_EQ(out[1].units, 20);
  EXPECT_EQ(out[1].ns, 200 * kMs);
  // 境目の 1 ns 手前で終わった単位は、まだ窓 2 を閉じない
  EXPECT_TRUE(rec.on_unit_done(600 * kMs - 1, 0));
  EXPECT_EQ(out[2].units, 0);
  EXPECT_TRUE(!rec.on_unit_done(600 * kMs + 5, 0));  // 最後の境目を越えた: false
  EXPECT_EQ(out[2].units, 2);
  EXPECT_EQ(out[2].ns, 200 * kMs + 5);
  EXPECT_EQ(out[2].unit_ns_min, 6);
  EXPECT_EQ(out[2].unit_ns_max, 200 * kMs - 1);
}

TEST(window_recorder_unit_spanning_bounds_makes_empty_windows) {
  // 窓は 100 ms × 5。2 個目の単位が 90 ms から 350 ms までかかり、境目 100、200、300 をまたぐ
  const auto bounds = make_bounds(0, 100 * kMs, 5);
  std::vector<WorkerWindow> out(5);
  WindowRecorder rec;
  rec.reset(bounds.data(), 5, out.data(), 0);
  EXPECT_TRUE(rec.on_unit_done(90 * kMs, 3));
  EXPECT_TRUE(rec.on_unit_done(350 * kMs, 5));
  EXPECT_EQ(out[0].units, 2);  // またいだ単位は、最初にまたいだ窓に数える
  EXPECT_EQ(out[0].ns, 350 * kMs);
  EXPECT_EQ(out[0].cpu_mask, (std::uint64_t{1} << 3) | (std::uint64_t{1} << 5));
  EXPECT_EQ(out[0].unit_ns_min, 90 * kMs);
  EXPECT_EQ(out[0].unit_ns_max, 260 * kMs);
  for (int k : {1, 2}) {  // 2 つ目以降の窓は、空で閉じる
    EXPECT_EQ(out[k].units, 0);
    EXPECT_EQ(out[k].ns, 0);
    EXPECT_EQ(out[k].cpu_mask, std::uint64_t{0});
    EXPECT_EQ(out[k].unit_ns_min, 0);
    EXPECT_EQ(out[k].unit_ns_max, 0);
  }
  EXPECT_TRUE(rec.on_unit_done(390 * kMs, 5));
  EXPECT_TRUE(rec.on_unit_done(410 * kMs, 5));
  EXPECT_EQ(out[3].units, 2);
  EXPECT_EQ(out[3].ns, 60 * kMs);  // 前の区切り (350 ms) から
  // 最後の単位が、残りの境目 (500 ms) を大きく越える
  EXPECT_TRUE(!rec.on_unit_done(900 * kMs, 5));
  EXPECT_EQ(out[4].units, 1);
  EXPECT_EQ(out[4].ns, 490 * kMs);
  EXPECT_TRUE(!rec.on_unit_done(950 * kMs, 5));  // 止まった後に呼ばれても、記録を壊さない
  EXPECT_EQ(out[4].units, 1);

  // 空の窓のワーカーは、スループットに 0 として足す。全員が空なら、1 単位あたりの時間は 0 (その回は無効)
  const WindowOutput w1 = combine_window(out.data(), 1, 5, 1);
  EXPECT_EQ(w1.units, 0);
  EXPECT_EQ(w1.units_per_sec, 0.0);
  EXPECT_EQ(w1.ns_per_unit, 0);
}

TEST(window_recorder_first_window_starts_at_worker_start) {
  // ワーカーが、t0 (= 1000 ms) より 3 ms 遅れて最初の単位を始めた。最初の窓の実時間は、開始の時刻から数える
  const std::int64_t t0 = 1000 * kMs;
  const auto bounds = make_bounds(t0, 50 * kMs, 2);
  std::vector<WorkerWindow> out(2);
  WindowRecorder rec;
  rec.reset(bounds.data(), 2, out.data(), t0 + 3 * kMs);
  std::int64_t t = t0 + 3 * kMs;
  bool more = true;
  int n = 0;
  while (more) {
    t += 4 * kMs;
    more = rec.on_unit_done(t, -1);  // CPU 番号が不明 (iOS)
    ++n;
  }
  EXPECT_EQ(out[0].units, 12);       // 1003 + 48 = 1051 ms で境目 (1050 ms) を越える
  EXPECT_EQ(out[0].ns, 48 * kMs);
  EXPECT_EQ(out[1].units, 13);       // 1103 ms で次の境目 (1100 ms) を越える
  EXPECT_EQ(out[1].ns, 52 * kMs);
  EXPECT_EQ(out[0].cpu_mask, std::uint64_t{0});
  EXPECT_EQ(n, 25);

  // 最後の境目より後に始まったワーカー: 最初の単位で全部の窓が閉じる (1 個目の窓にだけ数える)
  std::vector<WorkerWindow> late(2);
  rec.reset(bounds.data(), 2, late.data(), t0 + 500 * kMs);
  EXPECT_TRUE(!rec.on_unit_done(t0 + 504 * kMs, 64));  // CPU 番号 64 以上は、マスクに入れない
  EXPECT_EQ(late[0].units, 1);
  EXPECT_EQ(late[0].ns, 4 * kMs);
  EXPECT_EQ(late[0].cpu_mask, std::uint64_t{0});
  EXPECT_EQ(late[1].units, 0);
  EXPECT_EQ(late[1].ns, 0);
}

TEST(window_recorder_matches_reference_counting) {
  // 長さの不ぞろいな単位 (決まった擬似乱数) で、手本の求め方と同じ値になる
  std::uint64_t seed = 12345;
  auto next = [&seed]() {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<std::int64_t>(seed >> 33);
  };
  for (int trial = 0; trial < 200; ++trial) {
    const int windows = 1 + static_cast<int>(next() % 8);
    const std::int64_t window_ns = (1 + next() % 50) * kMs;
    const std::int64_t t0 = next() % (1000 * kMs);
    const std::int64_t start = t0 + next() % (2 * window_ns);
    const std::int64_t max_unit = 1 + next() % (trial % 4 == 0 ? 3 * window_ns : window_ns / 4 + 1);
    const auto bounds = make_bounds(t0, window_ns, windows);
    std::vector<std::int64_t> done;
    std::int64_t t = start;  // 最後の境目より後に始まる場合もある (その場合も、単位を 1 つは実行する)
    do {
      t += 1 + next() % max_unit;
      done.push_back(t);
    } while (t < bounds.back());
    std::vector<WorkerWindow> out(static_cast<size_t>(windows));
    WindowRecorder rec;
    rec.reset(bounds.data(), windows, out.data(), start);
    EXPECT_EQ(feed(rec, done), static_cast<int>(done.size()));  // 最後の単位で、ちょうど false が返る
    const auto ref = reference_windows(done, bounds, start);
    EXPECT_EQ(ref.size(), static_cast<size_t>(windows));
    std::int64_t units = 0;
    std::int64_t ns = 0;
    for (int k = 0; k < windows && static_cast<size_t>(k) < ref.size(); ++k) {
      EXPECT_EQ(out[static_cast<size_t>(k)].units, ref[static_cast<size_t>(k)].first);
      EXPECT_EQ(out[static_cast<size_t>(k)].ns, ref[static_cast<size_t>(k)].second);
      units += out[static_cast<size_t>(k)].units;
      ns += out[static_cast<size_t>(k)].ns;
    }
    // 窓を全部足すと、実行した単位の全部と、開始から最後の単位の終わりまでの時間になる
    EXPECT_EQ(units, static_cast<std::int64_t>(done.size()));
    EXPECT_EQ(ns, done.back() - start);
  }
}

TEST(window_combine_sums_worker_rates) {
  // ワーカー 2 本、窓 2 つ。records[w * 2 + k]
  std::vector<WorkerWindow> records(4);
  records[0].units = 150;  // ワーカー 0 の窓 0: 150 個 / 200 ms = 750 個/秒
  records[0].ns = 200 * kMs;
  records[1].units = 100;  // ワーカー 0 の窓 1: 100 個 / 250 ms = 400 個/秒
  records[1].ns = 250 * kMs;
  records[2].units = 50;   // ワーカー 1 の窓 0: 50 個 / 200 ms = 250 個/秒
  records[2].ns = 200 * kMs;
  records[3].units = 0;    // ワーカー 1 の窓 1: 単位が 1 つも終わらなかった
  records[3].ns = 0;
  const WindowOutput w0 = combine_window(records.data(), 2, 2, 0);
  EXPECT_EQ(w0.units, 200);
  EXPECT_NEAR(w0.units_per_sec, 1000.0, 1e-9);
  EXPECT_EQ(w0.ns_per_unit, 1'000'000);
  const WindowOutput w1 = combine_window(records.data(), 2, 2, 1);
  EXPECT_EQ(w1.units, 100);
  EXPECT_NEAR(w1.units_per_sec, 400.0, 1e-9);
  EXPECT_EQ(w1.ns_per_unit, 2'500'000);
  // 整数の ns に丸める: 3 個 / 1 ms = 3000 個/秒 -> 333,333.33 ns -> 333,333
  WorkerWindow r;
  r.units = 3;
  r.ns = 1 * kMs;
  EXPECT_EQ(combine_window(&r, 1, 1, 0).ns_per_unit, 333'333);
  r.units = 2;
  r.ns = 3;  // 1.5 ns -> 2 (四捨五入)
  EXPECT_EQ(combine_window(&r, 1, 1, 0).ns_per_unit, 2);
}

// ==== 偏りのないこと (作った時刻の列。PHASE1_PLAN.md P3.21 (1) の状況) ================

TEST(window_throughput_unbiased_with_mixed_core_speeds) {
  // 基準機の単位時間 (DESIGN.md §1.8 (1))。遅いコア 4 + 中間 3 + 速いコア 1 に、ワーカーが 1 本ずつ
  auto reference_device = [](std::int64_t slow, std::int64_t mid, std::int64_t fast) {
    std::vector<SimWorker> workers;
    for (int i = 0; i < 4; ++i) workers.push_back({slow, 0, 0, 0});
    for (int i = 0; i < 3; ++i) workers.push_back({mid, 0, 0, 0});
    workers.push_back({fast, 0, 0, 0});
    return workers;
  };
  // ワーカーがコアを 1 つずつ占めていれば、どの窓も (単位数 ÷ 実時間) が単位時間の逆数になり、誤差は出ない
  expect_unbiased(reference_device(4120 * kUs, 1460 * kUs, 1330 * kUs), 1e-9, 1e-9);  // 篩 (上限 100 万)
  expect_unbiased(reference_device(7050 * kUs, 720 * kUs, 670 * kUs), 1e-9, 1e-9);    // 行列積の片
}

TEST(window_throughput_unbiased_when_workers_share_cores) {
  // コアより多いワーカー (HONOR 端末は 8 本が 6 コアを分け合う。DESIGN.md §1.8 (3))。
  // 窓の端で待ちに当たったワーカーの分だけ、窓ごとの値は揺れる (ワーカーの間で打ち消し合う)。
  // 中央値は 0.5% 以内、窓ごとの値は 1% 以内で合う。この模型での値は、順に
  // 1.0002 (全窓)、0.9998〜1.0019 (中央値 1.0006)、0.9943〜1.0039 (中央値 1.0036)
  expect_unbiased(shared_cores(8, 1080 * kUs, 6 * kMs, 2 * kMs), 0.005, 0.01);    // 8 本が 6 コア。6 ms 動いて 2 ms 待つ
  expect_unbiased(shared_cores(8, 900 * kUs, 12 * kMs, 4 * kMs), 0.005, 0.01);    // 同。12 ms 動いて 4 ms 待つ
  expect_unbiased(shared_cores(16, 1500 * kUs, 4 * kMs, 12 * kMs), 0.005, 0.01);  // 16 本が 4 コア。4 ms 動いて 12 ms 待つ
}

TEST(window_unit_longer_than_window_gives_empty_windows) {
  // 1 単位 (250 ms) が窓 (200 ms) より長いと、単位が 1 つも終わらない窓ができる
  const auto ratios = simulate_ratios({{250 * kMs, 0, 0, 0}});
  int empty = 0;
  for (double r : ratios) {
    if (r == 0.0) {
      ++empty;
    } else {
      EXPECT_NEAR(r, 1.0, 1e-9);
    }
  }
  EXPECT_EQ(empty, 1);  // 1000 ms に終わった単位が、境目 800 ms と 1000 ms をまたぐ (2 つ目の窓が空になる)

  const auto bounds = make_bounds(0, 200 * kMs, 6);
  std::vector<WorkerWindow> records(6);
  simulate_worker({250 * kMs, 0, 0, 0}, bounds, records.data());
  const std::int64_t expected_units[6] = {1, 1, 1, 1, 0, 1};
  for (int k = 0; k < 6; ++k) {
    EXPECT_EQ(records[k].units, expected_units[k]);
    // 空の窓の「1 単位あたりの時間」は 0 (本計測の窓に 0 があれば、その回は無効にする)
    EXPECT_EQ(combine_window(records.data(), 1, 6, k).ns_per_unit, expected_units[k] ? 250 * kMs : 0);
  }
}

// ==== カーネル ==================================================================

TEST(sieve_timed_kernel_verifies_each_unit) {
  SieveTimedKernel k;  // 上限 100 万
  EXPECT_EQ(k.expected_checksum(), std::uint64_t{78'498});
  k.prepare(2);
  for (int w = 0; w < 2; ++w) {
    k.begin(w);
    EXPECT_TRUE(!k.checksum_ok(w));  // まだ 1 回も照合していない
    k.run_unit(w);
    EXPECT_TRUE(k.checksum_ok(w));
    EXPECT_EQ(k.observed_checksum(w), std::uint64_t{78'498});
    k.run_unit(w);  // 2 回目も同じ (作業領域の使い回しで結果が変わらない)
    k.finish(w);
    EXPECT_TRUE(k.checksum_ok(w));
    EXPECT_EQ(k.observed_checksum(w), std::uint64_t{78'498});
  }
  k.release();
  EXPECT_EQ(count_primes_simple(kSieveUnitLimit), kSieveUnitPrimeCount);  // 定数そのものの確認
}

TEST(sieve_timed_kernel_detects_wrong_count) {
  SieveTimedKernel k(1'000'000, 78'499);  // 正解を違う値にする
  k.prepare(1);
  k.begin(0);
  k.run_unit(0);
  EXPECT_TRUE(!k.checksum_ok(0));
  EXPECT_EQ(k.observed_checksum(0), std::uint64_t{78'498});  // 食い違った値が残る
  k.begin(0);  // begin で状態が戻る
  EXPECT_TRUE(!k.checksum_ok(0));
  k.release();

  SieveTimedKernel small(100, 25);  // 上限を変えても使える (診断モードの上限 4000 万と同じクラス)
  small.prepare(1);
  small.begin(0);
  small.run_unit(0);
  EXPECT_TRUE(small.checksum_ok(0));
}

TEST(matmul_checksum_rows_adds_up_to_whole) {
  for (int n : {64, 128, 192}) {
    std::vector<double> a(static_cast<size_t>(n) * n), b(a.size()), c(a.size(), 0.0);
    matmul_fill_inputs(a.data(), b.data(), n);
    matmul_blocked(a.data(), b.data(), c.data(), n, kMatmulBlock);
    std::uint64_t sum = 0;
    for (int i0 = 0; i0 < n; i0 += kMatmulBlock) sum += matmul_checksum_rows(c.data(), n, i0, i0 + kMatmulBlock);
    EXPECT_EQ(sum, matmul_checksum(c.data(), n));
    EXPECT_EQ(sum, matmul_expected_checksum(a.data(), b.data(), n));
    EXPECT_EQ(matmul_checksum_rows(c.data(), n, 0, n), matmul_checksum(c.data(), n));
    EXPECT_EQ(matmul_checksum_rows(c.data(), n, 5, 5), std::uint64_t{0});  // 空の範囲
  }
}

TEST(matmul_piece_all_pieces_equal_blocked) {
  for (int n : {64, 128, 192}) {
    std::vector<double> a(static_cast<size_t>(n) * n), b(a.size()), ref(a.size(), 0.0), c(a.size(), 0.0);
    matmul_fill_inputs(a.data(), b.data(), n);
    matmul_blocked(a.data(), b.data(), ref.data(), n, kMatmulBlock);
    for (int ii = 0; ii < n; ii += kMatmulBlock) {
      for (int kk = 0; kk < n; kk += kMatmulBlock) matmul_piece(a.data(), b.data(), c.data(), n, kMatmulBlock, ii, kk);
    }
    EXPECT_TRUE(c == ref);  // 整数値なので完全一致する
  }
}

TEST(matmul_piece_kernel_matches_blocked) {
  for (int n : {128, 192}) {
    std::vector<double> a(static_cast<size_t>(n) * n), b(a.size()), ref(a.size(), 0.0);
    matmul_fill_inputs(a.data(), b.data(), n);
    matmul_blocked(a.data(), b.data(), ref.data(), n, kMatmulBlock);

    MatmulPieceKernel k(n);
    k.prepare(1);
    const int pieces = (n / kMatmulBlock) * (n / kMatmulBlock);
    EXPECT_EQ(k.piece_count(), pieces);
    EXPECT_EQ(k.expected_checksum(), matmul_expected_checksum(a.data(), b.data(), n));
    k.begin(0);
    EXPECT_TRUE(!k.checksum_ok(0));  // まだ 1 回も照合していない
    for (int rep = 1; rep <= 2; ++rep) {  // 2 回続けても通る (行ブロックの最初の片がゼロに戻す)
      for (int p = 0; p < pieces; ++p) {
        EXPECT_EQ(k.next_piece(0), p);
        k.run_unit(0);
      }
      EXPECT_EQ(k.next_piece(0), 0);
      EXPECT_EQ(k.verified_count(0), static_cast<std::int64_t>(rep));
      EXPECT_TRUE(k.checksum_ok(0));
      EXPECT_EQ(k.observed_checksum(0), k.expected_checksum());
      EXPECT_TRUE(std::equal(ref.begin(), ref.end(), k.output(0)));
    }
    k.release();
  }
}

TEST(matmul_piece_kernel_finish_completes_partial_product) {
  const int n = 192;  // 9 片
  MatmulPieceKernel k(n);
  k.prepare(2);
  std::vector<double> a(static_cast<size_t>(n) * n), b(a.size()), ref(a.size(), 0.0);
  matmul_fill_inputs(a.data(), b.data(), n);
  matmul_blocked(a.data(), b.data(), ref.data(), n, kMatmulBlock);
  for (int stop_after : {1, 4, 8, 9, 13}) {  // 途中で止めて finish を呼ぶ
    for (int w = 0; w < 2; ++w) {
      k.begin(w);
      for (int p = 0; p < stop_after; ++p) k.run_unit(w);
      EXPECT_EQ(k.checksum_ok(w), stop_after >= 9);  // 行列積が 1 回完成するまでは、照合していない
      k.finish(w);
      EXPECT_EQ(k.next_piece(w), 0);
      EXPECT_EQ(k.verified_count(w), static_cast<std::int64_t>((stop_after + 8) / 9));
      EXPECT_TRUE(k.checksum_ok(w));
      EXPECT_EQ(k.observed_checksum(w), k.expected_checksum());
      EXPECT_TRUE(std::equal(ref.begin(), ref.end(), k.output(w)));  // ワーカーごとに別の出力
    }
  }
  // begin の直後の finish は、何も実行しない (照合していないので、通ったことにはならない)
  k.begin(0);
  k.finish(0);
  EXPECT_EQ(k.verified_count(0), std::int64_t{0});
  EXPECT_TRUE(!k.checksum_ok(0));
}

TEST(matmul_piece_kernel_detects_wrong_checksum) {
  MatmulPieceKernel k(128);
  k.prepare(1);
  const std::uint64_t right = k.expected_checksum();
  k.override_expected_checksum(right + 1);  // 正解を違う値にする
  k.begin(0);
  for (int p = 0; p < k.piece_count(); ++p) k.run_unit(0);
  EXPECT_TRUE(!k.checksum_ok(0));
  EXPECT_EQ(k.observed_checksum(0), right);  // 食い違った値が残る
  for (int p = 0; p < k.piece_count(); ++p) k.run_unit(0);
  EXPECT_TRUE(!k.checksum_ok(0));            // 1 回でも食い違ったら、偽のまま
  k.override_expected_checksum(right);
  k.begin(0);
  k.run_unit(0);
  k.finish(0);
  EXPECT_TRUE(k.checksum_ok(0));
}

TEST(matmul_piece_kernel_rejects_size_not_multiple_of_block) {
  MatmulPieceKernel k(100);
  bool thrown = false;
  try {
    k.prepare(1);
  } catch (const std::invalid_argument&) {
    thrown = true;
  }
  EXPECT_TRUE(thrown);
}

TEST(matmul_piece_kernel_full_size) {
  MatmulPieceKernel k;  // n=576
  k.prepare(1);
  EXPECT_EQ(k.piece_count(), 81);
  EXPECT_EQ(k.piece_count(), kMatmulPieceCount);
  k.begin(0);
  for (int p = 0; p < 81; ++p) k.run_unit(0);
  EXPECT_TRUE(k.checksum_ok(0));
  MatmulKernel whole;  // 現行の単位 (行列積 1 回) と、同じ値で照合している
  whole.prepare(1);
  EXPECT_EQ(k.observed_checksum(0), whole.unit_checksum());
  EXPECT_EQ(k.expected_checksum(), whole.unit_checksum());
  k.release();
}

// ==== TimedUnitTest (実スレッド。不変条件だけを検査する) ==============================

TEST(timed_unit_test_single_thread_invariants) {
  auto kernel = std::make_unique<FakeTimedKernel>(std::vector<std::int64_t>{100 * kUs});
  FakeTimedKernel* k = kernel.get();
  TimedUnitTest test(std::move(kernel), /*multithread=*/false);
  test.setup(context(8));  // シングルは、ctx.threads に関係なく 1 本
  const WindowedRun run = test.run_windows(kTestWindowNs, kTestWarmup, kTestMeasured);
  expect_invariants(test, *k, run, 1);
  EXPECT_EQ(test.expected_checksum(), std::uint64_t{42});
  EXPECT_EQ(test.observed_checksum(), std::uint64_t{42});
  test.teardown();
}

TEST(timed_unit_test_two_workers_invariants) {
  auto kernel = std::make_unique<FakeTimedKernel>(std::vector<std::int64_t>{100 * kUs});
  FakeTimedKernel* k = kernel.get();
  TimedUnitTest test(std::move(kernel), /*multithread=*/true);
  test.setup(context(2));
  const WindowedRun run = test.run_windows(kTestWindowNs, kTestWarmup, kTestMeasured);
  expect_invariants(test, *k, run, 2);
  test.teardown();
}

TEST(timed_unit_test_can_run_again) {
  // 同じテストで続けて実行しても、前の回の記録が残らない (測り直しと、ピーク → 持続で起きる)
  auto kernel = std::make_unique<FakeTimedKernel>(std::vector<std::int64_t>{100 * kUs});
  FakeTimedKernel* k = kernel.get();
  TimedUnitTest test(std::move(kernel), /*multithread=*/true);
  test.setup(context(2));
  for (int rep = 1; rep <= 2; ++rep) {
    const WindowedRun run = test.run_windows(kTestWindowNs, 0, 2);  // ウォームアップなしでも動く
    EXPECT_EQ(run.windows.size(), size_t{2});
    EXPECT_EQ(test.window_count(), 2);
    EXPECT_TRUE(run.checksum_ok);
    for (int w = 0; w < 2; ++w) {
      EXPECT_EQ(test.worker_window(w, 0).units + test.worker_window(w, 1).units, k->units(w));
      EXPECT_EQ(k->begins(w), rep);
      EXPECT_EQ(k->finishes(w), rep);
    }
  }
  test.teardown();
}

TEST(timed_unit_test_more_workers_than_cores_invariants) {
  // コアより多いワーカー (論理コア数 × 2 + 1 本)。順番待ちで単位が長くなっても、数え方の不変条件は成り立つ
  const int workers = mb::platform::logical_cpu_count() * 2 + 1;
  auto kernel = std::make_unique<FakeTimedKernel>(std::vector<std::int64_t>{100 * kUs});
  FakeTimedKernel* k = kernel.get();
  TimedUnitTest test(std::move(kernel), /*multithread=*/true);
  test.setup(context(workers));
  const WindowedRun run = test.run_windows(kTestWindowNs, kTestWarmup, kTestMeasured);
  expect_invariants(test, *k, run, workers);
  test.teardown();
}

TEST(timed_unit_test_reports_checksum_failure) {
  auto kernel = std::make_unique<FakeTimedKernel>(std::vector<std::int64_t>{100 * kUs});
  kernel->set_bad_worker(1);  // ワーカー 1 だけ照合が通らない
  TimedUnitTest test(std::move(kernel), /*multithread=*/true);
  test.setup(context(2));
  const WindowedRun run = test.run_windows(kTestWindowNs, kTestWarmup, kTestMeasured);
  EXPECT_EQ(run.windows.size(), size_t{4});
  EXPECT_TRUE(!run.checksum_ok);
  EXPECT_EQ(test.observed_checksum(), std::uint64_t{41});
  test.teardown();
}

TEST(timed_unit_test_rejects_bad_arguments) {
  TimedUnitTest test(std::make_unique<FakeTimedKernel>(std::vector<std::int64_t>{100 * kUs}), /*multithread=*/false);
  test.setup(context(1));
  for (const WindowedRun& run : {test.run_windows(0, 1, 3), test.run_windows(kTestWindowNs, 0, 0),
                                 test.run_windows(kTestWindowNs, -1, 3), test.run_windows(-5, 1, 3)}) {
    EXPECT_TRUE(run.windows.empty());
    EXPECT_TRUE(!run.checksum_ok);
  }
  test.teardown();
}

TEST(timed_unit_test_sums_rates_of_workers_with_different_speeds) {
  // ワーカー 0 は 200 µs ごと、ワーカー 1 は 500 µs ごとに 1 単位。合計は 5000 + 2000 = 7000 個/秒。
  // 絶対値を見る唯一のテストなので、許容差を広く取り (±20%)、本計測の窓の中央値で比べる。
  // 共有のランナーでスレッドが長く止められた回に備えて、3 回まで試す (数え方の誤りなら、3 回とも外れる)
  auto kernel = std::make_unique<FakeTimedKernel>(std::vector<std::int64_t>{200 * kUs, 500 * kUs});
  FakeTimedKernel* k = kernel.get();
  TimedUnitTest test(std::move(kernel), /*multithread=*/true);
  test.setup(context(2));
  const double expected = 7000.0;
  bool within = false;
  double last = 0.0;
  for (int attempt = 0; attempt < 3 && !within; ++attempt) {
    const WindowedRun run = test.run_windows(kTestWindowNs, kTestWarmup, kTestMeasured);
    expect_invariants(test, *k, run, 2, attempt + 1);
    if (run.windows.size() != size_t{4}) break;
    std::vector<double> rates;
    for (int i = kTestWarmup; i < kTestWarmup + kTestMeasured; ++i) {
      rates.push_back(run.windows[static_cast<size_t>(i)].units_per_sec);
    }
    std::sort(rates.begin(), rates.end());
    last = rates[rates.size() / 2];
    within = last > expected * 0.8 && last < expected * 1.2;
  }
  if (!within) std::fprintf(stderr, "    median units_per_sec = %.1f (expected %.1f +-20%%)\n", last, expected);
  EXPECT_TRUE(within);
  test.teardown();
}

TEST(timed_unit_test_runs_real_kernels) {
  // 篩 (上限 100 万) のシングル。照合が通り、窓の数が合う
  {
    TimedUnitTest test(std::make_unique<SieveTimedKernel>(), /*multithread=*/false);
    test.setup(context(4));
    const WindowedRun run = test.run_windows(kTestWindowNs, kTestWarmup, kTestMeasured);
    EXPECT_EQ(run.windows.size(), size_t{4});
    EXPECT_TRUE(run.checksum_ok);
    EXPECT_EQ(test.expected_checksum(), std::uint64_t{78'498});
    EXPECT_EQ(test.observed_checksum(), std::uint64_t{78'498});
    test.teardown();
  }
  // 行列積の片 (n=576) のマルチ (2 本)。窓の終わりで途中の行列積は、計測の外で完成して照合される
  {
    auto kernel = std::make_unique<MatmulPieceKernel>();
    MatmulPieceKernel* k = kernel.get();
    TimedUnitTest test(std::move(kernel), /*multithread=*/true);
    test.setup(context(2));
    const WindowedRun run = test.run_windows(kTestWindowNs, kTestWarmup, kTestMeasured);
    EXPECT_EQ(run.windows.size(), size_t{4});
    EXPECT_TRUE(run.checksum_ok);
    for (int w = 0; w < 2; ++w) {
      EXPECT_EQ(k->next_piece(w), 0);
      EXPECT_TRUE(k->verified_count(w) >= 1);
      std::int64_t units = 0;
      for (int i = 0; i < 4; ++i) units += test.worker_window(w, i).units;
      // 窓に数えた単位 (片) に、計測の外で完成させた分を足すと、行列積の整数回ぶんになる
      EXPECT_TRUE(units >= 1 && units <= k->verified_count(w) * 81);
      EXPECT_TRUE(units > (k->verified_count(w) - 1) * 81);
    }
    EXPECT_EQ(test.observed_checksum(), test.expected_checksum());
    test.teardown();
  }
}

TEST(timed_unit_test_inload_probe_keeps_invariants) {
  // 診断用の、負荷中のクロックの記録を入れても、数え方は変わらない
  auto kernel = std::make_unique<FakeTimedKernel>(std::vector<std::int64_t>{100 * kUs});
  FakeTimedKernel* k = kernel.get();
  TimedUnitTest test(std::move(kernel), /*multithread=*/true);
  test.setup(context(2));
  test.enable_inload_probe(true);
  const WindowedRun run = test.run_windows(kTestWindowNs, kTestWarmup, kTestMeasured);
  expect_invariants(test, *k, run, 2);
  for (int w = 0; w < 2; ++w) {
    EXPECT_TRUE(test.load_ghz_avg(w) >= 0.0);  // 対応していないアーキテクチャでは 0
    EXPECT_TRUE(test.load_ghz_max(w) >= test.load_ghz_avg(w));
  }
  test.enable_inload_probe(false);
  const WindowedRun again = test.run_windows(kTestWindowNs, 0, 1);
  EXPECT_TRUE(again.checksum_ok);
  for (int w = 0; w < 2; ++w) EXPECT_EQ(test.load_ghz_max(w), 0.0);  // 指定を外すと、記録は 0 に戻る
  test.teardown();
}
