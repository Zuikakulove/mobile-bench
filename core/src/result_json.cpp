#include "json_writer.h"
#include "mobilebench/result.h"

namespace mb {

namespace {

using detail::JsonWriter;

template <class T>
void write_int_array(JsonWriter& w, const std::vector<T>& values) {
  w.begin_array();
  for (auto v : values) w.value(static_cast<std::int64_t>(v));
  w.end_array();
}

void write_workers(JsonWriter& w, const std::vector<WorkerTelemetry>& workers) {
  w.begin_array();
  for (const auto& wk : workers) {
    w.begin_object();
    w.key("worker"); w.value(wk.worker);
    w.key("units"); w.value(wk.units);
    w.key("busy_ns"); w.value(wk.busy_ns);
    w.key("cpus"); write_int_array(w, wk.cpus);
    w.key("ghz_before"); w.value_fixed(wk.ghz_before, 3);
    w.key("ghz_after"); w.value_fixed(wk.ghz_after, 3);
    w.end_object();
  }
  w.end_array();
}

void write_run_detail(JsonWriter& w, const RunDetail& d) {
  w.begin_object();
  w.key("warmup"); w.value(d.warmup);
  w.key("ns"); w.value(d.ns);
  w.key("t_start_ms"); w.value(d.t_start_ms);
  if (!d.telemetry.unit_ns.empty()) {
    w.key("unit_ns"); write_int_array(w, d.telemetry.unit_ns);
    w.key("unit_cpus"); write_int_array(w, d.telemetry.unit_cpus);
  }
  w.key("workers"); write_workers(w, d.telemetry.workers);
  w.key("cpu_khz_before"); write_int_array(w, d.cpu_khz_before);
  w.key("cpu_khz_after"); write_int_array(w, d.cpu_khz_after);
  w.end_object();
}

void write_topology(JsonWriter& w, const CpuTopology& t) {
  w.begin_object();
  w.key("source"); w.value(t.source);
  w.key("max_khz"); write_int_array(w, t.max_khz);
  w.key("capacity"); write_int_array(w, t.capacity);
  w.key("caches");
  w.begin_array();
  for (const auto& c : t.caches) {
    w.begin_object();
    w.key("level"); w.value(c.level);
    w.key("type"); w.value(c.type);
    w.key("size_bytes"); w.value(c.size_bytes);
    w.key("shared_cpus"); w.value(c.shared_cpus);
    w.end_object();
  }
  w.end_array();
  w.key("perf_levels");
  w.begin_array();
  for (const auto& pl : t.perf_levels) {
    w.begin_object();
    w.key("name"); w.value(pl.name);
    w.key("logical_cpus"); w.value(pl.logical_cpus);
    w.key("l1d_bytes"); w.value(pl.l1d_bytes);
    w.key("l2_bytes"); w.value(pl.l2_bytes);
    w.key("cpus_per_l2"); w.value(pl.cpus_per_l2);
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

void write_diagnostics(JsonWriter& w, const Diagnostics& d) {
  w.begin_object();
  w.key("matmul_sweep");
  w.begin_array();
  for (const auto& p : d.matmul_sweep) {
    w.begin_object();
    w.key("n"); w.value(p.n);
    w.key("threads"); w.value(p.threads);
    w.key("st_units"); w.value(p.st_units);
    w.key("mt_units"); w.value(p.mt_units);
    w.key("st_working_set_bytes"); w.value(p.st_working_set_bytes);
    w.key("mt_working_set_bytes"); w.value(p.mt_working_set_bytes);
    w.key("st_runs_ns"); write_int_array(w, p.st_runs_ns);
    w.key("mt_runs_ns"); write_int_array(w, p.mt_runs_ns);
    w.key("st_median_ns"); w.value(p.st_median_ns);
    w.key("mt_median_ns"); w.value(p.mt_median_ns);
    w.key("st_gflops"); w.value_fixed(p.st_gflops, 3);
    w.key("mt_gflops"); w.value_fixed(p.mt_gflops, 3);
    w.key("valid"); w.value(p.valid);
    w.key("mt_workers"); write_workers(w, p.mt_workers);
    w.end_object();
  }
  w.end_array();
  w.key("freq_samples");
  w.begin_object();
  w.key("interval_ms"); w.value(d.freq.interval_ms);
  w.key("t_ms"); write_int_array(w, d.freq.t_ms);
  w.key("khz");
  w.begin_array();
  for (const auto& row : d.freq.khz) write_int_array(w, row);
  w.end_array();
  w.end_object();
  w.end_object();
}

}  // namespace

// JSON スキーマ (schema_version を変えたら UI 側のパーサも更新すること)
//
// {
//   "schema_version": 2,
//   "benchmark_version": "0.1.0",
//   "score_formula": "reference_ns / median_ns * 1000",
//   "overall_score": 1234.567,          // 各テストスコアの幾何平均
//   "valid": true, "cancelled": false,
//   "config": { "warmup_runs": 1, "measured_runs": 5, "logical_cores": 8 },
//   "build":  { "os", "arch", "compiler", "flags_profile", "flags", "optimized" },
//   "device": { ...プラットフォーム層から渡された情報... },
//   "tests": [ { "id", "name", "category", "threads", "reference_ns", "warmup_ns",
//                "runs_ns": [...], "median_ns", "score", "valid",
//                "checksum", "expected_checksum",
//                "run_details": [ { "warmup", "ns", "t_start_ms", "unit_ns", "unit_cpus",
//                                   "workers": [ { "worker", "units", "busy_ns", "cpus",
//                                                  "ghz_before", "ghz_after" } ],
//                                   "cpu_khz_before", "cpu_khz_after" } ] } ],
//   "cpu_topology": { "source", "max_khz", "capacity", "caches", "perf_levels" },
//   "diagnostics": { "matmul_sweep": [...], "freq_samples": {...} }   // 診断モード時のみ
// }
// schema 2 (benchmark 0.1.1): run_details / cpu_topology / diagnostics を追加
std::string to_json(const SuiteResult& r, bool pretty) {
  detail::JsonWriter w(pretty);
  w.begin_object();
  w.key("schema_version"); w.value(2);
  w.key("benchmark_version"); w.value(r.benchmark_version);
  w.key("score_formula"); w.value("reference_ns / median_ns * 1000");
  w.key("overall_score"); w.value_fixed(r.overall_score, 3);
  w.key("valid"); w.value(r.valid);
  w.key("cancelled"); w.value(r.cancelled);

  w.key("config");
  w.begin_object();
  w.key("warmup_runs"); w.value(r.warmup_runs);
  w.key("measured_runs"); w.value(r.measured_runs);
  w.key("logical_cores"); w.value(r.logical_cores);
  w.key("diagnostics"); w.value(r.diagnostics_enabled);
  w.end_object();

  w.key("build");
  w.begin_object();
  w.key("os"); w.value(r.build.os);
  w.key("arch"); w.value(r.build.arch);
  w.key("compiler"); w.value(r.build.compiler);
  w.key("flags_profile"); w.value(r.build.flags_profile);
  w.key("flags"); w.value(r.build.flags);
  w.key("optimized"); w.value(r.build.optimized);
  w.end_object();

  w.key("device");
  w.begin_object();
  for (const auto& [k, v] : r.device) {
    w.key(k);
    w.value(v);
  }
  w.end_object();

  w.key("cpu_topology");
  write_topology(w, r.topology);

  w.key("tests");
  w.begin_array();
  for (const auto& t : r.tests) {
    w.begin_object();
    w.key("id"); w.value(t.id);
    w.key("name"); w.value(t.name);
    w.key("category"); w.value(t.category);
    w.key("threads"); w.value(t.threads);
    w.key("reference_ns"); w.value(t.reference_ns);
    w.key("warmup_ns"); w.value(t.warmup_ns);
    w.key("runs_ns");
    w.begin_array();
    for (auto ns : t.runs_ns) w.value(ns);
    w.end_array();
    w.key("median_ns"); w.value(t.median_ns);
    w.key("score"); w.value_fixed(t.score, 3);
    w.key("valid"); w.value(t.valid);
    // 64bit 値は JavaScript/Swift の Double で精度落ちしないよう文字列で出す
    w.key("checksum"); w.value(std::to_string(t.checksum));
    w.key("expected_checksum"); w.value(std::to_string(t.expected_checksum));
    if (!t.error.empty()) {
      w.key("error");
      w.value(t.error);
    }
    w.key("run_details");
    w.begin_array();
    for (const auto& d : t.run_details) write_run_detail(w, d);
    w.end_array();
    w.end_object();
  }
  w.end_array();

  if (r.diagnostics_enabled) {
    w.key("diagnostics");
    write_diagnostics(w, r.diagnostics);
  }

  w.end_object();
  return w.str();
}

}  // namespace mb
