// OS ごとに異なる処理はここに集約する。
// Phase 2 ではストレージテスト用に「OS キャッシュを回避してファイルを開く」関数
// (Android: O_DIRECT / iOS: fcntl(F_NOCACHE)) をここに追加する予定。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mobilebench/telemetry.h"

namespace mb::platform {

std::string os_name();      // "android" / "ios" / "ios-simulator" / "linux" / "macos"
std::string arch_name();    // "arm64" / "x86_64" / ...
std::string compiler_name();
int logical_cpu_count();    // 構成上の論理コア数 (オフラインのコアも含む)

// 計測用ワーカースレッドの開始時に呼ぶ。OS ごとの「前面アプリの通常処理」相当の
// 優先度に揃える。iOS は QoS で P/E コアの割り当てが変わるため USER_INITIATED を
// 明示する (計測を開始するアプリ側のスレッドも同じ QoS にしている)。
// Android / Linux は既定の優先度のまま (Kotlin の Dispatchers.Default と同等)。
void configure_benchmark_thread();

// 呼び出しスレッドが現在動いている CPU 番号。取得できない OS (iOS) では -1
int current_cpu();

// 各コアの現在周波数 [kHz] (OS 報告値)。Android / Linux の sysfs のみ。
// 読めないコアは -1。どのコアも読めない (iOS や SELinux で拒否) 場合は空。
std::vector<std::int64_t> read_cpu_cur_khz(int cpu_count);

// 各コアの現在の周波数上限 [kHz] (scaling_max_freq。発熱・省電力で下げられる)。形式は同上
std::vector<std::int64_t> read_cpu_cap_khz(int cpu_count);

// CPU 構成 (最大周波数・capacity・キャッシュ)。取れるものだけ埋める。
CpuTopology read_cpu_topology(int cpu_count);

}  // namespace mb::platform
