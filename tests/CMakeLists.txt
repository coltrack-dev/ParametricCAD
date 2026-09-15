add_executable(geometry_tests
    geometry_tests.cpp
)

target_include_directories(geometry_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(geometry_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(geometry_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME geometry_tests COMMAND geometry_tests)
