#include <clocale>
#include <cmath>

#include "json_writer.h"
#include "mini_test.h"
#include "mobilebench/runner.h"
#include "mobilebench/version.h"

TEST(median_odd_and_even) {
  EXPECT_EQ(mb::median({5, 1, 3, 2, 4}), 3);
  EXPECT_EQ(mb::median({10, 40, 20, 30}), 25);
  EXPECT_EQ(mb::median({7}), 7);
  EXPECT_EQ(mb::median({}), 0);
}

TEST(median_ignores_outliers) {
  // 5 回中 1 回だけ極端に遅くても中央値は影響を受けない
  EXPECT_EQ(mb::median({100, 101, 99, 100, 100000}), 100);
}

TEST(score_formula) {
  EXPECT_NEAR(mb::compute_score(1000, 1000), 1000.0, 1e-9);
  EXPECT_NEAR(mb::compute_score(1000, 500), 2000.0, 1e-9);
  EXPECT_NEAR(mb::compute_score(1000, 2000), 500.0, 1e-9);
  EXPECT_EQ(mb::compute_score(1000, 0), 0.0);
}

TEST(geometric_mean) {
  EXPECT_NEAR(mb::geometric_mean({1000.0, 1000.0}), 1000.0, 1e-9);
  EXPECT_NEAR(mb::geometric_mean({500.0, 2000.0}), 1000.0, 1e-9);
  EXPECT_NEAR(mb::geometric_mean({2.0, 8.0, 4.0}), 4.0, 1e-9);
  EXPECT_EQ(mb::geometric_mean({}), 0.0);
  EXPECT_EQ(mb::geometric_mean({100.0, 0.0}), 0.0);
}

TEST(json_fixed_format_is_locale_independent) {
  // 小数点が "," になるロケールでも "." で出力されること
  std::setlocale(LC_ALL, "de_DE.UTF-8");
  using mb::detail::JsonWriter;
  EXPECT_EQ(JsonWriter::format_fixed(1234.5678, 3), std::string("1234.568"));
  EXPECT_EQ(JsonWriter::format_fixed(0.0005, 3), std::string("0.001"));
  EXPECT_EQ(JsonWriter::format_fixed(2.0, 3), std::string("2.000"));
  EXPECT_EQ(JsonWriter::format_fixed(-1.25, 1), std::string("-1.3"));
  EXPECT_EQ(JsonWriter::format_fixed(-0.0001, 2), std::string("0.00"));
  EXPECT_EQ(JsonWriter::format_fixed(NAN, 2), std::string("0"));
  std::setlocale(LC_ALL, "C");
}

TEST(json_escaping_and_structure) {
  mb::detail::JsonWriter w(false);
  w.begin_object();
  w.key("s"); w.value("a\"b\\c\n\x01");
  w.key("arr"); w.begin_array(); w.value(1); w.value(true); w.end_array();
  w.key("empty"); w.begin_object(); w.end_object();
  w.end_object();
  EXPECT_EQ(w.str(), std::string("{\"s\":\"a\\\"b\\\\c\\n\\u0001\",\"arr\":[1,true],\"empty\":{}}"));
}

TEST(suite_json_contains_required_fields) {
  mb::SuiteResult r;
  r.benchmark_version = mb::kBenchmarkVersion;
  mb::TestResult t;
  t.id = "x.y";
  t.name = "X";
  t.runs_ns = {1, 2, 3, 4, 5};
  t.median_ns = 3;
  t.score = 1500.25;
  r.tests.push_back(t);
  r.device["model"] = "Pixel \"Test\"";
  const std::string json = mb::to_json(r, false);
  for (const char* key : {"\"benchmark_version\":\"0.", "\"overall_score\":", "\"tests\":[", "\"id\":\"x.y\"",
                          "\"runs_ns\":[1,2,3,4,5]", "\"median_ns\":3", "\"score\":1500.250",
                          "\"model\":\"Pixel \\\"Test\\\"\""}) {
    EXPECT_TRUE(json.find(key) != std::string::npos);
  }
}
