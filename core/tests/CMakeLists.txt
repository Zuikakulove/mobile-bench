add_executable(mobilebench_tests
  test_main.cpp
  test_kernels.cpp
  test_runner.cpp
  test_stats_json.cpp
  test_summary.cpp
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

if(MB_BUILD_CLI)
  # 診断モード (クロック推定の比較、クラスタ別の単位の案、行列サイズ掃引) が動き、
  # 結果と分割した要約が JSON に出ることを確認 (JSON の出力順に並べた 1 つの正規表現)
  add_test(NAME cli_diagnostics COMMAND mobilebench_cli --only cpu.single.sieve --diagnostics --quiet --compact)
  set_tests_properties(cli_diagnostics PROPERTIES
    PASS_REGULAR_EXPRESSION "\"clock_probe\":\\{\"workers\":[0-9]+.*\"kernel\":\"sieve1m\".*\"kernel\":\"matmul576\".*\"matmul_sweep\":\\[\\{\"n\":64.*\"n\":768.*\"summary_parts\":\\[\"MobileBench v"
    TIMEOUT 300)
endif()
