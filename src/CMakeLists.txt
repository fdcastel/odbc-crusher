# Add subdirectories for each component
add_subdirectory(core)
add_subdirectory(cli)

# Main executable
add_executable(odbc-crusher main.cpp)

target_link_libraries(odbc-crusher PRIVATE
    odbc_crusher_core
    odbc_crusher_cli
    CLI11::CLI11
)

target_include_directories(odbc-crusher PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Install target
install(TARGETS odbc-crusher
    RUNTIME DESTINATION bin
)
