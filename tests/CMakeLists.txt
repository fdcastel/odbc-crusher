# tests/  — split into two layers (IMPROVEMENT_PLAN.md §5.3):
#   unit/  — real GTest unit tests of project plumbing (RAII handles, error
#            parsing, crash guard, discovery).
#   e2e/   — subprocess harness that spawns the odbc-crusher binary against
#            the mock driver and asserts on the JSON report (§5.1).
#
# The 23 thin tests::*Tests(conn).run() wrappers that used to live alongside
# these have been retired in favour of the e2e harness.

# ── Unit tests ────────────────────────────────────────────────────────────
add_executable(odbc_crusher_tests
    unit/test_main.cpp
    unit/test_odbc_environment.cpp
    unit/test_odbc_connection.cpp
    unit/test_odbc_error.cpp
    unit/test_driver_info.cpp
    unit/test_type_info.cpp
    unit/test_function_info.cpp
    unit/test_crash_guard.cpp
    unit/test_category_teardown.cpp
    unit/test_test_base.cpp
    unit/test_row_verification.cpp
    unit/test_stale_table_reuse.cpp
    unit/test_sibling_catalog.cpp
    unit/test_console_reporter.cpp
    unit/test_json_reporter.cpp
    unit/test_utf8_sanitize.cpp
    unit/test_guarded_buffer.cpp
    unit/test_sqlwchar_utils.cpp
    unit/test_cursor_leak_heuristic.cpp
    unit/test_crash_survivors.cpp
)

target_include_directories(odbc_crusher_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(odbc_crusher_tests PRIVATE
    odbc_crusher_core
    odbc_crusher_discovery
    odbc_crusher_tests_lib
    odbc_crusher_reporting
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    nlohmann_json::nlohmann_json
    project_warnings
    project_options
)

# Discover tests for CTest. Bump DISCOVERY_TIMEOUT past the 5s default —
# a fresh Windows CI runner needs >5s to cold-load ODBC32.dll + the
# driver search path before `--gtest_list_tests` can exit, and the build
# fails intermittently when discovery hits the timeout.
#
# LABELS unit lets the CI workflow run "tests that don't need a registered
# ODBC driver" via `ctest -L unit` instead of a brittle name regex.
include(GoogleTest)
gtest_discover_tests(odbc_crusher_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

# ── E2E harness — IMPROVEMENT_PLAN.md §5.1 ────────────────────────────────
# Spawns the odbc-crusher binary against the mock driver and asserts on the
# JSON report. Compile-time path injection avoids hand-coding build paths
# in the harness; tests skip themselves when the mock driver is unloadable.
add_executable(odbc_crusher_e2e_tests
    e2e/test_e2e_main.cpp
    e2e/test_e2e_scenarios.cpp
    e2e/e2e_harness.cpp
)

target_link_libraries(odbc_crusher_e2e_tests PRIVATE
    GTest::gtest
    nlohmann_json::nlohmann_json
    project_warnings
    project_options
)

target_compile_definitions(odbc_crusher_e2e_tests PRIVATE
    CRUSHER_BIN_PATH="$<TARGET_FILE:odbc-crusher>"
)

# Ensure the e2e exe is rebuilt whenever odbc-crusher itself changes.
add_dependencies(odbc_crusher_e2e_tests odbc-crusher)

gtest_discover_tests(odbc_crusher_e2e_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "e2e"
)
