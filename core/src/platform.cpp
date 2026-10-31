#include "platform.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <pthread.h>
#include <sys/qos.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__) || defined(__ANDROID__)
#include <sched.h>
#include <unistd.h>
#endif

namespace mb::platform {

std::string os_name() {
#if defined(__ANDROID__)
  return "android";
#elif defined(__APPLE__)
#if TARGET_OS_SIMULATOR
  return "ios-simulator";
#elif TARGET_OS_IOS
  return "ios";
#else
  return "macos";
#endif
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

std::string arch_name() {
#if defined(__aarch64__) || defined(__arm64__)
  return "arm64";
#elif defined(__arm__)
  return "arm";
#elif defined(__x86_64__)
  return "x86_64";
#elif defined(__i386__)
  return "x86";
#else
  return "unknown";
#endif
}

std::string compiler_name() {
#if defined(__clang__)
  return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
  return std::string("gcc ") + __VERSION__;
#else
  return "unknown";
#endif
}

int logical_cpu_count() {
  int n = 0;
#if defined(__APPLE__)
  int value = 0;
  size_t size = sizeof(value);
  if (sysctlbyname("hw.logicalcpu_max", &value, &size, nullptr, 0) == 0) n = value;
#elif defined(__linux__) || defined(__ANDROID__)
  // Android は省電力のためコアをオフラインにすることがあるため、
  // ONLN ではなく CONF (構成上のコア数) を使い、実行ごとのぶれを防ぐ。
  long conf = sysconf(_SC_NPROCESSORS_CONF);
  if (conf > 0) n = static_cast<int>(conf);
#endif
  if (n <= 0) n = static_cast<int>(std::thread::hardware_concurrency());
  return n > 0 ? n : 1;
}

void configure_benchmark_thread() {
#if defined(__APPLE__)
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif
}

int current_cpu() {
#if defined(__linux__) || defined(__ANDROID__)
  return sched_getcpu();
#else
  return -1;
#endif
}

namespace {

#if defined(__linux__) || defined(__ANDROID__)
// sysfs の小さなテキストファイルを読む。失敗したら空文字列
std::string read_small_file(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "re");
  if (f == nullptr) return {};
  char buf[256];
  const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
  std::fclose(f);
  buf[n] = '\0';
  std::string s(buf);
  while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
  return s;
}

std::int64_t read_int_file(const std::string& path) {
  const std::string s = read_small_file(path);
  if (s.empty()) return -1;
  char* end = nullptr;
  const long long v = std::strtoll(s.c_str(), &end, 10);
  return end == s.c_str() ? -1 : static_cast<std::int64_t>(v);
}

std::string cpu_dir(int cpu) { return "/sys/devices/system/cpu/cpu" + std::to_string(cpu); }

// "96K" / "12288K" / "8M" -> bytes
std::int64_t parse_cache_size(const std::string& s) {
  if (s.empty()) return -1;
  char* end = nullptr;
  const long long v = std::strtoll(s.c_str(), &end, 10);
  if (end == s.c_str()) return -1;
  std::int64_t mul = 1;
  if (*end == 'K' || *end == 'k') mul = 1024;
  if (*end == 'M' || *end == 'm') mul = 1024 * 1024;
  return static_cast<std::int64_t>(v) * mul;
}
#endif

#if defined(__APPLE__)
std::int64_t sysctl_int(const char* name) {
  std::int64_t v64 = 0;
  size_t size = sizeof(v64);
  if (sysctlbyname(name, &v64, &size, nullptr, 0) != 0) return -1;
  if (size == sizeof(std::int32_t)) {
    std::int32_t v32 = 0;
    std::memcpy(&v32, &v64, sizeof(v32));
    return v32;
  }
  return v64;
}

std::string sysctl_string(const char* name) {
  char buf[128] = {0};
  size_t size = sizeof(buf) - 1;
  if (sysctlbyname(name, buf, &size, nullptr, 0) != 0) return {};
  return std::string(buf);
}
#endif

}  // namespace

namespace {

std::vector<std::int64_t> read_cpufreq_files(int cpu_count, const char* name) {
  std::vector<std::int64_t> khz;
#if defined(__linux__) || defined(__ANDROID__)
  bool any = false;
  khz.reserve(static_cast<size_t>(cpu_count));
  for (int cpu = 0; cpu < cpu_count; ++cpu) {
    const std::int64_t v = read_int_file(cpu_dir(cpu) + "/cpufreq/" + name);
    any = any || v > 0;
    khz.push_back(v);
  }
  if (!any) khz.clear();
#else
  (void)cpu_count;
  (void)name;
#endif
  return khz;
}

}  // namespace

std::vector<std::int64_t> read_cpu_cur_khz(int cpu_count) { return read_cpufreq_files(cpu_count, "scaling_cur_freq"); }

std::vector<std::int64_t> read_cpu_cap_khz(int cpu_count) { return read_cpufreq_files(cpu_count, "scaling_max_freq"); }

CpuTopology read_cpu_topology(int cpu_count) {
  CpuTopology t;
  t.source = "none";
#if defined(__linux__) || defined(__ANDROID__)
  bool any = false;
  for (int cpu = 0; cpu < cpu_count; ++cpu) {
    const std::int64_t max_khz = read_int_file(cpu_dir(cpu) + "/cpufreq/cpuinfo_max_freq");
    const std::int64_t cap = read_int_file(cpu_dir(cpu) + "/cpu_capacity");
    t.max_khz.push_back(max_khz);
    t.capacity.push_back(static_cast<int>(cap));
    any = any || max_khz > 0 || cap > 0;
    for (int idx = 0; idx < 8; ++idx) {
      const std::string base = cpu_dir(cpu) + "/cache/index" + std::to_string(idx);
      const std::int64_t level = read_int_file(base + "/level");
      if (level < 0) break;
      CacheInfo c;
      c.level = static_cast<int>(level);
      c.type = read_small_file(base + "/type");
      c.size_bytes = parse_cache_size(read_small_file(base + "/size"));
      c.shared_cpus = read_small_file(base + "/shared_cpu_list");
      const bool dup = std::any_of(t.caches.begin(), t.caches.end(), [&](const CacheInfo& o) {
        return o.level == c.level && o.type == c.type && o.shared_cpus == c.shared_cpus;
      });
      if (!dup) t.caches.push_back(c);
      any = true;
    }
  }
  if (any) t.source = "sysfs";
#elif defined(__APPLE__)
  (void)cpu_count;
  const std::int64_t levels = sysctl_int("hw.nperflevels");
  for (std::int64_t i = 0; i < levels && i < 4; ++i) {
    const std::string prefix = "hw.perflevel" + std::to_string(i) + ".";
    PerfLevel pl;
    pl.name = sysctl_string((prefix + "name").c_str());
    pl.logical_cpus = static_cast<int>(sysctl_int((prefix + "logicalcpu").c_str()));
    pl.l1d_bytes = sysctl_int((prefix + "l1dcachesize").c_str());
    pl.l2_bytes = sysctl_int((prefix + "l2cachesize").c_str());
    pl.cpus_per_l2 = static_cast<int>(sysctl_int((prefix + "cpusperl2").c_str()));
    t.perf_levels.push_back(pl);
  }
  if (!t.perf_levels.empty()) t.source = "sysctl";
#else
  (void)cpu_count;
#endif
  return t;
}

}  // namespace mb::platform
