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
    operations/BoxFeature.cpp
    operations/BoxFeature.h
    operations/CylinderFeature.cpp
    operations/CylinderFeature.h
    geometry/Point3D.h
    geometry/Vector3D.h
)

add_executable(ParametricCAD ${PARAMETRIC_CAD_SOURCES})

target_include_directories(ParametricCAD
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OpenCASCADE_INCLUDE_DIR}
)

target_link_libraries(ParametricCAD
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        ${OpenCASCADE_LIBRARIES}
)

if(MSVC)
    target_compile_options(ParametricCAD PRIVATE /W4 /permissive-)
else()
    target_compile_options(ParametricCAD PRIVATE -Wall -Wextra -Wpedantic)
endif()
