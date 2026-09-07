# Generate an ELF version script (or a macOS exported-symbols list) from the
# Windows module-definition file — IMPROVEMENT_PLAN.md D2.
#
# `src/mockodbc.def` is a deliberate 58-symbol export set: W variants only for
# the string functions, plain names for the rest, and no ODBC 2.x aliases —
# the pattern MySQL's Unicode driver uses. The `.so` had no visibility control
# at all, so it *also* exported the 38 ANSI entry points Windows hides. The W
# wrappers delegate to those ANSI functions, so unixODBC could reach a path the
# Windows driver manager cannot, and the two platforms were structurally
# different drivers built from one tree.
#
# Generating the list from the `.def` rather than maintaining a second copy is
# the point: a symbol added to one and not the other is exactly the drift this
# is here to stop, and F11 checks the two agree.

function(mock_generate_export_list def_file out_file)
    file(READ "${def_file}" _def_text)
    string(REPLACE ";" "\\;" _def_text "${_def_text}")
    string(REPLACE "\n" ";" _def_lines "${_def_text}")

    set(_symbols "")
    set(_in_exports FALSE)
    foreach(_line IN LISTS _def_lines)
        string(STRIP "${_line}" _line)
        # `;` starts a comment in a .def file; it survives as `\;` after the
        # escaping above.
        if(_line MATCHES "^\\\\?;" OR _line STREQUAL "")
            continue()
        endif()
        if(_line STREQUAL "EXPORTS")
            set(_in_exports TRUE)
            continue()
        endif()
        if(NOT _in_exports)
            continue()
        endif()
        # A symbol line is a bare identifier; anything else (LIBRARY, an
        # ordinal, a DATA marker) is not something we export by name.
        if(_line MATCHES "^([A-Za-z_][A-Za-z0-9_]*)$")
            list(APPEND _symbols "${CMAKE_MATCH_1}")
        endif()
    endforeach()

    list(LENGTH _symbols _count)
    if(_count LESS 40)
        message(FATAL_ERROR
            "Parsed only ${_count} symbols from ${def_file}; expected the "
            "driver's full export set. Refusing to generate an export list "
            "that would hide most of the driver.")
    endif()

    if(APPLE)
        set(_content "")
        foreach(_sym IN LISTS _symbols)
            string(APPEND _content "_${_sym}\n")
        endforeach()
    else()
        set(_content "{\n    global:\n")
        foreach(_sym IN LISTS _symbols)
            string(APPEND _content "        ${_sym};\n")
        endforeach()
        string(APPEND _content "    local:\n        *;\n};\n")
    endif()

    file(WRITE "${out_file}" "${_content}")
    message(STATUS "Export list: ${_count} symbols from ${def_file}")
endfunction()
