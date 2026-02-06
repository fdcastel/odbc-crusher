# ODBC Tests library
add_library(odbc_crusher_tests_lib
    test_base.cpp
    connection_tests.cpp
    statement_tests.cpp
    metadata_tests.cpp
    datatype_tests.cpp
    transaction_tests.cpp
    advanced_tests.cpp
    buffer_validation_tests.cpp
    error_queue_tests.cpp
    state_machine_tests.cpp
    descriptor_tests.cpp
    cancellation_tests.cpp
    sqlstate_tests.cpp
    boundary_tests.cpp
    datatype_edge_tests.cpp
)

target_include_directories(odbc_crusher_tests_lib
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(odbc_crusher_tests_lib
    PUBLIC
        odbc_crusher_core
        odbc_crusher_discovery
    PRIVATE
        project_warnings
        project_options
)

target_compile_features(odbc_crusher_tests_lib PUBLIC cxx_std_17)
