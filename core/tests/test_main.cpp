#include <cstring>

#include "mini_test.h"

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int ran = 0;
  for (const auto& c : minitest::registry()) {
    if (filter && std::strstr(c.name, filter) == nullptr) continue;
    const int before = minitest::failures();
    std::fprintf(stderr, "[ RUN  ] %s\n", c.name);
    c.fn();
    std::fprintf(stderr, "[ %s ] %s\n", minitest::failures() == before ? " OK " : "FAIL", c.name);
    ++ran;
  }
  std::fprintf(stderr, "%d tests, %d failures\n", ran, minitest::failures());
  return minitest::failures() == 0 && ran > 0 ? 0 : 1;
}
