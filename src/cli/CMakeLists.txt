# CLI library (placeholder for future CLI utilities)
add_library(odbc_crusher_cli INTERFACE)

target_include_directories(odbc_crusher_cli
    INTERFACE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(odbc_crusher_cli
    INTERFACE
        CLI11::CLI11
)
