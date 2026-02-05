# Mock ODBC Driver - CMake Build System
cmake_minimum_required(VERSION 3.20)

project(mock-odbc-driver
    VERSION 1.0.0
    DESCRIPTION "A configurable Mock ODBC Driver for testing"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Platform-specific settings
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
    set(ODBC_LIBRARIES odbc32)
elseif(APPLE)
    find_library(ODBC_FRAMEWORK ODBC)
    set(ODBC_LIBRARIES ${ODBC_FRAMEWORK})
else()
    # Linux
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(ODBC QUIET odbc)
    endif()
    if(NOT ODBC_FOUND)
        set(ODBC_LIBRARIES odbc)
    endif()
endif()

# Driver shared library
add_library(mock-odbc-driver SHARED
    src/driver/driver_main.cpp
    src/driver/handles.cpp
    src/driver/environment.cpp
    src/driver/connection.cpp
    src/driver/statement.cpp
    src/driver/descriptor.cpp
    src/driver/diagnostics.cpp
    src/driver/config.cpp
    src/mock/mock_catalog.cpp
    src/mock/mock_types.cpp
    src/mock/mock_data.cpp
    src/mock/behaviors.cpp
    src/odbc/connection_api.cpp
    src/odbc/statement_api.cpp
    src/odbc/catalog_api.cpp
    src/odbc/descriptor_api.cpp
    src/odbc/info_api.cpp
    src/odbc/transaction_api.cpp
    src/odbc/diagnostic_api.cpp
    src/utils/string_utils.cpp
)

target_include_directories(mock-odbc-driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Platform-specific output name
if(WIN32)
    set_target_properties(mock-odbc-driver PROPERTIES
        OUTPUT_NAME "mockodbc"
        PREFIX ""
        SUFFIX ".dll"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
else()
    set_target_properties(mock-odbc-driver PROPERTIES
        OUTPUT_NAME "mockodbc"
        PREFIX "lib"
    )
endif()

# Install targets
install(TARGETS mock-odbc-driver
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Testing
enable_testing()

# Add Google Test for unit testing
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Unit tests
add_executable(mock-odbc-tests
    tests/test_main.cpp
    tests/test_handles.cpp
    tests/test_config.cpp
    tests/test_gettypeinfo.cpp
)

target_link_libraries(mock-odbc-tests PRIVATE
    mock-odbc-driver
    GTest::gtest
    GTest::gtest_main
    ${ODBC_LIBRARIES}
)

target_include_directories(mock-odbc-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

include(GoogleTest)
gtest_discover_tests(mock-odbc-tests)

# Simple manual test executable
add_executable(test_simple test_simple.cpp)
target_link_libraries(test_simple PRIVATE ${ODBC_LIBRARIES})
