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

add_executable(cae_mesh_tests
    cae_mesh_tests.cpp
)
target_include_directories(cae_mesh_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(cae_mesh_tests PRIVATE ParametricCAD::CAECore)
add_test(NAME cae_mesh_tests COMMAND cae_mesh_tests)

add_executable(cae_fem_tests
    cae_fem_tests.cpp
)
target_include_directories(cae_fem_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(cae_fem_tests PRIVATE ParametricCAD::CAECore)
add_test(NAME cae_fem_tests COMMAND cae_fem_tests)

find_program(PARAMETRIC_CAD_GMSH_EXECUTABLE gmsh)
if(PARAMETRIC_CAD_GMSH_EXECUTABLE)
    add_executable(gmsh_mesh_tests
        gmsh_mesh_tests.cpp
        ../src/cae/GmshMeshGenerator.cpp
    )
    target_include_directories(gmsh_mesh_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(gmsh_mesh_tests PRIVATE
        Qt6::Core
        ParametricCAD::CAECore
        ParametricCAD::OCCTModeling)
    add_test(NAME gmsh_mesh_tests COMMAND gmsh_mesh_tests)
    set_tests_properties(gmsh_mesh_tests PROPERTIES
        ENVIRONMENT "PARAMETRIC_CAD_GMSH_EXECUTABLE=${PARAMETRIC_CAD_GMSH_EXECUTABLE}")
else()
    message(STATUS "Gmsh not found: gmsh_mesh_tests will not be built")
endif()

add_executable(push_pull_drag_tests
    push_pull_drag_tests.cpp
    ../src/viewer/PushPullDrag.cpp
    ../src/viewer/TransformMath.cpp
    ../src/viewer/SnapManager.cpp
    ../src/model/TopologicalReference.cpp
    ../src/model/Body.cpp
    ../src/model/ParametricFeature.cpp
    ../src/model/ShapePayload.cpp
    ../src/viewer/TransformGizmoPicking.cpp
)
target_include_directories(push_pull_drag_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(push_pull_drag_tests PRIVATE Qt6::Gui
    ParametricCAD::OCCTModeling
    ParametricCAD::OCCTVisualization)
add_test(NAME push_pull_drag_tests COMMAND push_pull_drag_tests)

add_executable(interactive_operation_tests interactive_operation_tests.cpp)
target_include_directories(interactive_operation_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME interactive_operation_tests COMMAND interactive_operation_tests)

add_executable(selection_tests
    selection_tests.cpp
    ../src/viewer/SelectionAdapter.cpp
)
target_include_directories(selection_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(selection_tests PRIVATE Qt6::Core Qt6::Gui
    ParametricCAD::OCCTModeling
    ParametricCAD::OCCTVisualization)
add_test(NAME selection_tests COMMAND selection_tests)

find_package(Qt6 REQUIRED COMPONENTS Test)

add_library(model_test_support STATIC
    ../src/application/ModelingController.cpp
    ../src/application/SelectionResolver.cpp
    ../src/application/ProjectController.cpp
    ../src/application/IfcImporter.cpp
    ../src/application/SimulationController.cpp
    ../src/application/BimNavigationModel.cpp
    ../src/application/VisibilityManager.cpp
    ../src/model/ProjectFile.cpp
    ../src/commands/FeatureCommands.cpp
    ../src/model/FeatureVisibility.cpp
    ../src/model/ProjectArchive.cpp
    ../src/model/Document.cpp
    ../src/model/Feature.cpp
    ../src/model/Body.cpp
    ../src/model/ParametricFeature.cpp
    ../src/model/ShapePayload.cpp
    ../src/model/TopologicalReference.cpp
    ../src/operations/BoxFeature.cpp
    ../src/operations/CylinderFeature.cpp
    ../src/operations/BasicFeatures.cpp
    ../src/operations/ParametricFeatures.cpp
    ../src/operations/SketchProfileBuilder.cpp
    ../src/operations/SketchPathBuilder.cpp
    ../src/operations/SketchTrimService.cpp
    ../src/operations/SketchExtendService.cpp
    ../src/operations/SketchFilletService.cpp
    ../src/operations/SketchChamferService.cpp
    ../src/operations/SketchConstraintSolver.cpp
    ../src/operations/PatternFeatures.cpp
    ../src/operations/ImportedFeature.cpp
    ../src/import/IfcOpenShellAdapter090.cpp
    ../src/import/IfcMetadata.cpp
    ../src/cae/GmshMeshGenerator.cpp
)
target_include_directories(model_test_support PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(model_test_support PUBLIC Qt6::Core Qt6::Gui
    ${PARAMETRIC_CAD_MINIZIP_TARGET}
    ParametricCAD::OCCTModeling
    ParametricCAD::OCCTVisualization
    ParametricCAD::CAECore)

if(PARAMETRIC_CAD_ENABLE_IFC)
    target_compile_definitions(model_test_support PUBLIC
        PARAMETRIC_CAD_HAS_IFCOPENSHELL=1
        PARAMETRIC_CAD_IFCOPENSHELL_090=1
        PARAMETRIC_CAD_IFCOPENSHELL_PLUGIN_DIR="${PARAMETRIC_CAD_IFC_PLUGIN_DIR}")
    target_include_directories(model_test_support SYSTEM PUBLIC ${PARAMETRIC_CAD_IFC_INCLUDE_DIR})
    target_link_libraries(model_test_support PUBLIC
        ${PARAMETRIC_CAD_IFC_GEOM_LIBRARY}
        ${PARAMETRIC_CAD_IFC_PARSE_LIBRARY}
        ${PARAMETRIC_CAD_IFC_OCCT_KERNEL_LIBRARY})
endif()
add_executable(project_file_tests project_file_tests.cpp)
target_compile_definitions(project_file_tests PRIVATE
    PARAMETRIC_CAD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(project_file_tests PRIVATE model_test_support)
add_test(NAME project_file_tests COMMAND project_file_tests)

add_executable(project_archive_tests project_archive_tests.cpp)
target_link_libraries(project_archive_tests PRIVATE model_test_support)
add_test(NAME project_archive_tests COMMAND project_archive_tests)

add_executable(ifc_import_tests ifc_import_tests.cpp)
target_compile_definitions(ifc_import_tests PRIVATE
    PARAMETRIC_CAD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(ifc_import_tests PRIVATE model_test_support)
add_test(NAME ifc_import_tests COMMAND ifc_import_tests)

add_executable(ifc_workflow_tests ifc_workflow_tests.cpp)
target_compile_definitions(ifc_workflow_tests PRIVATE
    PARAMETRIC_CAD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(ifc_workflow_tests PRIVATE model_test_support)
add_test(NAME ifc_workflow_tests COMMAND ifc_workflow_tests)

if(PARAMETRIC_CAD_ENABLE_IFC)
    set(_parametriccad_ifc_test_rpath "${PARAMETRIC_CAD_IFC_LIBRARY_DIR}")
    set_target_properties(ifc_import_tests ifc_workflow_tests
        PROPERTIES
            BUILD_RPATH "${_parametriccad_ifc_test_rpath}"
            INSTALL_RPATH "${_parametriccad_ifc_test_rpath}")
    add_executable(ifc_building_acceptance ifc_building_acceptance.cpp)
    target_compile_definitions(ifc_building_acceptance PRIVATE
        PARAMETRIC_CAD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(ifc_building_acceptance PRIVATE model_test_support)
    set_target_properties(ifc_building_acceptance
        PROPERTIES
            BUILD_RPATH "${_parametriccad_ifc_test_rpath}"
            INSTALL_RPATH "${_parametriccad_ifc_test_rpath}")
    add_test(NAME ifc_building_acceptance COMMAND ifc_building_acceptance)
    unset(_parametriccad_ifc_test_rpath)
endif()

add_executable(model_tests model_tests.cpp)
target_link_libraries(model_tests PRIVATE model_test_support Qt6::Test)
add_test(NAME model_tests COMMAND model_tests)
set_tests_properties(model_tests PROPERTIES ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=;QT_QPA_PLATFORM=offscreen")

add_executable(bim_navigation_tests bim_navigation_tests.cpp)
target_link_libraries(bim_navigation_tests PRIVATE model_test_support Qt6::Test)
add_test(NAME bim_navigation_tests COMMAND bim_navigation_tests)
set_tests_properties(bim_navigation_tests PROPERTIES ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=;QT_QPA_PLATFORM=offscreen")

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

set(_main_window_test_support_sources
    "${PROJECT_SOURCE_DIR}/src/resources/toolbar_icons.qrc"
    "${PROJECT_SOURCE_DIR}/src/viewer/MainWindow.cpp"
    "${PROJECT_SOURCE_DIR}/src/viewer/MainWindow.h"
    "${PROJECT_SOURCE_DIR}/src/viewer/ModelPresenter.cpp"
    "${PROJECT_SOURCE_DIR}/src/viewer/PushPullDrag.cpp"
    "${PROJECT_SOURCE_DIR}/src/viewer/TransformMath.cpp"
    "${PROJECT_SOURCE_DIR}/src/viewer/TransformGizmo.cpp"
    "${PROJECT_SOURCE_DIR}/src/viewer/TransformGizmoPicking.cpp"
    "${PROJECT_SOURCE_DIR}/src/viewer/SnapManager.cpp"
    "${PROJECT_SOURCE_DIR}/src/viewer/SelectionAdapter.cpp"
    "${PROJECT_SOURCE_DIR}/src/viewer/FeatureEditorPanel.cpp"
    "${PROJECT_SOURCE_DIR}/src/viewer/BimNavigationPanel.cpp"
    "${PROJECT_SOURCE_DIR}/src/viewer/BimInspectorPanel.cpp"
    "${PROJECT_SOURCE_DIR}/src/viewer/CadViewer.cpp"
    "${PROJECT_SOURCE_DIR}/src/viewer/SketchEntityPicker.cpp"
)
add_library(main_window_test_support STATIC ${_main_window_test_support_sources})
unset(_main_window_test_support_sources)
target_include_directories(main_window_test_support PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(main_window_test_support PUBLIC
    model_test_support Qt6::Test Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Concurrent ParametricCAD::OCCTVisualization)

add_executable(main_window_tests main_window_tests.cpp)
target_link_libraries(main_window_tests PRIVATE main_window_test_support)
add_test(NAME main_window_tests COMMAND main_window_tests)
set_tests_properties(main_window_tests PROPERTIES
    ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=;QT_QPA_PLATFORM=offscreen")

add_executable(controller_tests controller_tests.cpp)
target_link_libraries(controller_tests PRIVATE model_test_support Qt6::Test)
add_test(NAME controller_tests COMMAND controller_tests)

add_executable(sketch_trim_tests sketch_trim_tests.cpp)
target_link_libraries(sketch_trim_tests PRIVATE model_test_support)
add_test(NAME sketch_trim_tests COMMAND sketch_trim_tests)
