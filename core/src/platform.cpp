#include "platform.h"

#include <thread>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <pthread.h>
#include <sys/qos.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__) || defined(__ANDROID__)
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

}  // namespace mb::platform
