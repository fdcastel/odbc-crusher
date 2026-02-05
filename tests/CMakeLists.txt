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

# Discover tests for CTest
include(GoogleTest)
gtest_discover_tests(odbc_crusher_tests)
