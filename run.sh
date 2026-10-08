#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

case "$(uname -s)" in
    Darwin)
        build_dir="${PARAMETRIC_CAD_BUILD_DIR:-$repo_root/build-macos}"
        cache="$build_dir/CMakeCache.txt"
        ifc_root="${PARAMETRIC_CAD_IFCOPENSHELL_ROOT:-}"

        # macOS development builds default to the documented no-IFC
        # configuration. Set PARAMETRIC_CAD_IFCOPENSHELL_ROOT to enable IFC.
        if [[ -n "$ifc_root" ]]; then
            ifc_root=$(cd -- "$ifc_root" && pwd)
            ifc_enabled=ON
        else
            ifc_enabled=OFF
        fi

        configure_macos=0
        if [[ ! -f "$cache" ]]; then
            configure_macos=1
        elif ! grep -Fqx "PARAMETRIC_CAD_ENABLE_IFC:BOOL=$ifc_enabled" "$cache"; then
            configure_macos=1
        fi

        if [[ "$configure_macos" == 1 ]]; then
            cmake_args=(
                -S "$repo_root"
                -B "$build_dir"
                -G Ninja
                -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
                -DPARAMETRIC_CAD_BUILD_TESTS=ON
                -DPARAMETRIC_CAD_ENABLE_IFC="$ifc_enabled"
            )
            if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
                cmake_args+=("-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH")
            fi
            if [[ -n "${Qt6_ROOT:-}" ]]; then
                cmake_args+=("-DQt6_ROOT=$Qt6_ROOT")
            fi
            if [[ -n "${OpenCASCADE_DIR:-}" ]]; then
                cmake_args+=("-DOpenCASCADE_DIR=$OpenCASCADE_DIR")
            fi
            if [[ "$ifc_enabled" == ON ]]; then
                cmake_args+=("-DPARAMETRIC_CAD_IFCOPENSHELL_ROOT=$ifc_root")
            fi
            echo "Configuring macOS build: $build_dir"
            cmake "${cmake_args[@]}"
        fi

        echo "Building: $build_dir"
        cmake --build "$build_dir" --parallel

        app="$build_dir/src/ParametricCAD.app"
        executable="$app/Contents/MacOS/ParametricCAD"
        if [[ ! -x "$executable" ]]; then
            echo "ParametricCAD executable was not found: $executable" >&2
            exit 1
        fi

        # Custom OCCT/IfcOpenShell installations may not be on the system
        # loader path. Homebrew installations normally need no override.
        runtime_paths=()
        for root in "${PARAMETRIC_CAD_OCCT_ROOT:-${OCCT_ROOT:-}}" "$ifc_root"; do
            if [[ -n "$root" && -d "$root/lib" ]]; then
                runtime_paths+=("$root/lib")
            fi
        done
        if [[ ${#runtime_paths[@]} -gt 0 ]]; then
            runtime_path=$(IFS=:; printf '%s' "${runtime_paths[*]}")
            export DYLD_LIBRARY_PATH="$runtime_path${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
        fi

        echo "Running:  $executable"
        exec "$executable" "$@"
        ;;
    *)
        build_dir="$repo_root/build"
        cache="$build_dir/CMakeCache.txt"

        if [[ ! -f "$cache" ]] \
            || ! grep -Fqx 'PARAMETRIC_CAD_ENABLE_IFC:BOOL=ON' "$cache"; then
            "$repo_root/configure.sh"
        fi

        echo "Building: $build_dir"
        cmake --build "$build_dir" -j
        echo "Running:  $build_dir/src/ParametricCAD"
        exec "$build_dir/src/ParametricCAD" "$@"
        ;;
esac
