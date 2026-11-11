cmake_minimum_required(VERSION 3.22.1)
project(mobilebench_android LANGUAGES CXX)

# モノレポの core/ を Linux / iOS と同じソースのまま組み込む
set(MB_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core)
set(MB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MB_BUILD_CLI OFF CACHE BOOL "" FORCE)
add_subdirectory(${MB_CORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/core)

add_library(mobilebench_jni SHARED jni_bridge.cpp)
target_link_libraries(mobilebench_jni PRIVATE mobilebench_core log)
mobilebench_apply_flags(mobilebench_jni)
