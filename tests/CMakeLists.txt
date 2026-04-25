# Test executable
add_executable(odbc_crusher_tests
    test_main.cpp
    test_odbc_environment.cpp
    test_odbc_connection.cpp
    test_odbc_error.cpp
    test_driver_info.cpp
    test_type_info.cpp
    test_function_info.cpp
    test_connection_tests.cpp
    test_statement_tests.cpp
    test_metadata_tests.cpp
    test_datatype_tests.cpp
    test_transaction_tests.cpp
    test_advanced_tests.cpp
    test_buffer_validation.cpp
    test_error_queue.cpp
    test_state_machine.cpp
    test_descriptor_tests.cpp
    test_cancellation_tests.cpp
    test_sqlstate_tests.cpp
    test_boundary_tests.cpp
    test_datatype_edge_tests.cpp
    test_unicode_tests.cpp
    test_catalog_depth_tests.cpp
    test_diagnostic_depth_tests.cpp
    test_cursor_behavior_tests.cpp
    test_param_binding_tests.cpp
    test_array_param_tests.cpp
    test_escape_sequence_tests.cpp
    test_numeric_struct_tests.cpp
    test_cursor_stress_tests.cpp
    test_crash_guard.cpp
)

target_include_directories(odbc_crusher_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(odbc_crusher_tests PRIVATE
    odbc_crusher_core
    odbc_crusher_discovery
    odbc_crusher_tests_lib
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

# Discover tests for CTest. Bump DISCOVERY_TIMEOUT past the 5s default —
# a fresh Windows CI runner needs >5s to cold-load ODBC32.dll + the
# driver search path before `--gtest_list_tests` can exit, and the build
# fails intermittently when discovery hits the timeout.
include(GoogleTest)
gtest_discover_tests(odbc_crusher_tests
    DISCOVERY_TIMEOUT 60
)
