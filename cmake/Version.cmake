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
    endif()
endif()

set(ODBC_CRUSHER_VERSION "${ODBC_CRUSHER_VERSION_MAJOR}.${ODBC_CRUSHER_VERSION_MINOR}.${ODBC_CRUSHER_VERSION_PATCH}")
message(STATUS "ODBC Crusher version: ${ODBC_CRUSHER_VERSION}")

configure_file(
    "${CMAKE_SOURCE_DIR}/include/odbc_crusher/version.hpp.in"
    "${CMAKE_SOURCE_DIR}/include/odbc_crusher/version.hpp"
    @ONLY
)
