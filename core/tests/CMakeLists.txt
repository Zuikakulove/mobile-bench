add_executable(mobilebench_tests
  test_main.cpp
  test_kernels.cpp
  test_runner.cpp
  test_stats_json.cpp
)
target_include_directories(mobilebench_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mobilebench_tests PRIVATE mobilebench_core)
mobilebench_apply_flags(mobilebench_tests)

add_test(NAME core_unit_tests COMMAND mobilebench_tests)

if(MB_BUILD_CLI)
  # 実際のテストを 1 つ実行し、チェックサムが一致して JSON が出ることを確認
  add_test(NAME cli_smoke COMMAND mobilebench_cli --only cpu.single.sieve --quiet --compact)
  set_tests_properties(cli_smoke PROPERTIES PASS_REGULAR_EXPRESSION "\"valid\":true")
endif()
