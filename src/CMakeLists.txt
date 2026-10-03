set(PARAMETRIC_CAD_SOURCES
    app/main.cpp
    application/ModelingController.cpp
    application/ModelingController.h
    application/Selection.h
    application/SelectionResolver.cpp
    application/SelectionResolver.h
    application/FeatureEditingService.h
    application/ProjectController.cpp
    application/ProjectController.h
    commands/FeatureCommands.cpp
    commands/FeatureCommands.h
    model/FeatureVisibility.cpp
    model/FeatureVisibility.h
    viewer/MainWindow.cpp
    viewer/MainWindow.h
    viewer/ModelPresenter.cpp
    viewer/ModelPresenter.h
    viewer/PushPullDrag.cpp
    viewer/PushPullDrag.h
    viewer/TransformMath.cpp
    viewer/TransformMath.h
    viewer/TransformGizmo.cpp
    viewer/TransformGizmo.h
    viewer/TransformGizmoPicking.cpp
    viewer/TransformGizmoPicking.h
    viewer/SelectionAdapter.cpp
    viewer/SelectionAdapter.h
    viewer/SelectionTypes.h
    viewer/SnapManager.cpp
    viewer/SnapManager.h
    viewer/FeatureEditorPanel.cpp
    viewer/FeatureEditorPanel.h
    viewer/CadViewer.cpp
    viewer/CadViewer.h
    model/Document.cpp
    model/Document.h
    model/ProjectFile.cpp
    model/ProjectFile.h
    model/Feature.cpp
    model/Feature.h
    model/ParametricFeature.cpp
    model/ParametricFeature.h
    model/TopologicalReference.cpp
    model/TopologicalReference.h
    model/Body.cpp
    model/Body.h
    operations/BoxFeature.cpp
    operations/BoxFeature.h
    operations/CylinderFeature.cpp
    operations/CylinderFeature.h
    operations/BasicFeatures.cpp
    operations/BasicFeatures.h
    operations/ParametricFeatures.cpp
    operations/ParametricFeatures.h
    operations/SketchProfileBuilder.cpp
    operations/SketchProfileBuilder.h
    operations/SketchTrimService.cpp
    operations/SketchTrimService.h
    operations/SketchExtendService.cpp
    operations/SketchExtendService.h
    operations/SketchConstraintSolver.cpp
    operations/SketchConstraintSolver.h
    operations/PatternFeatures.cpp
    operations/PatternFeatures.h
    geometry/Point3D.h
    geometry/Vector3D.h
)

add_executable(ParametricCAD ${PARAMETRIC_CAD_SOURCES})

if(APPLE)
    set_target_properties(ParametricCAD PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ParametricCAD"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.parametriccad.ParametricCAD"
    )
endif()

target_include_directories(ParametricCAD
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(ParametricCAD
        SYSTEM PRIVATE
        ${OpenCASCADE_INCLUDE_DIR}
)

if(DEFINED OpenCASCADE_LIBRARY_DIR)
    target_link_directories(ParametricCAD PRIVATE ${OpenCASCADE_LIBRARY_DIR})
endif()

target_link_libraries(ParametricCAD
        PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets

        # OCCT Foundation
        TKernel
        TKMath

        # OCCT geometry
        TKG2d
        TKG3d
        TKGeomBase
        TKBRep
        TKGeomAlgo
        TKTopAlgo

        # Modeling operations
        TKPrim
        TKBO
        TKFillet
        TKOffset

        # 3D visualization
        TKService
        TKV3d
        TKOpenGl
)

if(MSVC)
    target_compile_options(ParametricCAD PRIVATE /W4 /permissive-)
else()
    target_compile_options(ParametricCAD PRIVATE -Wall -Wextra -Wpedantic)
endif()
