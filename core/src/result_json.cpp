#include "json_writer.h"
#include "mobilebench/result.h"

namespace mb {

// JSON スキーマ (schema_version を変えたら UI 側のパーサも更新すること)
//
// {
//   "schema_version": 1,
//   "benchmark_version": "0.1.0",
//   "score_formula": "reference_ns / median_ns * 1000",
//   "overall_score": 1234.567,          // 各テストスコアの幾何平均
//   "valid": true, "cancelled": false,
//   "config": { "warmup_runs": 1, "measured_runs": 5, "logical_cores": 8 },
//   "build":  { "os", "arch", "compiler", "flags_profile", "flags", "optimized" },
//   "device": { ...プラットフォーム層から渡された情報... },
//   "tests": [ { "id", "name", "category", "threads", "reference_ns", "warmup_ns",
//                "runs_ns": [...], "median_ns", "score", "valid",
//                "checksum", "expected_checksum" } ]
// }
std::string to_json(const SuiteResult& r, bool pretty) {
  detail::JsonWriter w(pretty);
  w.begin_object();
  w.key("schema_version"); w.value(1);
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
    w.end_object();
  }
  w.end_array();

  w.end_object();
  return w.str();
}

}  // namespace mb
