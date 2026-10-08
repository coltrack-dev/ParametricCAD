#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir="$repo_root/build"
cache="$build_dir/CMakeCache.txt"

if [[ ! -f "$cache" ]] \
    || ! grep -Fqx 'PARAMETRIC_CAD_ENABLE_IFC:BOOL=ON' "$cache"; then
    "$repo_root/configure.sh"
fi

echo "Building: $build_dir"
cmake --build "$build_dir" -j
echo "Running:  $build_dir/src/ParametricCAD"
exec "$build_dir/src/ParametricCAD"
