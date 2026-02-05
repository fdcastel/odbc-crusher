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
    target_compile_options(project_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
    )
    
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(project_warnings INTERFACE
            -Wmisleading-indentation
            -Wduplicated-cond
            -Wduplicated-branches
            -Wlogical-op
            -Wuseless-cast
        )
    endif()
endif()

# Create interface library for options
add_library(project_options INTERFACE)

# Set position independent code
set_target_properties(project_options PROPERTIES
    INTERFACE_POSITION_INDEPENDENT_CODE ON
)
