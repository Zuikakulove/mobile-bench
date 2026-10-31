# core のコンパイルフラグ (フラグプロファイル 1) の唯一の定義場所 (CMake 側)。
# iOS は Xcode でビルドするため ios/project.yml に同じ設定を書いている。
# 片方を変えたらもう片方も揃え、include/mobilebench/build_profile.h と
# version.h を更新すること。

set(MOBILEBENCH_FLAGS_PROFILE 1)
set(MOBILEBENCH_COMPILE_OPTIONS -O2 -ffp-contract=on)

function(mobilebench_apply_flags target)
  target_compile_features(${target} PUBLIC cxx_std_17)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  target_compile_definitions(${target} PRIVATE MB_FLAGS_PROFILE=${MOBILEBENCH_FLAGS_PROFILE})
  # CMAKE_BUILD_TYPE (Android の Debug ビルド等) に関わらず必ず -O2 にする。
  # target_compile_options は CMAKE_CXX_FLAGS_<CONFIG> より後ろに付くため、
  # 最後に指定された -O2 が有効になる。
  target_compile_options(${target} PRIVATE ${MOBILEBENCH_COMPILE_OPTIONS})
endfunction()
