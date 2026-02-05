# Platform-specific configuration

if(WIN32)
    # Windows-specific settings
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    
    # Use UTF-8 for source files
    if(MSVC)
        add_compile_options(/utf-8)
    endif()
    
elseif(UNIX)
    # Linux/Unix-specific settings
    if(NOT APPLE)
        # Linux-specific
        add_compile_definitions(_GNU_SOURCE)
    endif()
endif()
