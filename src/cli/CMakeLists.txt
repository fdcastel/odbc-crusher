# CLI library
add_library(odbc_crusher_cli
    cli_parser.cpp
    config.cpp
)

target_include_directories(odbc_crusher_cli
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(odbc_crusher_cli
    PRIVATE
        CLI11::CLI11
        project_warnings
        project_options
)

target_compile_features(odbc_crusher_cli PUBLIC cxx_std_17)
