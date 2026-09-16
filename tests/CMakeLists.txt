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

find_package(Qt6 REQUIRED COMPONENTS Test)

add_library(model_test_support STATIC
    ../src/model/ProjectFile.cpp
    ../src/commands/FeatureCommands.cpp
    ../src/model/FeatureVisibility.cpp
    ../src/model/Document.cpp
    ../src/model/Feature.cpp
    ../src/model/Body.cpp
    ../src/model/ParametricFeature.cpp
    ../src/operations/BoxFeature.cpp
    ../src/operations/CylinderFeature.cpp
    ../src/operations/BasicFeatures.cpp
    ../src/operations/ParametricFeatures.cpp
)
target_include_directories(model_test_support PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_include_directories(model_test_support SYSTEM PUBLIC ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(model_test_support PUBLIC Qt6::Core Qt6::Gui
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKFillet TKOffset)
add_executable(project_file_tests project_file_tests.cpp)
target_link_libraries(project_file_tests PRIVATE model_test_support)
add_test(NAME project_file_tests COMMAND project_file_tests)

add_executable(model_tests model_tests.cpp)
target_link_libraries(model_tests PRIVATE model_test_support Qt6::Test)
add_test(NAME model_tests COMMAND model_tests)
set_tests_properties(model_tests PROPERTIES ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=;QT_QPA_PLATFORM=offscreen")

add_executable(sketch_face_tests sketch_face_tests.cpp)
target_link_libraries(sketch_face_tests PRIVATE model_test_support Qt6::Test)
add_test(NAME sketch_face_tests COMMAND sketch_face_tests)
set_tests_properties(sketch_face_tests PROPERTIES ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=;QT_QPA_PLATFORM=offscreen")

add_executable(boolean_cut_tests boolean_cut_tests.cpp)
target_link_libraries(boolean_cut_tests PRIVATE model_test_support Qt6::Test)
add_test(NAME boolean_cut_tests COMMAND boolean_cut_tests)
set_tests_properties(boolean_cut_tests PROPERTIES ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=;QT_QPA_PLATFORM=offscreen")

add_executable(undo_tests undo_tests.cpp)
target_link_libraries(undo_tests PRIVATE model_test_support Qt6::Test)
add_test(NAME undo_tests COMMAND undo_tests)

add_executable(undo_panel_tests undo_panel_tests.cpp ../src/viewer/FeatureEditorPanel.cpp)
target_link_libraries(undo_panel_tests PRIVATE model_test_support Qt6::Test Qt6::Widgets)
add_test(NAME undo_panel_tests COMMAND undo_panel_tests)
set_tests_properties(undo_panel_tests PROPERTIES ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=;QT_QPA_PLATFORM=offscreen")
