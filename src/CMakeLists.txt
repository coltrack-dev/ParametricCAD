set(PARAMETRIC_CAD_SOURCES
    app/main.cpp
    viewer/MainWindow.cpp
    viewer/MainWindow.h
    viewer/CadViewer.cpp
    viewer/CadViewer.h
    model/Document.cpp
    model/Document.h
    model/Feature.cpp
    model/Feature.h
    model/ParametricFeature.cpp
    model/ParametricFeature.h
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
    geometry/Point3D.h
    geometry/Vector3D.h
)

add_executable(ParametricCAD ${PARAMETRIC_CAD_SOURCES})

target_include_directories(ParametricCAD
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(ParametricCAD
        SYSTEM PRIVATE
        ${OpenCASCADE_INCLUDE_DIR}
)

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
