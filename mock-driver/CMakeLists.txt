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

# Shared CMake modules — IMPROVEMENT_PLAN.md D16/E1 (warning flags) and E3
# (the GoogleTest pin). The mock is built as its own CMake project
# (`cmake -S mock-driver`, which is how CI, release.yml and the docs all
# build it), so it does not inherit the parent's module path — point at
# ../cmake explicitly.
#
# This is a hard requirement rather than a soft fallback on purpose: a
# fallback would mean a second literal GoogleTest pin in the tree, which is
# exactly the drift E3 exists to remove. This directory is not independently
# distributable — it is always built from a checkout of the whole repo.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CompilerWarnings.cmake")
    message(FATAL_ERROR
        "mock-driver requires the repository's shared cmake/ directory "
        "(expected at ${CMAKE_CURRENT_SOURCE_DIR}/../cmake). Build it from a "
        "full checkout: cmake -B mock-driver/build -S mock-driver")
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT TARGET project_warnings)
    include(CompilerWarnings)
endif()

# Platform-specific defines (Windows-only — odbc.h needs these to behave)
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
endif()

# Use CMake's FindODBC consistently across all three platforms — it
# resolves Homebrew unixODBC paths on macOS (/opt/homebrew/include),
# system unixODBC on Linux, and odbc32 on Windows. Mirrors the main
# project's src/core/CMakeLists.txt usage. The previous hand-rolled
# block missed the macOS Homebrew include path entirely, breaking the
# §6.2 cross-platform CI build.
find_package(ODBC REQUIRED)

# Core sources — shared between the driver DLL and unit tests
set(MOCK_DRIVER_CORE_SOURCES
    src/driver/driver_main.cpp
    src/driver/handles.cpp
    src/driver/environment.cpp
    src/driver/connection.cpp
    src/driver/statement.cpp
    src/driver/descriptor.cpp
    src/driver/diagnostics.cpp
    src/driver/config.cpp
    src/driver/entry_guard.cpp
    src/mock/mock_catalog.cpp
    src/mock/mock_types.cpp
    src/mock/mock_data.cpp
    src/mock/behaviors.cpp
    src/odbc/connection_api.cpp
    src/odbc/statement_api.cpp
    src/odbc/param_binding.cpp
    src/odbc/catalog_api.cpp
    src/odbc/descriptor_api.cpp
    src/odbc/info_api.cpp
    src/odbc/transaction_api.cpp
    src/odbc/diagnostic_api.cpp
    src/odbc/unicode_wrappers.cpp
    src/utils/string_utils.cpp
    src/utils/buffer_copy.cpp
    src/utils/c_types.cpp
)

# D19: the core sources were listed in both targets, so every build compiled
# all 26 of them twice. An object library compiles them once and both targets
# link the same objects. The include directories and warning flags are PUBLIC
# on the object library so the consumers inherit them rather than repeating
# them - which is what let the two lists drift in the first place.
add_library(mock_core OBJECT ${MOCK_DRIVER_CORE_SOURCES})

target_link_libraries(mock_core PUBLIC
    project_warnings
    project_options
)

target_include_directories(mock_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ODBC headers — sql.h, sqlext.h, sqltypes.h. ODBC::ODBC carries both
# the headers and the link library (odbc32 / unixodbc / iODBC). The
# driver itself doesn't link against the DM (it IS the driver), but
# the test exe below does need the link.
target_include_directories(mock_core PUBLIC
    $<TARGET_PROPERTY:ODBC::ODBC,INTERFACE_INCLUDE_DIRECTORIES>
)

# The shared library is built from the DLL's own position-independent copy of
# those objects. POSITION_INDEPENDENT_CODE has to be set on the object library
# itself: on Linux a SHARED target's objects need -fPIC, and objects compiled
# into an OBJECT library do not inherit it from the consumer.
set_target_properties(mock_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Driver shared library
add_library(mock-odbc-driver SHARED $<TARGET_OBJECTS:mock_core>)

target_link_libraries(mock-odbc-driver PRIVATE
    mock_core
)

# Platform-specific output name
if(WIN32)
    set_target_properties(mock-odbc-driver PROPERTIES
        OUTPUT_NAME "mockodbc"
        PREFIX ""
        SUFFIX ".dll"
    )
    # Use explicit .def file for symbol exports — never WINDOWS_EXPORT_ALL_SYMBOLS
    # (WAES re-exports odbc32.lib symbols causing infinite recursion with the DM)
    target_sources(mock-odbc-driver PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/mockodbc.def
    )
else()
    set_target_properties(mock-odbc-driver PROPERTIES
        OUTPUT_NAME "mockodbc"
        PREFIX "lib"
    )

    # D2: give the .so the same export surface as the Windows DLL. Without
    # this it exported every ODBC entry point the driver defines - including
    # the 38 ANSI ones the .def deliberately hides and the ODBC 2.x aliases -
    # so unixODBC could reach a path the Windows DM cannot, and the two
    # platforms were structurally different drivers built from one tree.
    #
    # The list is generated from src/mockodbc.def so there is one export set,
    # not two that can drift.
    include(GenerateVersionScript)
    set(MOCK_EXPORT_LIST "${CMAKE_CURRENT_BINARY_DIR}/mockodbc.exports")
    mock_generate_export_list(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mockodbc.def" "${MOCK_EXPORT_LIST}")

    # The version script alone, and deliberately *not*
    # -fvisibility=hidden. The two do not compose the way the row assumes:
    # hidden visibility is applied by the compiler, and a version script can
    # only restrict what the linker was given - it cannot re-export a symbol
    # the compiler already hid. Setting both exported nothing at all, so the
    # .so stopped being loadable as a driver. Measured: every Linux e2e test
    # skipped and the CI job still went green, which is what prompted D51.
    set_target_properties(mock-odbc-driver PROPERTIES
        LINK_DEPENDS "${MOCK_EXPORT_LIST}"
    )
    if(APPLE)
        target_link_options(mock-odbc-driver PRIVATE
            "-Wl,-exported_symbols_list,${MOCK_EXPORT_LIST}")
    else()
        target_link_options(mock-odbc-driver PRIVATE
            "-Wl,--version-script=${MOCK_EXPORT_LIST}")
    endif()
endif()

# Install targets
install(TARGETS mock-odbc-driver
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Testing
enable_testing()

# Add Google Test for unit testing. The version pin lives in the shared
# cmake/Dependencies.cmake (E3), so it is impossible for it to drift from
# the root project's — there is exactly one pin in the tree.
include(Dependencies)
odbc_crusher_fetch_googletest()

# Unit tests — compile core sources directly to access internal symbols
add_executable(mock-odbc-tests
    tests/test_main.cpp
    tests/test_handles.cpp
    tests/test_connection.cpp
    tests/test_entry_guard.cpp
    tests/test_fault_injection.cpp
    tests/test_config.cpp
    tests/test_gettypeinfo.cpp
    tests/test_error_injection.cpp
    tests/test_performance.cpp
    tests/test_silent_corruption.cpp
    tests/test_param_binding.cpp
    tests/test_dml.cpp
    tests/test_where_parser.cpp
    tests/test_function_call.cpp
    tests/test_query_semantics.cpp
    tests/test_diag_header.cpp
    tests/test_stmt_contract.cpp
    tests/test_catalog_semantics.cpp
    tests/test_transaction_state.cpp
    tests/test_descriptor_contract.cpp
    tests/test_getinfo_paths.cpp
    tests/test_array_corruption.cpp
    tests/test_fault_injection_reach.cpp
    tests/test_catalog_shapes.cpp
    tests/test_unicode_wrappers.cpp
    tests/test_cursor_and_data.cpp
    tests/test_block_fetch.cpp
    tests/test_literal_select.cpp
    tests/test_buffer_copy.cpp
    tests/test_c_types.cpp
    tests/test_handle_lifetime.cpp
    $<TARGET_OBJECTS:mock_core>
)

target_link_libraries(mock-odbc-tests PRIVATE
    mock_core
    GTest::gtest
    GTest::gtest_main
    ODBC::ODBC
)

include(GoogleTest)
gtest_discover_tests(mock-odbc-tests
    DISCOVERY_TIMEOUT 60
)
