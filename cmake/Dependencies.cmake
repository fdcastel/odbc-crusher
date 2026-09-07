# External dependency pins — the single source of truth.
#
# IMPROVEMENT_PLAN.md E3: the root project and the standalone mock-driver
# project each used to carry their own FetchContent_Declare(googletest ...)
# with its own GIT_TAG, so a version bump had to be made in two places and
# nothing detected it when only one of them moved. The pins live here now and
# both projects read them.
#
# Note this deliberately does NOT merge the two _deps trees: the mock driver is
# a separate CMake project on purpose (CI builds it standalone on all three
# platforms, the register scripts and the docs all use `cmake -S mock-driver`),
# so it keeps its own build directory and its own dependency cache. What is
# shared is the version, which is the part that could silently drift.

set(ODBC_CRUSHER_GOOGLETEST_REPOSITORY "https://github.com/google/googletest.git")
set(ODBC_CRUSHER_GOOGLETEST_TAG        "v1.17.0")

set(ODBC_CRUSHER_CLI11_REPOSITORY      "https://github.com/CLIUtils/CLI11.git")
set(ODBC_CRUSHER_CLI11_TAG             "v2.6.2")

set(ODBC_CRUSHER_JSON_REPOSITORY       "https://github.com/nlohmann/json.git")
set(ODBC_CRUSHER_JSON_TAG              "v3.12.0")

# Fetch GoogleTest at the shared pin. A macro rather than a function so the
# targets FetchContent_MakeAvailable defines land in the caller's scope.
macro(odbc_crusher_fetch_googletest)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY ${ODBC_CRUSHER_GOOGLETEST_REPOSITORY}
        GIT_TAG ${ODBC_CRUSHER_GOOGLETEST_TAG}
    )
    # For Windows: prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endmacro()
