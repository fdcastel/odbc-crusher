# Reporting library
add_library(odbc_crusher_reporting
    console_reporter.cpp
    json_reporter.cpp
)

target_include_directories(odbc_crusher_reporting PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(odbc_crusher_reporting
    PUBLIC
        odbc_crusher_tests_lib
        odbc_crusher_discovery
        nlohmann_json::nlohmann_json
    PRIVATE
        odbc_crusher_version
        project_warnings
        project_options
)

target_compile_features(odbc_crusher_reporting PUBLIC cxx_std_17)
