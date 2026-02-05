# Reporting library
add_library(odbc_crusher_reporting
    console_reporter.cpp
    json_reporter.cpp
)

target_include_directories(odbc_crusher_reporting PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(odbc_crusher_reporting PUBLIC
    odbc_crusher_tests_lib
    odbc_crusher_discovery
    nlohmann_json::nlohmann_json
)
