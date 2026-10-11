#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir="$repo_root/build"

if [[ -n "${PARAMETRIC_CAD_IFCOPENSHELL_ROOT:-}" ]]; then
    ifc_root=$PARAMETRIC_CAD_IFCOPENSHELL_ROOT
elif [[ -d "$repo_root/../deps/ifcopenshell-v090" ]]; then
    ifc_root="$repo_root/../deps/ifcopenshell-v090"
else
    echo "IfcOpenShell 0.9.0 was not found." >&2
    echo "Set PARAMETRIC_CAD_IFCOPENSHELL_ROOT to its install directory." >&2
    exit 1
fi

ifc_root=$(cd -- "$ifc_root" && pwd)

echo "Configuring: $build_dir"
echo "IFC root:    $ifc_root"
cmake_args=(
    -S "$repo_root"
    -B "$build_dir"
    -G Ninja
    -DPARAMETRIC_CAD_BUILD_TESTS=ON
    -DPARAMETRIC_CAD_ENABLE_IFC=ON
    "-DPARAMETRIC_CAD_IFCOPENSHELL_ROOT=$ifc_root"
)
if [[ -n "${PARAMETRIC_CAD_IFCOPENSHELL_SOURCE_DIR:-}" ]]; then
    cmake_args+=(
        "-DPARAMETRIC_CAD_IFCOPENSHELL_SOURCE_DIR=$PARAMETRIC_CAD_IFCOPENSHELL_SOURCE_DIR")
fi
cmake "${cmake_args[@]}"
