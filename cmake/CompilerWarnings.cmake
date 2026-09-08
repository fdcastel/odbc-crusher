# Compiler warnings configuration

# Create interface library for warnings
add_library(project_warnings INTERFACE)

if(MSVC)
    target_compile_options(project_warnings INTERFACE
        /W4           # Warning level 4
        /WX           # Treat warnings as errors
        /permissive-  # Standards conformance
        /w14242       # 'identifier': conversion, possible loss of data
        /w14254       # 'operator': conversion, possible loss of data
        /w14263       # 'function': member function does not override any base class virtual member function
        /w14265       # 'classname': class has virtual functions, but destructor is not virtual
        /w14287       # 'operator': unsigned/negative constant mismatch
        /we4289       # nonstandard extension used: 'variable': loop control variable declared in the for-loop is used outside the for-loop scope
        /w14296       # 'operator': expression is always 'boolean_value'
        /w14311       # 'variable': pointer truncation from 'type1' to 'type2'
        /w14545       # expression before comma evaluates to a function which is missing an argument list
        /w14546       # function call before comma missing argument list
        /w14547       # 'operator': operator before comma has no effect; expected operator with side-effect
        /w14549       # 'operator': operator before comma has no effect; did you intend 'operator'?
        /w14555       # expression has no effect; expected expression with side-effect
        /w14619       # pragma warning: there is no warning number 'number'
        /w14640       # Enable warning on thread un-safe static member initialization
        /w14826       # Conversion from 'type1' to 'type2' is sign-extended
        /w14905       # wide string literal cast to 'LPSTR'
        /w14906       # string literal cast to 'LPWSTR'
        /w14928       # illegal copy-initialization; more than one user-defined conversion has been implicitly applied
    )
else()
    # E5: this set had no -Werror, so Linux and macOS emitted warnings and
    # ignored every one of them - 1,636 on the first green run after E1 -
    # while AGENTS.md promised "compiles without warnings on Windows *and*
    # Linux". The promise was enforced on Windows alone.
    #
    # Three flags are deliberately not in this list, and they are the ones
    # that produced 1,447 of those 1,636:
    #
    #   -Wold-style-cast   780   `(SQLCHAR*)"literal"` at nearly every ODBC
    #                            call site. The C API takes SQLCHAR* for what
    #                            are string literals in C++; the alternative
    #                            is a const_cast/reinterpret_cast pair on
    #                            every line, which is not more readable.
    #   -Wsign-conversion  667   SQLSMALLINT/SQLULEN/size_t conversions, again
    #                            inherent to an API that mixes signed and
    #                            unsigned lengths by design.
    #   -Wuseless-cast      93   GCC-only, and mostly the same casts seen from
    #                            the other side.
    #
    # E5 measured and triaged the remainder and found no live defect in it,
    # so this is not "turn off the ones that are failing" - it is dropping
    # three flags whose output is unactionable here and enforcing everything
    # else. A warning nobody can act on is noise, and the noise is what let
    # the other 189 hide.
    target_compile_options(project_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Werror
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(project_warnings INTERFACE
            -Wmisleading-indentation
            -Wduplicated-cond
            -Wduplicated-branches
            -Wlogical-op
        )
    endif()
endif()

# Create interface library for options
add_library(project_options INTERFACE)

# Set position independent code
set_target_properties(project_options PROPERTIES
    INTERFACE_POSITION_INDEPENDENT_CODE ON
)
