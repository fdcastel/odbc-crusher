# Version detection from git tags
#
# If building from a git tag like "v1.2.3", uses that version.
# Otherwise falls back to the CMake project() version.
# Generates version.hpp from version.hpp.in at configure time.

find_package(Git QUIET)

set(ODBC_CRUSHER_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(ODBC_CRUSHER_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(ODBC_CRUSHER_VERSION_PATCH ${PROJECT_VERSION_PATCH})

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --match "v*" --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_TAG_RESULT
    )
    
    if(GIT_TAG_RESULT EQUAL 0 AND GIT_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
        set(ODBC_CRUSHER_VERSION_MAJOR ${CMAKE_MATCH_1})
        set(ODBC_CRUSHER_VERSION_MINOR ${CMAKE_MATCH_2})
        set(ODBC_CRUSHER_VERSION_PATCH ${CMAKE_MATCH_3})
        message(STATUS "Version from git tag: ${GIT_TAG}")
    elseif(NOT GIT_TAG_RESULT EQUAL 0)
        message(WARNING "git describe --tags --match v* failed (result ${GIT_TAG_RESULT}); "
                        "falling back to project() version ${PROJECT_VERSION}. "
                        "This is expected on a shallow clone or when no v* tag exists.")
    elseif(NOT GIT_TAG STREQUAL "")
        message(WARNING "Git tag '${GIT_TAG}' does not match vMAJOR.MINOR.PATCH; "
                        "falling back to project() version ${PROJECT_VERSION}.")
    endif()
else()
    # No git, or no .git directory - a source tarball, which is what a GitHub
    # release attaches. This branch used to be silent, so a build from the
    # published archive reported the project() fallback as though it had been
    # read from a tag. Both warnings above cover cases inside the repository;
    # this is the one that reaches people who never cloned it.
    message(WARNING "No git tag available (no .git directory or git not "
                    "found), so the version is the project() fallback "
                    "${PROJECT_VERSION} rather than a tag. This is expected "
                    "for a source-tarball build; the binary will report "
                    "${PROJECT_VERSION}.")
endif()

set(ODBC_CRUSHER_VERSION "${ODBC_CRUSHER_VERSION_MAJOR}.${ODBC_CRUSHER_VERSION_MINOR}.${ODBC_CRUSHER_VERSION_PATCH}")
message(STATUS "ODBC Crusher version: ${ODBC_CRUSHER_VERSION}")

# E2: generate into the *build* tree, never the source tree. Writing it back
# into include/ meant a read-only checkout could not configure, two build
# directories on different tags fought over one file, and a stale header
# survived until the next configure happened to rewrite it.
set(ODBC_CRUSHER_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")

configure_file(
    "${CMAKE_SOURCE_DIR}/include/odbc_crusher/version.hpp.in"
    "${ODBC_CRUSHER_GENERATED_INCLUDE_DIR}/odbc_crusher/version.hpp"
    @ONLY
)

# Carry the generated include directory as a target rather than a bare
# include_directories() call, so only what needs version.hpp gets it.
add_library(odbc_crusher_version INTERFACE)
target_include_directories(odbc_crusher_version
    INTERFACE ${ODBC_CRUSHER_GENERATED_INCLUDE_DIR}
)
