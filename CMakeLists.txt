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
    "Prefix or build tree of an IfcOpenShell 0.9.x installation")
set(PARAMETRIC_CAD_IFCOPENSHELL_SOURCE_DIR "" CACHE PATH
    "Optional IfcOpenShell 0.9.x source tree for development headers")

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

    # IfcOpenShell 0.9.x exports targets in some builds, but the installed
    # package is also commonly shipped as only headers, versioned core
    # libraries and runtime plugins. Resolve those artifacts directly instead
    # of relying on the old 0.7 target names or on a build-tree-only export.
    set(_ifc_search_dirs
        "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib"
        "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/lib64"
        "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/bin")
    find_path(_ifc_include_dir
        NAMES ifcparse/file.h ifcgeom/iterator.h plugin/plugin.h
        PATHS "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/include"
        "${PARAMETRIC_CAD_IFCOPENSHELL_SOURCE_DIR}/src"
        "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/src"
        NO_DEFAULT_PATH)
    if(NOT _ifc_include_dir)
        message(FATAL_ERROR
            "IfcOpenShell 0.9.x headers were not found below "
            "PARAMETRIC_CAD_IFCOPENSHELL_ROOT. Expected ifcparse/file.h, "
            "ifcgeom/iterator.h and plugin/plugin.h.")
    endif()
    find_path(_ifc_source_include_dir
        NAMES ifcgeom/kernels/ifc_geomlibrary_api.h
        PATHS
            "${PARAMETRIC_CAD_IFCOPENSHELL_SOURCE_DIR}/src"
            "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT}/src"
        NO_DEFAULT_PATH)
    if(_ifc_source_include_dir)
        # Some 0.9.x binary packages omit this private API header even though
        # their installed OpenCASCADE result header includes it relatively.
        # Use the matching source include tree when it is supplied/available.
        set(_ifc_include_dirs "${_ifc_source_include_dir};${_ifc_include_dir}")
    else()
        message(FATAL_ERROR
            "This IfcOpenShell 0.9.x installation is missing "
            "ifcgeom/kernels/ifc_geomlibrary_api.h, which is referenced by "
            "its OpenCASCADE conversion header. Install the complete SDK or "
            "set PARAMETRIC_CAD_IFCOPENSHELL_SOURCE_DIR to the matching "
            "IfcOpenShell 0.9.x source tree.")
    endif()
    unset(_ifc_eigen_include_dir CACHE)
    find_path(_ifc_eigen_include_dir NAMES Eigen/Dense
        PATH_SUFFIXES eigen3)
    if(NOT _ifc_eigen_include_dir)
        message(FATAL_ERROR
            "The Eigen headers required by IfcOpenShell 0.9.x were not found "
            "(Eigen/Dense).")
    endif()

    function(parametriccad_find_ifc_core_library output base_name)
        unset(_ifc_library CACHE)
        find_library(_ifc_library
            NAMES "${base_name}"
            PATHS ${_ifc_search_dirs}
            NO_DEFAULT_PATH)
        if(NOT _ifc_library)
            # Versioned Unix libraries are intentionally globbed here: 0.9.x
            # installs may omit the unversioned linker symlink. The version
            # check excludes accidental 0.7/0.8 libraries without naming a
            # patch release.
            set(_ifc_candidates)
            foreach(_ifc_dir IN LISTS _ifc_search_dirs)
                file(GLOB _ifc_dir_candidates LIST_DIRECTORIES false
                    "${_ifc_dir}/lib${base_name}.0.9.*.dylib"
                    "${_ifc_dir}/lib${base_name}.so.0.9.*"
                    "${_ifc_dir}/${base_name}.0.9.*.dll")
                list(APPEND _ifc_candidates ${_ifc_dir_candidates})
            endforeach()
            list(SORT _ifc_candidates)
            if(_ifc_candidates)
                list(GET _ifc_candidates 0 _ifc_library)
            endif()
        endif()
        if(NOT _ifc_library OR NOT EXISTS "${_ifc_library}")
            message(FATAL_ERROR
                "IfcOpenShell 0.9.x core library '${base_name}' was not found "
                "in ${_ifc_search_dirs}.")
        endif()
        set(${output} "${_ifc_library}" PARENT_SCOPE)
    endfunction()

    parametriccad_find_ifc_core_library(_ifc_plugin_library ifcopenshell.plugin)
    parametriccad_find_ifc_core_library(_ifc_parse_library ifcopenshell.parse)
    parametriccad_find_ifc_core_library(_ifc_geometry_library ifcopenshell.geometry)

    add_library(ParametricCAD::IfcOpenShellPlugin UNKNOWN IMPORTED GLOBAL)
    set_target_properties(ParametricCAD::IfcOpenShellPlugin PROPERTIES
        IMPORTED_LOCATION "${_ifc_plugin_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${_ifc_include_dirs};${_ifc_eigen_include_dir}")
    add_library(ParametricCAD::IfcOpenShellParse UNKNOWN IMPORTED GLOBAL)
    set_target_properties(ParametricCAD::IfcOpenShellParse PROPERTIES
        IMPORTED_LOCATION "${_ifc_parse_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${_ifc_include_dirs};${_ifc_eigen_include_dir}"
        INTERFACE_LINK_LIBRARIES ParametricCAD::IfcOpenShellPlugin)
    add_library(ParametricCAD::IfcOpenShellGeometry UNKNOWN IMPORTED GLOBAL)
    set_target_properties(ParametricCAD::IfcOpenShellGeometry PROPERTIES
        IMPORTED_LOCATION "${_ifc_geometry_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${_ifc_include_dirs};${_ifc_eigen_include_dir}"
        INTERFACE_LINK_LIBRARIES
            "ParametricCAD::IfcOpenShellParse;ParametricCAD::IfcOpenShellPlugin")

    # These are not link dependencies. IfcOpenShell 0.9.x discovers them by
    # basename through plugin::manager after the core libraries are loaded.
    # Validate the required schema/mapping/kernel modules at configure time.
    set(PARAMETRIC_CAD_IFC_PLUGIN_DIR "" CACHE PATH
        "Directory containing IfcOpenShell 0.9.x runtime plugins")
    if(NOT PARAMETRIC_CAD_IFC_PLUGIN_DIR)
        foreach(_ifc_dir IN LISTS _ifc_search_dirs)
            if(EXISTS "${_ifc_dir}/ifcopenshell_parse_schema_ifc2x3.dylib"
               OR EXISTS "${_ifc_dir}/ifcopenshell_parse_schema_ifc2x3.so"
               OR EXISTS "${_ifc_dir}/ifcopenshell_parse_schema_ifc2x3.dll")
                set(PARAMETRIC_CAD_IFC_PLUGIN_DIR "${_ifc_dir}")
                break()
            endif()
        endforeach()
    endif()
    if(NOT PARAMETRIC_CAD_IFC_PLUGIN_DIR)
        message(FATAL_ERROR
            "IfcOpenShell 0.9.x runtime plugin directory was not found. "
            "Expected parse_schema_ifc2x3, parse_schema_ifc4, "
            "geometry_mapping_ifc2x3, geometry_mapping_ifc4 and "
            "geometry_kernel_opencascade plugins.")
    endif()
    foreach(_ifc_plugin IN ITEMS
        ifcopenshell_parse_schema_ifc2x3
        ifcopenshell_parse_schema_ifc4
        ifcopenshell_geometry_mapping_ifc2x3
        ifcopenshell_geometry_mapping_ifc4
        ifcopenshell_geometry_kernel_opencascade)
        file(GLOB _ifc_plugin_matches LIST_DIRECTORIES false
            "${PARAMETRIC_CAD_IFC_PLUGIN_DIR}/${_ifc_plugin}.dylib"
            "${PARAMETRIC_CAD_IFC_PLUGIN_DIR}/${_ifc_plugin}.so"
            "${PARAMETRIC_CAD_IFC_PLUGIN_DIR}/${_ifc_plugin}.dll")
        if(NOT _ifc_plugin_matches)
            message(FATAL_ERROR
                "IfcOpenShell 0.9.x plugin '${_ifc_plugin}' is missing from "
                "${PARAMETRIC_CAD_IFC_PLUGIN_DIR}.")
        endif()
        list(APPEND PARAMETRIC_CAD_IFC_RUNTIME_PLUGINS ${_ifc_plugin_matches})
    endforeach()

    list(GET PARAMETRIC_CAD_IFC_RUNTIME_PLUGINS 4 _ifc_kernel_plugin)
    add_library(ParametricCAD::IfcOpenShellGeometryKernelOpenCascade UNKNOWN IMPORTED GLOBAL)
    set_target_properties(ParametricCAD::IfcOpenShellGeometryKernelOpenCascade PROPERTIES
        IMPORTED_LOCATION "${_ifc_kernel_plugin}"
        INTERFACE_INCLUDE_DIRECTORIES "${_ifc_include_dirs};${_ifc_eigen_include_dir}"
        INTERFACE_LINK_LIBRARIES
            "ParametricCAD::IfcOpenShellGeometry;ParametricCAD::IfcOpenShellPlugin;ParametricCAD::OCCTModeling")

    set(PARAMETRIC_CAD_IFC_INCLUDE_DIR "${_ifc_include_dirs};${_ifc_eigen_include_dir}")
    set(PARAMETRIC_CAD_IFC_GEOM_LIBRARY ParametricCAD::IfcOpenShellGeometry)
    set(PARAMETRIC_CAD_IFC_PARSE_LIBRARY ParametricCAD::IfcOpenShellParse)
    set(PARAMETRIC_CAD_IFC_LIBRARY_DIR "${_ifc_plugin_library}")
    get_filename_component(PARAMETRIC_CAD_IFC_LIBRARY_DIR
        "${PARAMETRIC_CAD_IFC_LIBRARY_DIR}" DIRECTORY)
    set(PARAMETRIC_CAD_IFC_OCCT_KERNEL_LIBRARY
        ParametricCAD::IfcOpenShellGeometryKernelOpenCascade)
    set(PARAMETRIC_CAD_IFC_SCHEMA_2X3_LIBRARY "")
    set(PARAMETRIC_CAD_IFC_SCHEMA_4_LIBRARY "")
    set(PARAMETRIC_CAD_IFC_PLUGIN_DIR "${PARAMETRIC_CAD_IFC_PLUGIN_DIR}")
    unset(_ifc_include_dir)
    unset(_ifc_source_include_dir)
    unset(_ifc_include_dirs)
    unset(_ifc_eigen_include_dir)
    unset(_ifc_plugin_library)
    unset(_ifc_parse_library)
    unset(_ifc_geometry_library)
    unset(_ifc_search_dirs)
    unset(_ifc_kernel_plugin)
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
