cmake_minimum_required(VERSION 3.20)
# The version used when there is no git tag to read - a source tarball, which
# is what every GitHub release attaches. It had been 0.1.0 since the first
# commit, so anyone building the published archive got a binary claiming a
# version two years stale. Keep it equal to the version being released; the
# release workflow refuses to publish if it drifts.
project(odbc-crusher VERSION 0.7.0 LANGUAGES CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json so IDE/clangd/clang-tidy pick up the right
# flags. Mirrors mock-driver/CMakeLists.txt — both projects benefit.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add cmake modules path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Include CMake modules
include(CompilerWarnings)
include(Dependencies)
include(Platform)
include(Version)

# Find ODBC
find_package(ODBC REQUIRED)

# Enable testing
enable_testing()

# Fetch Google Test — pin lives in cmake/Dependencies.cmake (E3)
odbc_crusher_fetch_googletest()

# Fetch CLI11 for command-line parsing
FetchContent_Declare(
    cli11
    GIT_REPOSITORY ${ODBC_CRUSHER_CLI11_REPOSITORY}
    GIT_TAG ${ODBC_CRUSHER_CLI11_TAG}
)
FetchContent_MakeAvailable(cli11)

# Fetch nlohmann/json
FetchContent_Declare(
    json
    GIT_REPOSITORY ${ODBC_CRUSHER_JSON_REPOSITORY}
    GIT_TAG ${ODBC_CRUSHER_JSON_TAG}
)
FetchContent_MakeAvailable(json)

# E5: fetched dependencies get SYSTEM include directories, so our warning set
# stops being an opinion about their code.
#
# -Werror turned two -Wnull-dereference hits inside
# `_deps/json-src/include/nlohmann/json.hpp` into build failures. The only
# ways out are patching a vendored header on every update, or dropping a flag
# that protects our own code - neither of which is a trade worth making for a
# warning about somebody else's implementation. SYSTEM is the mechanism that
# exists for this.
#
# Applied to every fetched target rather than to the one that tripped first:
# the next dependency to warn should not need this discovered again.
# `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` rather than FetchContent's SYSTEM
# option, which needs CMake 3.25 and this project requires 3.20.
foreach(_dep_target nlohmann_json CLI11 gtest gtest_main gmock gmock_main)
    if(TARGET ${_dep_target})
        get_target_property(_dep_includes ${_dep_target}
                            INTERFACE_INCLUDE_DIRECTORIES)
        if(_dep_includes)
            set_target_properties(${_dep_target} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_dep_includes}")
        endif()
    endif()
endforeach()

# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)
