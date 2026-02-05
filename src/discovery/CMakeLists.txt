# Discovery library
add_library(odbc_crusher_discovery
    driver_info.cpp
    type_info.cpp
    function_info.cpp
)

target_include_directories(odbc_crusher_discovery
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(odbc_crusher_discovery
    PUBLIC
        odbc_crusher_core
    PRIVATE
        project_warnings
        project_options
)

target_compile_features(odbc_crusher_discovery PUBLIC cxx_std_17)
