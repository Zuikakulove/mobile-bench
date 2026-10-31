cmake_minimum_required(VERSION 3.18)
project(mobilebench_core LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(MB_TOP_LEVEL ON)
else()
  set(MB_TOP_LEVEL OFF)
endif()

option(MB_BUILD_TESTS "Build core unit tests" ${MB_TOP_LEVEL})
option(MB_BUILD_CLI "Build command line runner" ${MB_TOP_LEVEL})

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MobileBenchFlags.cmake)

# このディレクトリのスコープでは、ビルドタイプ由来の -O フラグを取り除く
# (add_subdirectory 経由で使われても親プロジェクトには影響しない)
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  string(REGEX REPLACE "-O[0-3sz]" "" CMAKE_CXX_FLAGS_${cfg} "${CMAKE_CXX_FLAGS_${cfg}}")
endforeach()

find_package(Threads REQUIRED)

add_library(mobilebench_core STATIC
  src/clock_probe.cpp
  src/diagnostics.cpp
  src/freq_sampler.cpp
  src/platform.cpp
  src/registry.cpp
  src/result_json.cpp
  src/runner.cpp
  src/stats.cpp
  src/worker_pool.cpp
  src/tests/cpu_matmul.cpp
  src/tests/cpu_sieve.cpp
)
target_include_directories(mobilebench_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mobilebench_core PUBLIC Threads::Threads)
set_target_properties(mobilebench_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
mobilebench_apply_flags(mobilebench_core)
if(MB_TOP_LEVEL AND NOT MSVC)
  target_compile_options(mobilebench_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MB_BUILD_CLI)
  add_executable(mobilebench_cli tools/cli_main.cpp)
  target_link_libraries(mobilebench_cli PRIVATE mobilebench_core)
  mobilebench_apply_flags(mobilebench_cli)
endif()

if(MB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
