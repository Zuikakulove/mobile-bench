// 外部依存なしの最小テストフレームワーク (GoogleTest の取得を不要にするため)。
#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace minitest {

struct Case {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

inline int& failures() {
  static int count = 0;
  return count;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

template <class A, class B>
void expect_eq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
  if (!(a == b)) {
    std::ostringstream os;
    os << file << ":" << line << ": EXPECT_EQ(" << ea << ", " << eb << ") failed: " << a << " vs " << b;
    std::fprintf(stderr, "    %s\n", os.str().c_str());
    ++failures();
  }
}

inline void expect_true(bool v, const char* expr, const char* file, int line) {
  if (!v) {
    std::fprintf(stderr, "    %s:%d: EXPECT_TRUE(%s) failed\n", file, line, expr);
    ++failures();
  }
}

}  // namespace minitest

#define MT_CONCAT_(a, b) a##b
#define MT_CONCAT(a, b) MT_CONCAT_(a, b)
#define TEST(name)                                                              \
  static void MT_CONCAT(test_fn_, name)();                                      \
  static ::minitest::Registrar MT_CONCAT(test_reg_, name)(#name, MT_CONCAT(test_fn_, name)); \
  static void MT_CONCAT(test_fn_, name)()

#define EXPECT_EQ(a, b) ::minitest::expect_eq((a), (b), #a, #b, __FILE__, __LINE__)
#define EXPECT_TRUE(v) ::minitest::expect_true(static_cast<bool>(v), #v, __FILE__, __LINE__)
#define EXPECT_NEAR(a, b, eps) EXPECT_TRUE(((a) - (b)) <= (eps) && ((b) - (a)) <= (eps))
