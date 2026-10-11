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
option(PARAMETRIC_CAD_ENABLE_IFC "Enable the IfcOpenShell 0.9 adapter" ON)

set(PARAMETRIC_CAD_IFCOPENSHELL_ROOT "" CACHE PATH
    "Prefix or build tree of an IfcOpenShell 0.9.0 installation")
set(PARAMETRIC_CAD_IFCOPENSHELL_SOURCE_DIR "" CACHE PATH
    "Optional IfcOpenShell 0.9.0 source tree for the OpenCASCADE conversion bridge")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Concurrent)

# Open CASCADE package naming differs between distributions.
# The official/config package normally exposes OpenCASCADE_* variables.
find_package(OpenCASCADE REQUIRED)

# Keep the OCCT toolkit composition in one place.  These interface targets
# are shared by the application and tests that compile viewer/selection code;
# this also keeps MSVC's static symbol resolution consistent with Unix builds.
add_library(parametriccad_occt_modeling INTERFACE)
target_include_directories(parametriccad_occt_modeling INTERFACE
    ${OpenCASCADE_INCLUDE_DIR})
if(DEFINED OpenCASCADE_LIBRARY_DIR)
    target_link_directories(parametriccad_occt_modeling INTERFACE
        ${OpenCASCADE_LIBRARY_DIR})
endif()
target_link_libraries(parametriccad_occt_modeling INTERFACE
    TKernel
    TKMath
    TKG2d
    TKG3d
    TKGeomBase
    TKBRep
    TKGeomAlgo
    TKTopAlgo
    TKPrim
    TKBO
    TKFillet
    TKOffset)

add_library(parametriccad_occt_visualization INTERFACE)
target_link_libraries(parametriccad_occt_visualization INTERFACE
    TKService
    TKV3d
    TKOpenGl)

add_library(ParametricCAD::OCCTModeling ALIAS parametriccad_occt_modeling)
add_library(ParametricCAD::OCCTVisualization ALIAS parametriccad_occt_visualization)

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
    set(_ifc_config_paths
        "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib/cmake/IfcOpenShell"
        "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib64/cmake/IfcOpenShell"
        "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/build/lib/cmake/IfcOpenShell"
        "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/build")
    find_package(IfcOpenShell 0.9.0 CONFIG QUIET
        PATHS ${_ifc_config_paths} NO_DEFAULT_PATH)
    unset(_ifc_config_paths)
    if(NOT TARGET IfcOpenShell::IfcGeom OR NOT TARGET IfcOpenShell::IfcParse
       OR NOT TARGET IfcOpenShell::geometry_kernel_opencascade)
        message(FATAL_ERROR
            "IfcOpenShell 0.9.0 CMake targets (IfcGeom, IfcParse, or the "
            "OpenCASCADE geometry kernel) were not found. Set "
            "PARAMETRIC_CAD_IFCOPENSHELL_ROOT to its install prefix (or a build "
            "tree exporting IfcOpenShellTargets.cmake). ParametricCAD requires "
            "the 0.9.0 API and does not fall back to older libraries.")
    endif()
    get_target_property(_ifc_target_include_dirs
        IfcOpenShell::IfcGeom INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT _ifc_target_include_dirs OR _ifc_target_include_dirs STREQUAL "_ifc_target_include_dirs-NOTFOUND")
        set(_ifc_target_include_dirs
            "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/include")
    endif()
    find_path(_ifc_internal_include_dir
        NAMES ifcgeom/kernels/ifc_geomlibrary_api.h
        PATHS ${_ifc_target_include_dirs}
        NO_DEFAULT_PATH)
    find_path(_ifc_source_include_dir
        NAMES ifcgeom/kernels/ifc_geomlibrary_api.h
        PATHS
            "${PARAMETRIC_CAD_IFCOPENSHELL_SOURCE_DIR}/src"
            "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/src"
        NO_DEFAULT_PATH)
    if(_ifc_source_include_dir)
        # IfcOpenShell 0.9.0's installed OpenCASCADE conversion header refers
        # to this private API header, which some 0.9.0 installs omit. Prefer
        # the matching source header before the installed public headers; this
        # does not modify the SDK or the system include tree.
        set(PARAMETRIC_CAD_IFC_INCLUDE_DIR
            "${_ifc_source_include_dir};${_ifc_target_include_dirs}")
    elseif(_ifc_internal_include_dir)
        set(PARAMETRIC_CAD_IFC_INCLUDE_DIR "${_ifc_target_include_dirs}")
    else()
        message(FATAL_ERROR
            "The IfcOpenShell 0.9.0 SDK is missing ifc_geomlibrary_api.h, which is "
            "required by its OpenCASCADE conversion bridge. Set "
            "PARAMETRIC_CAD_IFCOPENSHELL_SOURCE_DIR to the matching 0.9.0 source "
            "tree, or use an installation containing the complete kernel headers.")
    endif()
    unset(_ifc_internal_include_dir)
    unset(_ifc_source_include_dir)
    unset(_ifc_target_include_dirs)
    set(PARAMETRIC_CAD_IFC_GEOM_LIBRARY IfcOpenShell::IfcGeom)
    set(PARAMETRIC_CAD_IFC_PARSE_LIBRARY IfcOpenShell::IfcParse)
    set(PARAMETRIC_CAD_IFC_OCCT_KERNEL_LIBRARY
        IfcOpenShell::geometry_kernel_opencascade)
    set(PARAMETRIC_CAD_IFC_SCHEMA_2X3_LIBRARY "")
    set(PARAMETRIC_CAD_IFC_SCHEMA_4_LIBRARY "")
    if(DEFINED IFCOPENSHELL_LIBRARY_DIR)
        set(PARAMETRIC_CAD_IFC_LIBRARY_DIR "${IFCOPENSHELL_LIBRARY_DIR}")
    else()
        set(PARAMETRIC_CAD_IFC_LIBRARY_DIR "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib")
    endif()
endif()

add_subdirectory(src)

if(PARAMETRIC_CAD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PARAMETRIC_CAD_ENABLE_IFC)
    set(_parametriccad_ifc_rpath "${PARAMETRIC_CAD_IFC_LIBRARY_DIR}")
    set_target_properties(ParametricCAD PROPERTIES
        BUILD_RPATH "${_parametriccad_ifc_rpath}"
        INSTALL_RPATH "${_parametriccad_ifc_rpath}")
    if(TARGET model_test_support)
        set_target_properties(model_test_support PROPERTIES
            BUILD_RPATH "${_parametriccad_ifc_rpath}"
            INSTALL_RPATH "${_parametriccad_ifc_rpath}")
    endif()
    unset(_parametriccad_ifc_rpath)
endif()
