cmake_minimum_required(VERSION 3.24)

project(ParametricCAD
        VERSION 0.1.0
        DESCRIPTION "Educational parametric CAD application"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(PARAMETRIC_CAD_BUILD_TESTS "Build unit tests" ON)
option(PARAMETRIC_CAD_ENABLE_IFC "Enable the optional pinned IfcOpenShell adapter" OFF)

set(PARAMETRIC_CAD_IFCOPENSHELL_ROOT "" CACHE PATH
    "Root of a separately built IfcOpenShell v0.7.1 installation")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Concurrent)

# Open CASCADE package naming differs between distributions.
# The official/config package normally exposes OpenCASCADE_* variables.
find_package(OpenCASCADE REQUIRED)

if(PARAMETRIC_CAD_ENABLE_IFC)
    if(NOT PARAMETRIC_CAD_IFCOPENSHELL_ROOT)
        message(FATAL_ERROR "PARAMETRIC_CAD_IFCOPENSHELL_ROOT is required when IFC support is enabled")
    endif()
    find_path(PARAMETRIC_CAD_IFC_INCLUDE_DIR
        NAMES ifcgeom_schema_agnostic/IfcGeomIterator.h
        PATHS "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/include"
              "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/src"
        NO_DEFAULT_PATH)
    find_library(PARAMETRIC_CAD_IFC_GEOM_LIBRARY NAMES IfcGeom
        PATHS "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib"
              "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib64"
        NO_DEFAULT_PATH)
    find_library(PARAMETRIC_CAD_IFC_PARSE_LIBRARY NAMES IfcParse
        PATHS "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib"
              "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib64"
        NO_DEFAULT_PATH)
    find_library(PARAMETRIC_CAD_IFC_SCHEMA_2X3_LIBRARY NAMES IfcGeom_ifc2x3
        PATHS "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib"
              "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib64"
        NO_DEFAULT_PATH)
    find_library(PARAMETRIC_CAD_IFC_SCHEMA_4_LIBRARY NAMES IfcGeom_ifc4
        PATHS "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib"
              "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib64"
        NO_DEFAULT_PATH)
    if(NOT PARAMETRIC_CAD_IFC_INCLUDE_DIR OR
       NOT PARAMETRIC_CAD_IFC_GEOM_LIBRARY OR
       NOT PARAMETRIC_CAD_IFC_PARSE_LIBRARY OR
       NOT PARAMETRIC_CAD_IFC_SCHEMA_2X3_LIBRARY OR
       NOT PARAMETRIC_CAD_IFC_SCHEMA_4_LIBRARY)
        message(FATAL_ERROR "IfcOpenShell v0.7.1 headers/libraries were not found under PARAMETRIC_CAD_IFCOPENSHELL_ROOT")
    endif()
endif()

add_subdirectory(src)

if(PARAMETRIC_CAD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
