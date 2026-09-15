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

add_executable(project_file_tests
    project_file_tests.cpp
    ../src/model/ProjectFile.cpp
    ../src/model/Document.cpp
    ../src/model/Feature.cpp
    ../src/model/Body.cpp
    ../src/model/ParametricFeature.cpp
    ../src/operations/BoxFeature.cpp
    ../src/operations/CylinderFeature.cpp
    ../src/operations/BasicFeatures.cpp
    ../src/operations/ParametricFeatures.cpp
)
target_include_directories(project_file_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(project_file_tests SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(project_file_tests PRIVATE Qt6::Core
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKFillet TKOffset)
add_test(NAME project_file_tests COMMAND project_file_tests)
