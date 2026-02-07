# Core ODBC library
add_library(odbc_crusher_core
    odbc_environment.cpp
    odbc_connection.cpp
    odbc_statement.cpp
    odbc_error.cpp
    crash_guard.cpp
    logger.cpp
)

target_include_directories(odbc_crusher_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(odbc_crusher_core
    PUBLIC
        ODBC::ODBC
    PRIVATE
        project_warnings
        project_options
)

target_compile_features(odbc_crusher_core PUBLIC cxx_std_17)
