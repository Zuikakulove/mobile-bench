// Linux / macOS 用のコマンドラインランナー。
//   mobilebench_cli [--only <id-prefix>]... [--threads N] [--diagnostics] [--compact] [--quiet]
// 結果の JSON を標準出力に、進捗を標準エラーに出す。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "mobilebench/runner.h"

int main(int argc, char** argv) {
  mb::RunConfig config;
  bool pretty = true;
  bool quiet = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
      config.only.emplace_back(argv[++i]);
    } else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
      config.threads = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--compact") == 0) {
      pretty = false;
    } else if (std::strcmp(argv[i], "--quiet") == 0) {
      quiet = true;
    } else if (std::strcmp(argv[i], "--diagnostics") == 0) {
      config.diagnostics = true;
    } else {
      std::fprintf(stderr, "usage: %s [--only <id-prefix>]... [--threads N] [--diagnostics] [--compact] [--quiet]\n", argv[0]);
      return 2;
    }
  }
  config.device["model"] = "cli";

  mb::Runner runner(config);
  const mb::SuiteResult result = runner.run([&](const mb::Progress& p) {
    if (quiet) return;
    std::fprintf(stderr, "[%3d%%] %s %s\n", static_cast<int>(p.fraction() * 100.0), p.test_id.c_str(),
                 p.warmup ? "(warmup)" : "");
  });
  std::cout << mb::to_json(result, pretty) << std::endl;
  return result.valid ? 0 : 1;
}
