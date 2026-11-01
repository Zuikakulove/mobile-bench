// 結果の要約テキスト。JSON 全体は大きく (診断モードで数十 KB)、チャット等に貼ると
// 途中で切れるため、切り分けに必要な情報を 1 回 1 行に詰めた 4〜5KB のテキストを作る。
// 小数はロケール非依存の JsonWriter::format_fixed で整形する。
#include <algorithm>
#include <cmath>
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

std::string get(const std::map<std::string, std::string>& m, const char* key) {
  auto it = m.find(key);
  return it == m.end() ? "?" : it->second;
}

std::string run_line(size_t index, int warmup_count, const RunDetail& d) {
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
  line += " cur " + mhz_groups(d.cpu_khz_after) + " cap " + mhz_groups(d.cpu_cap_khz);
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

}  // namespace

std::string make_summary_text(const SuiteResult& r) {
  std::string s;
  auto line = [&s](const std::string& l) {
    s += l;
    s += '\n';
  };
  std::string compiler = r.build.compiler.substr(0, r.build.compiler.find(" ("));
  line("MobileBench v" + r.benchmark_version + " | app " + get(r.device, "app_version") + " build " +
       get(r.device, "app_build") + " (" + get(r.device, "git_sha") + ")");
  line("device: " + get(r.device, "manufacturer") + " " + get(r.device, "model") + " soc=" + get(r.device, "soc_model") +
       " " + get(r.device, "platform") + " " + get(r.device, "os_version") + " | " + r.build.arch + " " + compiler + " " +
       r.build.flags + (r.build.optimized ? "" : " NOT-OPTIMIZED"));
  line("overall " + fixed(r.overall_score, 1) + " valid=" + (r.valid ? "yes" : "no") +
       (r.cancelled ? " CANCELLED" : "") + " cores=" + std::to_string(r.logical_cores) +
       " diag=" + (r.diagnostics_enabled ? "yes" : "no"));
  line("topology max_mhz " + mhz_groups(r.topology.max_khz) + " capacity " +
       groups(r.topology.capacity, [](int v) { return std::to_string(v); }));
  line("columns: run time cpu ghz(before>after) cur(after,MHz) cap(MHz) | ST: unit ms | MT: units/worker busy");

  for (const auto& t : r.tests) {
    line("[" + t.id + "] median " + ms(t.median_ns) + "ms score " + fixed(t.score, 1) + " threads " +
         std::to_string(t.threads) + (t.valid ? "" : " INVALID") + (t.error.empty() ? "" : " error=" + t.error));
    if (!t.conditions_before.empty() || !t.conditions_after.empty()) {
      line("  cond " + conditions(t.conditions_before) + " -> " + conditions(t.conditions_after));
    }
    for (size_t i = 0; i < t.run_details.size(); ++i) line(run_line(i, r.warmup_runs, t.run_details[i]));
  }

  if (r.diagnostics_enabled) {
    const auto& d = r.diagnostics;
    line("[diagnostics] cond " + conditions(d.conditions_before) + " -> " + conditions(d.conditions_after));
    // ブースト: 250ms ごとの区間に分け、区間内の中央値 (外れ値に強い) を出す
    for (const auto& b : d.boost) {
      std::string segs;
      constexpr std::int64_t kBucketMs = 250;
      size_t i = 0;
      while (i < b.samples.size()) {
        const std::int64_t bucket = b.samples[i].t_ms / kBucketMs;
        std::vector<double> ghz, unit;
        std::vector<int> cpus;
        size_t j = i;
        for (; j < b.samples.size() && b.samples[j].t_ms / kBucketMs == bucket; ++j) {
          ghz.push_back(b.samples[j].ghz);
          unit.push_back(b.samples[j].unit_ms);
          cpus.push_back(b.samples[j].cpu);
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
      line("  boost idle " + std::to_string(b.idle_ms) + "ms (t_ms:GHz unit_ms cpu) " + (segs.empty() ? "-" : segs));
    }
    line("  cluster: kernel threads ms/unit @GHz(load) = Mcycles/unit | matmul128: GF, flops/cycle/thread");
    for (const auto& c : d.clusters) {
      std::string head = "  cpu" + cpu_ranges(c.cpus) + " max" +
                         (c.max_khz > 0 ? std::to_string(c.max_khz / 1000) : std::string("?")) + " cap" +
                         (c.capacity > 0 ? std::to_string(c.capacity) : std::string("?")) + ":";
      line(head);
      for (const auto& m : c.measures) {
        std::string l = "    " + m.kernel + " " + std::to_string(m.threads) + "T" + (m.pinned ? "" : "(unpinned)") +
                        " " + fixed(m.ms_per_unit, 2) + "ms @" + fixed(m.ghz_load, 2) + "G = " +
                        fixed(m.mcycles_per_unit, 1) + "Mc";
        if (m.gflops > 0) l += " | " + fixed(m.gflops, 2) + "GF " + fixed(m.flops_per_cycle, 2) + "f/c";
        l += " cpu" + cpu_ranges(m.cpus);
        if (!m.error.empty()) l += " error=" + m.error;
        line(l);
      }
    }
    line("  n    ws1T     wsMT     1T_GF@GHz(cpu)   MT_GF@GHz   MT/1T  worker_units");
    for (const auto& p : d.matmul_sweep) {
      auto kb = [](std::int64_t b) { return std::to_string(b / 1024) + "K"; };
      std::string units;
      for (const auto& w : p.mt_workers) units += (units.empty() ? "" : ",") + std::to_string(w.units);
      line("  " + std::to_string(p.n) + " " + kb(p.st_working_set_bytes) + " " + kb(p.mt_working_set_bytes) + " " +
           fixed(p.st_gflops, 2) + "@" + fixed(p.st_ghz_load, 2) + "(cpu" + cpu_ranges(p.st_cpus) + ") " +
           fixed(p.mt_gflops, 2) + "@" + fixed(p.mt_ghz_load, 2) + " " +
           fixed(p.st_gflops > 0 ? p.mt_gflops / p.st_gflops : 0, 2) + "x" + (p.valid ? "" : " INVALID") + " [" +
           units + "]");
    }
    const auto& f = d.freq;
    if (!f.khz.empty()) {
      // 各コアの最小 / 最大周波数 (サンプル全体)
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
      line("  freq min/max MHz (" + std::to_string(f.khz.size()) + " samples @" + std::to_string(f.interval_ms) +
           "ms): " + groups(per_cpu, [](const std::string& v) { return v; }));
    } else {
      line("  freq samples: none (not readable on this device/OS)");
    }
  }
  return s;
}

}  // namespace mb
