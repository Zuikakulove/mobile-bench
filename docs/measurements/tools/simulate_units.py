#!/usr/bin/env python3
"""固定仕事量方式と固定時間方式の誤差のシミュレーション (docs/DESIGN.md §1.6(b))。

コアごとの「1 単位の時間 (ms)」を与え、スレッドの置き方の模型を 3 通り比べる。
1 つのコアで同時に動くスレッドは 1 つまで。時間はイベントごとに進める。

  pinned : スレッドは最初に置かれたコアから動かない (2026-11-10 の版と同じ模型)
  pull   : 速いコアが空いたら、最も遅いコアで実行中のスレッドをそこへ移す
  rotate : pull に加えて、一定間隔でスレッドとコアの対応を 1 つずつずらす
           (全スレッドが全コアを順に回る)

実機のスケジューラそのものではない。傾向を見るための模型である。

使い方:
  python3 -I simulate_units.py          # 全部 (約 30 秒)
  python3 -I simulate_units.py check    # 2026-11-10 の版の表の検算
  python3 -I simulate_units.py work     # 固定仕事量方式 (基準機の実測値)
  python3 -I simulate_units.py time     # 固定時間方式 (基準機の実測値)
"""
import random
import statistics
import sys

# 基準機 (Nothing Phone (1)) の 1 単位の時間 (ms)。並びは cpu0-3 / cpu4-6 / cpu7。
# 出典は docs/measurements/2026-11-12-nothing-phone-1/ (0.2.0 の診断モード)。
SIEVE_MS = [143.0] * 4 + [65.0] * 3 + [62.4]  # 篩 (上限 4000 万)。3 種類とも実測
# 行列積 n=576: cpu7 は実測 (53.8)、cpu4-6 は周波数の比で換算 (56.4)。
# cpu0-3 は未測定。n=128 の実測 (0.68 GFLOPS) から 457〜560 ms と仮定して、両端を試す。
MATMUL_LITTLE_MS = (457.0, 560.0)


def matmul_ms(little):
    return [little] * 4 + [56.4] * 3 + [53.8]


class Sim:
    """n 本のスレッドが n 個のコアで単位を処理する。コアとスレッドは常に 1 対 1。"""

    def __init__(self, unit_ms, model, rng, jitter=0.01, rotate_ms=16.0, start_ms=2.0):
        self.unit_ms = unit_ms
        self.n = len(unit_ms)
        self.model = model
        self.rng = rng
        self.jitter = jitter            # 単位ごとの時間のゆらぎ (±1%)
        self.rotate_ms = rotate_ms      # rotate の入れ替え間隔
        self.core_of = list(range(self.n))
        rng.shuffle(self.core_of)
        self.start = [rng.uniform(0.0, start_ms) for _ in range(self.n)]  # 起床の遅れ。None = 開始済み
        self.running = [False] * self.n
        self.remaining = [0.0] * self.n  # 実行中の単位の残り (1.0 = 1 単位)
        self.t = 0.0
        self.next_rotate = rotate_ms * rng.random() if model == "rotate" else float("inf")
        self.by_speed = sorted(range(self.n), key=lambda c: unit_ms[c])

    def _begin_unit(self, w):
        self.remaining[w] = 1.0 + self.rng.uniform(-self.jitter, self.jitter)
        self.running[w] = True

    def _rebalance(self):
        """空いている速いコアへ、最も遅いコアで実行中のスレッドを移す。"""
        if self.model == "pinned":
            return
        while True:
            runners = [w for w in range(self.n) if self.running[w]]
            busy = {self.core_of[w] for w in runners}
            free = [c for c in self.by_speed if c not in busy]
            if not free or not runners:
                return
            slow = max(runners, key=lambda w: self.unit_ms[self.core_of[w]])
            fast = free[0]
            if self.unit_ms[fast] >= self.unit_ms[self.core_of[slow]] * 0.99:
                return
            owner = self.core_of.index(fast)  # そのコアを持っている (止まっている) スレッドと交換する
            self.core_of[owner], self.core_of[slow] = self.core_of[slow], self.core_of[owner]

    def step(self, on_start, on_complete):
        """次のイベントまで進める。コールバックは、次の単位を始めるなら True を返す。"""
        if not any(self.running) and all(s is None for s in self.start):
            return False
        dt = self.next_rotate - self.t
        for w in range(self.n):
            if self.running[w]:
                dt = min(dt, self.remaining[w] * self.unit_ms[self.core_of[w]])
            elif self.start[w] is not None:
                dt = min(dt, max(0.0, self.start[w] - self.t))
        for w in range(self.n):
            if self.running[w]:
                self.remaining[w] -= dt / self.unit_ms[self.core_of[w]]
        self.t += dt
        eps = 1e-9
        changed = False
        for w in range(self.n):
            if self.running[w]:
                if self.remaining[w] <= eps:
                    if on_complete(w):
                        self._begin_unit(w)
                    else:
                        self.running[w] = False
                        changed = True
            elif self.start[w] is not None and self.t >= self.start[w] - eps:
                self.start[w] = None
                changed = True
                if on_start(w):
                    self._begin_unit(w)
        if self.t >= self.next_rotate - eps:
            self.core_of = self.core_of[1:] + self.core_of[:1]
            self.next_rotate += self.rotate_ms
            changed = True
        if changed:
            self._rebalance()
        return True


def fixed_work(unit_ms, total_units, model, rng):
    """現行方式: 全スレッドが共有カウンタから 1 単位ずつ取り合う。
    戻り値: (経過 ms, スレッドごとの単位数, スレッドごとの処理時間 ms)"""
    sim = Sim(unit_ms, model, rng)
    taken = [0]
    units = [0] * sim.n
    began = [0.0] * sim.n
    busy = [0.0] * sim.n

    def take(w):
        if taken[0] >= total_units:
            return False
        taken[0] += 1
        began[w] = sim.t
        return True

    def complete(w):
        units[w] += 1
        busy[w] += sim.t - began[w]
        return take(w)

    while sim.step(take, complete):
        pass
    return sim.t, units, busy


def fixed_time_stopping(unit_ms, window_ms, model, rng, windows=5):
    """固定時間方式 (窓ごとに全スレッドが止まる)。各スレッドは、締切を過ぎたら実行中の単位を終えて止まる。
    戻り値: 窓ごとの「計測したスループット / 真のスループット」の中央値を 2 通りと、はみ出しの最大 ms。
      discard  : 締切までに完了した単位だけを数え、窓の長さで割る (実行中の単位は捨てる)
      complete : 実行中の単位も完了させて数え、スレッドごとの実時間で割る"""
    true_rate = sum(1.0 / u for u in unit_ms)
    n = len(unit_ms)
    discard, completed, overshoot = [], [], 0.0
    for _ in range(windows):
        sim = Sim(unit_ms, model, rng, start_ms=0.0)
        in_window = [0] * n
        total = [0] * n
        stop_t = [0.0] * n

        def complete(w):
            total[w] += 1
            if sim.t <= window_ms:
                in_window[w] += 1
            if sim.t >= window_ms:
                stop_t[w] = sim.t
                return False
            return True

        while sim.step(lambda w: True, complete):
            pass
        discard.append(sum(in_window) / window_ms / true_rate)
        completed.append(sum(total[w] / stop_t[w] for w in range(n)) / true_rate)
        overshoot = max(overshoot, max(stop_t) - window_ms)
    return statistics.median(discard), statistics.median(completed), overshoot


def fixed_time_continuous(unit_ms, window_ms, model, rng, windows=5):
    """固定時間方式 (ウォームアップ 1 窓の後、windows 個の窓を止めずに続ける)。
    スレッドごとに、窓の境目をまたいだ単位が終わった時点で区切り、その間の (単位数 / 実時間) を合計する。
    戻り値: 窓ごとの「計測したスループット / 真のスループット」の中央値"""
    true_rate = sum(1.0 / u for u in unit_ms)
    n = len(unit_ms)
    sim = Sim(unit_ms, model, rng, start_ms=0.0)
    bounds = [window_ms * (k + 1) for k in range(windows + 1)]  # 窓の境目 (最初の 1 窓はウォームアップ)
    count = [0] * n                   # スレッドごとの累計の単位数
    crossed = [[] for _ in range(n)]  # 境目をまたいだ単位が終わった (時刻, 累計)

    def complete(w):
        count[w] += 1
        while len(crossed[w]) <= windows and sim.t >= bounds[len(crossed[w])]:
            crossed[w].append((sim.t, count[w]))
        return len(crossed[w]) <= windows

    while sim.step(lambda w: True, complete):
        pass
    estimates = []
    for k in range(windows):
        rate = 0.0
        for w in range(n):
            (t0, c0), (t1, c1) = crossed[w][k], crossed[w][k + 1]
            if t1 > t0:  # 1 単位が窓より長いと、単位が 1 つも終わらない窓ができる
                rate += (c1 - c0) / (t1 - t0)
        estimates.append(rate / true_rate)
    return statistics.median(estimates)


def report_fixed_work(name, unit_ms, totals=(16, 64, 256), trials=400, seed=1, detail=False):
    ideal = sum(1.0 / u for u in unit_ms)
    print(f"[{name}] 理想は {ideal * 1000:.1f} 単位/秒")
    for model in ("pinned", "pull", "rotate"):
        cells = []
        for total in totals:
            rng = random.Random(seed)
            ratios, patterns, idle = [], {}, []
            for _ in range(trials):
                elapsed, units, busy = fixed_work(unit_ms, total, model, rng)
                ratios.append(elapsed / (total / ideal))
                idle.append(min(busy) / elapsed)
                key = f"{min(units)}-{max(units)}"
                patterns[key] = patterns.get(key, 0) + 1
            ratios.sort()
            cell = (f"{total:3d} 単位 {statistics.median(ratios):.2f} "
                    f"({ratios[len(ratios) // 20]:.2f}-{ratios[-1 - len(ratios) // 20]:.2f})")
            if detail and total == totals[0]:
                cell += f" 単位数 {dict(sorted(patterns.items()))} busy最小/経過 {statistics.median(idle):.2f}"
            cells.append(cell)
        print(f"  {model:6s} " + " | ".join(cells))


def report_fixed_time(name, unit_ms, window_ms=200.0, trials=20, seed=1):
    print(f"[{name}] 単位: 遅いコア {max(unit_ms):.2f} ms / 速いコア {min(unit_ms):.2f} ms "
          f"(窓 {window_ms:.0f} ms に、遅いコアで {window_ms / max(unit_ms):.1f} 個)")
    for model in ("pinned", "rotate"):
        rng = random.Random(seed)
        d, c, o, g = [], [], [], []
        for _ in range(trials):
            a, b, over = fixed_time_stopping(unit_ms, window_ms, model, rng)
            d.append(a)
            c.append(b)
            o.append(over)
            g.append(fixed_time_continuous(unit_ms, window_ms, model, rng))
        print(f"  {model:6s} 捨てる {statistics.mean(d):.3f} | "
              f"完了させて実時間で割る {statistics.mean(c):.3f} ({min(c):.3f}-{max(c):.3f}) "
              f"はみ出し 最大 {max(o):.1f} ms (窓の {max(o) / window_ms * 100:.1f}%) | "
              f"止めずに続ける {statistics.mean(g):.3f} ({min(g):.3f}-{max(g):.3f})")


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    if what in ("all", "check"):
        print("=== 検算: 2026-11-10 の版の表 (固定仕事量方式。経過時間 ÷ 理想の時間。中央値と 5〜95% の範囲)")
        for ratio in (2.0, 2.5, 3.0, 3.5, 4.0):
            report_fixed_work(f"速いコア 4 + 遅いコア 4、速度比 {ratio}", [60.0] * 4 + [60.0 * ratio] * 4, trials=100)
        for ratio in (2.5, 3.0, 3.5):
            report_fixed_work(f"速いコア 2 + 遅いコア 6、速度比 {ratio}", [60.0] * 2 + [60.0 * ratio] * 6, trials=100)
        report_fixed_work("速いコア 2 + やや遅いコア 6、速度比 1.3", [60.0] * 2 + [78.0] * 6, trials=100)
        report_fixed_work("同じコア 8", [60.0] * 8, trials=100)
        print()
    if what in ("all", "work"):
        print("=== 固定仕事量方式: 経過時間 ÷ 理想の時間 (基準機。中央値と 5〜95% の範囲)")
        report_fixed_work("篩 (実測の単位時間)", SIEVE_MS, detail=True)
        for little in MATMUL_LITTLE_MS:
            report_fixed_work(f"行列積 n=576 (遅いコアを {little:.0f} ms と仮定)", matmul_ms(little), detail=True)
        print()
    if what in ("all", "time"):
        print("=== 固定時間方式: 計測したスループット ÷ 真のスループット (基準機。5 窓の中央値)")
        report_fixed_time("篩 現行の単位 (上限 4000 万)", SIEVE_MS)
        for divisor, label in ((19, "上限 200 万"), (37, "上限 100 万"), (66, "上限 50 万")):
            report_fixed_time(f"篩 {label} (現行の 1/{divisor} と仮定)", [u / divisor for u in SIEVE_MS])
        for little in MATMUL_LITTLE_MS:
            for pieces in (1, 81, 243):
                label = "現行の単位" if pieces == 1 else f"{pieces} 等分"
                report_fixed_time(f"行列積 {label} (遅いコアを {little:.0f} ms と仮定)",
                                  [u / pieces for u in matmul_ms(little)])


if __name__ == "__main__":
    main()
