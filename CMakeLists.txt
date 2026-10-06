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

# Prefer the package target supplied by vcpkg (and other CONFIG packages). It
# carries both the minizip include directory and its transitive dependencies.
# Keep a small Unix fallback for distributions that only provide libminizip
# and headers, without making pkg-config or a platform-specific path required.
set(PARAMETRIC_CAD_MINIZIP_TARGET "")
find_package(unofficial-minizip CONFIG QUIET)
if(TARGET unofficial::minizip::minizip)
    set(PARAMETRIC_CAD_MINIZIP_TARGET unofficial::minizip::minizip)
elseif(TARGET unofficial::minizip::minizip-static)
    set(PARAMETRIC_CAD_MINIZIP_TARGET unofficial::minizip::minizip-static)
endif()

if(NOT PARAMETRIC_CAD_MINIZIP_TARGET)
    if(WIN32)
        message(FATAL_ERROR
            "minizip was not found as the CONFIG target "
            "unofficial::minizip::minizip. Install the vcpkg minizip port "
            "for the active triplet and configure with its vcpkg toolchain.")
    endif()

    find_path(PARAMETRIC_CAD_MINIZIP_INCLUDE_DIR
        NAMES minizip/unzip.h minizip/zip.h)
    find_library(PARAMETRIC_CAD_MINIZIP_LIBRARY NAMES minizip)
    if(NOT PARAMETRIC_CAD_MINIZIP_INCLUDE_DIR OR NOT PARAMETRIC_CAD_MINIZIP_LIBRARY)
        message(FATAL_ERROR "minizip headers and library are required for portable .pcad containers")
    endif()
    add_library(ParametricCAD::minizip UNKNOWN IMPORTED)
    set_target_properties(ParametricCAD::minizip PROPERTIES
        IMPORTED_LOCATION "${PARAMETRIC_CAD_MINIZIP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${PARAMETRIC_CAD_MINIZIP_INCLUDE_DIR}")
    set(PARAMETRIC_CAD_MINIZIP_TARGET ParametricCAD::minizip)
endif()

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
