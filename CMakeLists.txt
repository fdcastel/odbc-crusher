cmake_minimum_required(VERSION 3.20)
project(odbc-crusher VERSION 0.1.0 LANGUAGES CXX)

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

# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)
