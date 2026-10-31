// コンパイルフラグプロファイル。
//
// Android (CMake) と iOS (Xcode) で core を同じ条件でコンパイルしたことを
// 保証するため、ビルドシステム側で MB_FLAGS_PROFILE を定義させる。
//
// プロファイル 1: -O2 -ffp-contract=on  (C++17)
//   - Linux / Android : core/cmake/MobileBenchFlags.cmake
//   - iOS             : ios/project.yml (GCC_OPTIMIZATION_LEVEL=2, OTHER_CFLAGS)
//
// どちらかを変更したら両方を揃え、version.h を上げること。
#pragma once

#ifndef MB_FLAGS_PROFILE
#error "MB_FLAGS_PROFILE is not defined. Build core with the shared flag profile (see build_profile.h)."
#endif

#if MB_FLAGS_PROFILE != 1
#error "Unknown MB_FLAGS_PROFILE. Update build_profile.h."
#endif

namespace mb {

inline constexpr int kFlagsProfile = MB_FLAGS_PROFILE;
inline constexpr const char* kFlagsProfileDescription = "-O2 -ffp-contract=on";

}  // namespace mb
