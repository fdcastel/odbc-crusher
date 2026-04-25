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
include(Platform)
include(Version)

# Find ODBC
find_package(ODBC REQUIRED)

# Enable testing
enable_testing()

# Fetch Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Fetch CLI11 for command-line parsing
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.6.2
)
FetchContent_MakeAvailable(cli11)

# Fetch nlohmann/json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(json)

# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)
