// OS ごとに異なる処理はここに集約する。
// Phase 2 ではストレージテスト用に「OS キャッシュを回避してファイルを開く」関数
// (Android: O_DIRECT / iOS: fcntl(F_NOCACHE)) をここに追加する予定。
#pragma once

#include <string>

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

}  // namespace mb::platform
