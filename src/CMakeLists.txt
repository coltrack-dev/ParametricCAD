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
    application/VisibilityManager.cpp
    application/VisibilityManager.h
    application/IfcImporter.cpp
    application/IfcImporter.h
    application/BimNavigationModel.cpp
    application/BimNavigationModel.h
    import/IfcOpenShellAdapter.cpp
    import/IfcOpenShellAdapter.h
    import/IfcMetadata.cpp
    import/IfcMetadata.h
    application/VisibilityMode.h
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
    viewer/BimNavigationPanel.cpp
    viewer/BimNavigationPanel.h
    viewer/BimInspectorPanel.cpp
    viewer/BimInspectorPanel.h
    viewer/CadViewer.cpp
    viewer/CadViewer.h
    viewer/SketchEntityPicker.cpp
    viewer/SketchEntityPicker.h
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
    model/ShapePayload.cpp
    model/ShapePayload.h
    model/ProjectArchive.cpp
    model/ProjectArchive.h
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
    operations/ImportedFeature.cpp
    operations/ImportedFeature.h
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

if(WIN32)
    # OCCT's native WNT window headers include Windows SDK declarations;
    # prevent min/max macros from colliding with the C++/Qt code.
    target_compile_definitions(ParametricCAD PRIVATE NOMINMAX)
endif()

if(PARAMETRIC_CAD_ENABLE_IFC)
    target_compile_definitions(ParametricCAD PRIVATE
        PARAMETRIC_CAD_HAS_IFCOPENSHELL=1
        HAS_SCHEMA_2x3
        HAS_SCHEMA_4
        "SCHEMA_SEQ=(2x3)(4)")
    target_include_directories(ParametricCAD SYSTEM PRIVATE ${PARAMETRIC_CAD_IFC_INCLUDE_DIR})
endif()

target_link_libraries(ParametricCAD
        PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Concurrent
        ${PARAMETRIC_CAD_MINIZIP_TARGET}
        ParametricCAD::OCCTModeling
        ParametricCAD::OCCTVisualization
)

if(PARAMETRIC_CAD_ENABLE_IFC)
    target_link_libraries(ParametricCAD PRIVATE
        ${PARAMETRIC_CAD_IFC_GEOM_LIBRARY}
        ${PARAMETRIC_CAD_IFC_PARSE_LIBRARY}
        ${PARAMETRIC_CAD_IFC_SCHEMA_2X3_LIBRARY}
        ${PARAMETRIC_CAD_IFC_SCHEMA_4_LIBRARY})
endif()

if(MSVC)
    target_compile_options(ParametricCAD PRIVATE /W4 /permissive-)
else()
    target_compile_options(ParametricCAD PRIVATE -Wall -Wextra -Wpedantic)
endif()
