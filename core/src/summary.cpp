// 結果の要約テキスト。JSON 全体は大きく (診断モードで数十 KB)、チャット等に貼ると
// 途中で切れるため、切り分けに必要な情報を 1 回 1 行に詰めたテキストを作る。
// 貼り付けは 10,000 文字で切れたことがあるので (docs/DESIGN.md §1.7 (8))、
// 1 パート 8,000 バイト (UTF-8) 以内の複数のパートに分ける (§5.6)。
//   - 各パートの先頭に見出し (実行を見分ける情報と part i/n)、末尾に終端の行
//     "END part i/n chars=N" を入れる。N は見出しから終端の行の直前まで (改行を含む) の文字数。
//     終端の行がなければ、そのパートは切れている
//   - テスト 1 本ぶんの行と、診断の 1 項目 (クラスタ別はクラスタ 1 つぶん) は途中で切らない
//   - 端末の状態 (cond) は先頭で 1 回全項目を出し (cond(base))、各テストでは base と違う項目だけを出す
//   - 周波数の上限 (cap) は、最大周波数 (topology) と違うときだけ出す
// 書式を変えたら docs/measurements/tools/analyze_summary.py も直すこと。
// 小数はロケール非依存の JsonWriter::format_fixed で整形する。
#include "summary.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "json_writer.h"
#include "mobilebench/result.h"

namespace mb {

namespace {

std::string fixed(double v, int decimals) { return detail::JsonWriter::format_fixed(v, decimals); }
std::string ms(std::int64_t ns) { return fixed(static_cast<double>(ns) / 1e6, 1); }

// 連続する同じ値をまとめる: [2918400 x6, 4320000 x2] (kHz) -> "2918x6 4320x2" (MHz)
template <class T, class F>
std::string groups(const std::vector<T>& values, F format) {
  if (values.empty()) return "-";
  std::string out;
  size_t i = 0;
  while (i < values.size()) {
    size_t j = i;
    while (j < values.size() && values[j] == values[i]) ++j;
    if (!out.empty()) out += ' ';
    out += format(values[i]);
    if (j - i > 1) out += "x" + std::to_string(j - i);
    i = j;
  }
  return out;
}

std::string mhz_groups(const std::vector<std::int64_t>& khz) {
  return groups(khz, [](std::int64_t v) { return v < 0 ? std::string("?") : std::to_string(v / 1000); });
}

// [0,1,2,3,6] -> "0-3,6"
std::string cpu_ranges(std::vector<int> cpus) {
  std::sort(cpus.begin(), cpus.end());
  cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
  cpus.erase(std::remove_if(cpus.begin(), cpus.end(), [](int c) { return c < 0; }), cpus.end());
  if (cpus.empty()) return "?";
  std::string out;
  size_t i = 0;
  while (i < cpus.size()) {
    size_t j = i;
    while (j + 1 < cpus.size() && cpus[j + 1] == cpus[j] + 1) ++j;
    if (!out.empty()) out += ',';
    out += std::to_string(cpus[i]);
    if (j > i) out += "-" + std::to_string(cpus[j]);
    i = j + 1;
  }
  return out;
}

std::string conditions(const Conditions& c) {
  if (c.empty()) return "-";
  std::string out;
  for (const auto& [k, v] : c) {
    if (!out.empty()) out += ' ';
    out += k + "=" + v;
  }
  return out;
}

// base と違う項目だけを "k=v" で並べる。base にあって c にない項目は "k=(none)"。違いがなければ "="
std::string conditions_diff(const Conditions& c, const Conditions& base) {
  std::string out;
  auto add = [&out](const std::string& item) {
    if (!out.empty()) out += ' ';
    out += item;
  };
  for (const auto& [k, v] : c) {
    auto it = base.find(k);
    if (it == base.end() || it->second != v) add(k + "=" + v);
  }
  for (const auto& [k, v] : base) {
    if (c.find(k) == c.end()) add(k + "=(none)");
  }
  return out.empty() ? "=" : out;
}

std::string get(const std::map<std::string, std::string>& m, const char* key) {
  auto it = m.find(key);
  return it == m.end() ? "?" : it->second;
}

std::string run_line(size_t index, int warmup_count, const RunDetail& d, const std::vector<std::int64_t>& max_khz) {
  const auto& ws = d.telemetry.workers;
  std::string label = d.warmup ? "W " : "#" + std::to_string(static_cast<int>(index) - warmup_count + 1);
  std::vector<int> cpus;
  double before = 0, after = 0, after_min = 0, after_max = 0;
  int n = 0;
  for (const auto& w : ws) {
    cpus.insert(cpus.end(), w.cpus.begin(), w.cpus.end());
    if (w.ghz_after > 0) {
      before += w.ghz_before;
      after += w.ghz_after;
      after_min = n == 0 ? w.ghz_after : std::min(after_min, w.ghz_after);
      after_max = n == 0 ? w.ghz_after : std::max(after_max, w.ghz_after);
      ++n;
    }
  }
  std::string line = "  " + label + " " + ms(d.ns) + "ms cpu" + cpu_ranges(cpus);
  if (n > 0) {
    line += " ghz " + fixed(before / n, 2) + ">" + fixed(after / n, 2);
    if (ws.size() > 1) line += "(" + fixed(after_min, 2) + "-" + fixed(after_max, 2) + ")";
  }
  line += " cur " + mhz_groups(d.cpu_khz_after);
  // 周波数の上限は、最大周波数と違うときだけ出す (読めない端末では出さない)
  if (!d.cpu_cap_khz.empty() && d.cpu_cap_khz != max_khz) line += " cap " + mhz_groups(d.cpu_cap_khz);
  if (!d.telemetry.unit_ns.empty()) {
    line += " u";
    for (auto ns : d.telemetry.unit_ns) line += " " + ms(ns);
  } else if (ws.size() > 1) {
    int umin = ws[0].units, umax = ws[0].units;
    std::int64_t bmin = ws[0].busy_ns, bmax = ws[0].busy_ns;
    for (const auto& w : ws) {
      umin = std::min(umin, w.units);
      umax = std::max(umax, w.units);
      bmin = std::min(bmin, w.busy_ns);
      bmax = std::max(bmax, w.busy_ns);
    }
    line += " units " + std::to_string(umin) + "-" + std::to_string(umax) + " busy " + ms(bmin) + "-" + ms(bmax) + "ms";
  }
  return line;
}

// 開始時の端末の状態 (各テストの cond はこれとの差だけを出す)
const Conditions& base_conditions(const SuiteResult& r) {
  if (!r.tests.empty()) return r.tests.front().conditions_before;
  return r.diagnostics.conditions_before;
}

std::size_t utf8_prefix_bytes(const std::string& s, std::size_t max_bytes) {
  if (s.size() <= max_bytes) return s.size();
  std::size_t n = max_bytes;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;  // 文字の途中で切らない
  return n;
}

}  // namespace

namespace detail {

std::size_t utf8_char_count(const std::string& s) {
  std::size_t n = 0;
  for (unsigned char c : s) {
    if ((c & 0xC0) != 0x80) ++n;
  }
  return n;
}

std::string summary_run_header(const SuiteResult& r) {
  return "MobileBench v" + r.benchmark_version + " | app " + get(r.device, "app_version") + " build " +
         get(r.device, "app_build") + " (" + get(r.device, "git_sha") + ") | run " +
         (r.started_utc.empty() ? std::string("?") : r.started_utc);
}

std::vector<SummaryBlock> summary_blocks(const SuiteResult& r) {
  std::vector<SummaryBlock> blocks;
  const Conditions& base = base_conditions(r);

  // ---- 先頭の情報
  {
    SummaryBlock b;
    const std::string compiler = r.build.compiler.substr(0, r.build.compiler.find(" ("));
    b.push_back("device: " + get(r.device, "manufacturer") + " " + get(r.device, "model") +
                " soc=" + get(r.device, "soc_model") + " " + get(r.device, "platform") + " " +
                get(r.device, "os_version") + " build=" + get(r.device, "os_build") + " | " + r.build.arch + " " +
                compiler + " " + r.build.flags + (r.build.optimized ? "" : " NOT-OPTIMIZED"));
    // OS のビルドを特定する補助の情報 (プラットフォーム層が渡したものだけ。docs/DESIGN.md D2)
    std::string os;
    for (const char* key : {"os_build_id", "os_incremental", "os_security_patch", "os_version_string", "os_fingerprint"}) {
      auto it = r.device.find(key);
      if (it == r.device.end() || it->second.empty()) continue;
      os += std::string(" ") + (key + 3) + "=" + it->second;  // "os_" を除く
    }
    if (!os.empty()) b.push_back("os" + os);
    std::string scores;
    for (const auto& ph : r.phases) {
      scores += ph.name + " " + fixed(ph.score, 1) + (ph.valid ? "" : "(invalid)") + (ph.stable ? "" : "(unstable)") + " ";
    }
    b.push_back("scores " + scores + "retention " + fixed(r.retention * 100.0, 1) + "% valid=" +
                (r.valid ? "yes" : "no") + (r.cancelled ? " CANCELLED" : "") +
                " cores=" + std::to_string(r.logical_cores) + " diag=" + (r.diagnostics_enabled ? "yes" : "no"));
    b.push_back("baseline " + fixed(r.baseline_ghz, 2) + "GHz(all-core) precondition " +
                std::to_string(r.precondition.ms) + "ms -> " + fixed(r.precondition.ghz, 2) + "GHz" +
                (r.precondition.settled ? " settled" : " not-settled"));
    b.push_back("topology max_mhz " + mhz_groups(r.topology.max_khz) + " capacity " +
                groups(r.topology.capacity, [](int v) { return std::to_string(v); }));
    b.push_back("columns: run time cpu ghz(before>after) cur(after,MHz) [cap(MHz), only if != max_mhz] | "
                "ST: unit ms | MT: units/worker busy");
    if (!base.empty()) {
      b.push_back("cond(base) " + conditions(base));
      b.push_back("cond: only keys that differ from cond(base) ('=' same as base)");
    }
    blocks.push_back(std::move(b));
  }

  // ---- テスト (1 本ずつ)
  for (const auto& t : r.tests) {
    SummaryBlock b;
    std::string wait;
    if (t.wait_ms >= 0) {
      wait = " wait " + std::to_string(t.wait_ms) + "ms" + (t.recovered ? "" : "(NOT recovered)") + " rec " +
             fixed(t.recovery_ghz, 2) + "G";
    }
    b.push_back("[" + t.phase + " " + t.id + "] median " + ms(t.median_ns) + "ms score " + fixed(t.score, 1) +
                " threads " + std::to_string(t.threads) + " @" + fixed(t.ghz, 2) + "G spread " +
                fixed(t.spread * 100.0, 1) + "%" + (t.stable ? "" : " UNSTABLE") + " attempts " +
                std::to_string(t.attempts) + wait + (t.valid ? "" : " INVALID") +
                (t.error.empty() ? "" : " error=" + t.error));
    const std::string before = conditions_diff(t.conditions_before, base);
    const std::string after = conditions_diff(t.conditions_after, base);
    if (before != "=" || after != "=") b.push_back("  cond " + before + " -> " + after);
    for (size_t i = 0; i < t.run_details.size(); ++i) {
      b.push_back(run_line(i, r.warmup_runs, t.run_details[i], r.topology.max_khz));
    }
    blocks.push_back(std::move(b));
  }

  if (!r.diagnostics_enabled) return blocks;
  const auto& d = r.diagnostics;

  // ---- 診断: ブースト (250ms ごとの区間に分け、区間内の中央値 (外れ値に強い) を出す)
  {
    SummaryBlock b;
    std::string cond = "[diagnostics]";
    if (!base.empty()) {
      cond += " cond " + conditions_diff(d.conditions_before, base) + " -> " + conditions_diff(d.conditions_after, base);
    }
    b.push_back(cond);
    for (const auto& bt : d.boost) {
      std::string segs;
      constexpr std::int64_t kBucketMs = 250;
      size_t i = 0;
      while (i < bt.samples.size()) {
        const std::int64_t bucket = bt.samples[i].t_ms / kBucketMs;
        std::vector<double> ghz, unit;
        std::vector<int> cpus;
        size_t j = i;
        for (; j < bt.samples.size() && bt.samples[j].t_ms / kBucketMs == bucket; ++j) {
          ghz.push_back(bt.samples[j].ghz);
          unit.push_back(bt.samples[j].unit_ms);
          cpus.push_back(bt.samples[j].cpu);
        }
        auto med = [](std::vector<double> v) {
          std::sort(v.begin(), v.end());
          return v[v.size() / 2];
        };
        if (!segs.empty()) segs += " | ";
        segs += std::to_string(bucket * kBucketMs) + ":" + fixed(med(ghz), 2) + "G u" + fixed(med(unit), 1) + " cpu" +
                cpu_ranges(cpus);
        i = j;
      }
      b.push_back("  boost idle " + std::to_string(bt.idle_ms) + "ms (t_ms:GHz unit_ms cpu) " + (segs.empty() ? "-" : segs));
    }
    blocks.push_back(std::move(b));
  }

  // ---- 診断: 状態を確かめるクロックの 2 つの求め方 (§6 V22)
  {
    const auto& cp = d.clock_probe;
    SummaryBlock b;
    b.push_back("  probe rest " + std::to_string(cp.rest_ms) + "ms load " + std::to_string(cp.load_ms) +
                "ms workers " + std::to_string(cp.workers) + " | current = mean of per-worker estimate after load,"
                " counted = adds in " + std::to_string(cp.count_from_ms) + "-" + std::to_string(cp.load_ms) +
                "ms / time (GHz, per-worker min-max)");
    std::vector<double> cur, cnt;
    for (const auto& s : cp.samples) {
      b.push_back("    " + std::to_string(s.t_ms) + "ms current " + fixed(s.current_ghz, 3) + " (" +
                  fixed(s.current_min, 2) + "-" + fixed(s.current_max, 2) + ") counted " + fixed(s.counted_ghz, 3) +
                  " (" + fixed(s.counted_min, 2) + "-" + fixed(s.counted_max, 2) + ")");
      if (s.current_ghz > 0) cur.push_back(s.current_ghz);
      if (s.counted_ghz > 0) cnt.push_back(s.counted_ghz);
    }
    auto range = [](const std::vector<double>& v) {
      if (v.empty()) return std::string("-");
      const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
      double sum = 0.0;
      for (double x : v) sum += x;
      const double mean = sum / static_cast<double>(v.size());
      return fixed(mean, 3) + " range " + fixed((*hi - *lo) / mean * 100.0, 1) + "%";
    };
    b.push_back("  probe " + std::to_string(cp.samples.size()) + " samples: current mean " + range(cur) +
                " | counted mean " + range(cnt) + " (range = (max-min)/mean)");
    blocks.push_back(std::move(b));
  }

  // ---- 診断: コアの種類ごと (クラスタ 1 つずつ)
  for (size_t ci = 0; ci < d.clusters.size(); ++ci) {
    const auto& c = d.clusters[ci];
    SummaryBlock b;
    if (ci == 0) {
      b.push_back("  cluster: kernel threads ms/unit @GHz(load) = Mcycles/unit | matmul: GF, flops/cycle/thread "
                  "(sieve: limit 40M, sieve1m: limit 1M, matmulN: n=N)");
    }
    b.push_back("  cpu" + cpu_ranges(c.cpus) + " max" +
                (c.max_khz > 0 ? std::to_string(c.max_khz / 1000) : std::string("?")) + " cap" +
                (c.capacity > 0 ? std::to_string(c.capacity) : std::string("?")) + ":");
    for (const auto& m : c.measures) {
      std::string l = "    " + m.kernel + " " + std::to_string(m.threads) + "T" + (m.pinned ? "" : "(unpinned)") + " " +
                      fixed(m.ms_per_unit, 2) + "ms @" + fixed(m.ghz_load, 2) + "G = " + fixed(m.mcycles_per_unit, 1) +
                      "Mc";
      if (m.gflops > 0) l += " | " + fixed(m.gflops, 2) + "GF " + fixed(m.flops_per_cycle, 2) + "f/c";
      l += " cpu" + cpu_ranges(m.cpus);
      if (!m.error.empty()) l += " error=" + m.error;
      b.push_back(l);
    }
    blocks.push_back(std::move(b));
  }

  // ---- 診断: 行列サイズの掃引
  {
    SummaryBlock b;
    b.push_back("  n    ws1T     wsMT     1T_GF@GHz(cpu)   MT_GF@GHz   MT/1T  worker_units");
    for (const auto& p : d.matmul_sweep) {
      auto kb = [](std::int64_t bytes) { return std::to_string(bytes / 1024) + "K"; };
      std::string units;
      for (const auto& w : p.mt_workers) units += (units.empty() ? "" : ",") + std::to_string(w.units);
      b.push_back("  " + std::to_string(p.n) + " " + kb(p.st_working_set_bytes) + " " + kb(p.mt_working_set_bytes) +
                  " " + fixed(p.st_gflops, 2) + "@" + fixed(p.st_ghz_load, 2) + "(cpu" + cpu_ranges(p.st_cpus) + ") " +
                  fixed(p.mt_gflops, 2) + "@" + fixed(p.mt_ghz_load, 2) + " " +
                  fixed(p.st_gflops > 0 ? p.mt_gflops / p.st_gflops : 0, 2) + "x" + (p.valid ? "" : " INVALID") +
                  " [" + units + "]");
    }
    blocks.push_back(std::move(b));
  }

  // ---- 診断: 各コアの最小 / 最大周波数 (サンプル全体)
  {
    const auto& f = d.freq;
    SummaryBlock b;
    if (!f.khz.empty()) {
      const size_t cpus = f.khz.front().size();
      std::vector<std::string> per_cpu;
      for (size_t c = 0; c < cpus; ++c) {
        std::int64_t lo = -1, hi = -1;
        for (const auto& row : f.khz) {
          if (c >= row.size() || row[c] < 0) continue;
          lo = lo < 0 ? row[c] : std::min(lo, row[c]);
          hi = std::max(hi, row[c]);
        }
        per_cpu.push_back(std::to_string(lo / 1000) + "/" + std::to_string(hi / 1000));
      }
      b.push_back("  freq min/max MHz (" + std::to_string(f.khz.size()) + " samples @" + std::to_string(f.interval_ms) +
                  "ms): " + groups(per_cpu, [](const std::string& v) { return v; }));
    } else {
      b.push_back("  freq samples: none (not readable on this device/OS)");
    }
    blocks.push_back(std::move(b));
  }
  return blocks;
}

}  // namespace detail

std::vector<std::string> make_summary_parts(const SuiteResult& r, std::size_t max_chars) {
  const std::string header = detail::summary_run_header(r);
  // 見出しと終端の行の最大の長さを先に取っておく (パートの数は 999 まで想定)
  const std::size_t reserve = header.size() + std::string(" | part 999/999\n").size() +
                              std::string("END part 999/999 chars=99999\n").size();
  const std::size_t capacity = max_chars > reserve + 16 ? max_chars - reserve : 16;

  std::vector<std::string> bodies(1);
  auto add_line = [&](std::string line) {
    if (line.size() + 1 > capacity) {  // 1 行だけで上限を超える場合 (通常は起きない) は切る
      line = line.substr(0, utf8_prefix_bytes(line, capacity > 7 ? capacity - 7 : 0)) + " [cut]";
    }
    if (!bodies.back().empty() && bodies.back().size() + line.size() + 1 > capacity) bodies.emplace_back();
    bodies.back() += line;
    bodies.back() += '\n';
  };
  for (const auto& block : detail::summary_blocks(r)) {
    std::size_t size = 0;
    for (const auto& l : block) size += l.size() + 1;
    // まとまりが今のパートに収まらなければ、次のパートへ送る (まとまり自体が上限を超えるときは行で分ける)
    if (!bodies.back().empty() && size <= capacity && bodies.back().size() + size > capacity) bodies.emplace_back();
    for (const auto& l : block) add_line(l);
  }

  std::vector<std::string> parts;
  const std::size_t n = bodies.size();
  for (std::size_t i = 0; i < n; ++i) {
    const std::string label = "part " + std::to_string(i + 1) + "/" + std::to_string(n);
    std::string text = header + " | " + label + "\n" + bodies[i];
    text += "END " + label + " chars=" + std::to_string(detail::utf8_char_count(text)) + "\n";
    parts.push_back(std::move(text));
  }
  return parts;
}

std::string make_summary_text(const SuiteResult& r) {
  std::string s;
  for (const auto& part : make_summary_parts(r)) s += part;
  return s;
}

}  // namespace mb
