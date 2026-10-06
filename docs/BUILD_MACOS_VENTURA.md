# Building ParametricCAD on macOS Ventura

This guide builds ParametricCAD from a clean macOS Ventura installation using
the same repository as Ubuntu and Windows. Keep platform build directories
separate:

```text
ParametricCAD/  source checkout
build-macos/    macOS build
build/          Ubuntu build
build-win/      Windows build
```

Do not create platform source forks or copy third-party files into the
repository.

## Supported toolchain and architecture

The project is C++20 and uses Qt 6, Open CASCADE Technology (OCCT), and
minizip. The macOS viewer uses OCCT's existing `Cocoa_Window` path.

Both native architectures are supported by the source project:

- Intel: `x86_64`;
- Apple Silicon: `arm64`.

Build every dependency and ParametricCAD for the same architecture. A
universal build is possible only when Qt, OCCT, minizip, and (if enabled)
IfcOpenShell all contain both slices; it is not the default development path.

The CMake project does not hard-pin Qt or OCCT patch versions. The reproducible
baseline used by the project documentation is:

| Component | Baseline |
|---|---|
| OS | macOS Ventura 13.x, updated |
| Compiler | Apple Clang from Xcode Command Line Tools/Xcode |
| CMake | 3.24 or newer |
| Ninja | current Homebrew release |
| Qt | Qt 6.12.0 source build, or a Qt 6 release supporting Ventura |
| OCCT | 7.6.3 for the validated IFC combination |
| minizip | Homebrew `minizip` |
| IfcOpenShell | v0.7.1, commit `ed8cbff3d253691ac81450eaf16cad46bf6149e5` |

Check the selected Qt release's supported macOS baseline before using it on
Ventura. The ParametricCAD code only requires Qt 6 APIs and does not require a
Qt 6.12-specific API.

## 1. Apple tools and Homebrew

Install Xcode or the command-line tools:

```bash
xcode-select --install
xcode-select -p
clang --version
clang++ --version
uname -m
```

If full Xcode is installed, select it when necessary:

```bash
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license accept
```

Install Homebrew from <https://brew.sh/> if it is not present. Its normal
prefix is `/opt/homebrew` on Apple Silicon and `/usr/local` on Intel.

```bash
brew update
brew install git cmake ninja python@3.11 pkg-config minizip freetype
git --version
cmake --version
ninja --version
```

`pkg-config` is useful for diagnosing native dependencies but is not called by
ParametricCAD's CMake. The project does not directly use TBB, pthread, or a
TBB API; do not treat TBB as a ParametricCAD dependency. Only enable TBB in an
OCCT build if a separately audited OCCT configuration requires it.

## 2. Qt 6

### Recommended pinned source build

Download the official `qt-everywhere-src-6.12.0` archive and unpack it outside
the repository. The source recipe uses only the small set of Qt submodules
needed by this project (`qtbase`, `qtsvg`, and `qttools`); WebEngine and Qt
Quick are not required.

```bash
export QT_VERSION=6.12.0
export QT_SRC="$HOME/dev/qt-everywhere-src-$QT_VERSION"
export QT_BUILD="$HOME/dev/qt-build-$QT_VERSION-$(uname -m)"
export QT_ROOT="$HOME/Qt/$QT_VERSION-$(uname -m)"

mkdir -p "$HOME/dev" "$HOME/Qt" "$QT_BUILD"
# Download qt-everywhere-src-$QT_VERSION.tar.xz from the Qt archive, then:
tar -xf "$HOME/Downloads/qt-everywhere-src-$QT_VERSION.tar.xz" -C "$HOME/dev"
cd "$QT_BUILD"
"$QT_SRC/configure" \
  -prefix "$QT_ROOT" \
  -release \
  -opensource \
  -confirm-license \
  -nomake examples \
  -nomake tests \
  -submodules qtbase,qtsvg,qttools
cmake --build . --parallel
cmake --install .
```

The project calls these CMake components: `Core`, `Gui`, `Widgets`, `OpenGL`,
`OpenGLWidgets`, and `Concurrent`. Verify the installation:

```bash
export Qt6_ROOT="$QT_ROOT"
"$Qt6_ROOT/bin/qtpaths" --qt-version
"$Qt6_ROOT/bin/qmake" -v
test -f "$Qt6_ROOT/lib/cmake/Qt6/Qt6Config.cmake"
```

For a quick non-pinned setup, Homebrew Qt is acceptable:

```bash
brew install qt
export Qt6_ROOT="$(brew --prefix qt)"
```

### Architecture rule

For a native build, run the commands in a native shell and pass the matching
architecture explicitly if needed:

```bash
-DCMAKE_OSX_ARCHITECTURES=arm64
-DCMAKE_OSX_ARCHITECTURES=x86_64
```

Do not link an arm64 Qt with x86_64 OCCT.

## 3. OCCT and FreeType

ParametricCAD links `TKernel`, `TKMath`, `TKG2d`, `TKG3d`, `TKGeomBase`,
`TKBRep`, `TKGeomAlgo`, `TKTopAlgo`, `TKPrim`, `TKBO`, `TKFillet`, `TKOffset`,
`TKService`, `TKV3d`, and `TKOpenGl`.

For an IFC-enabled development environment, build OCCT 7.6.3 from source so
it matches the validated IfcOpenShell build. A Homebrew OCCT installation is
fine for a non-IFC experiment, but may have a different version or ABI.

```bash
export OCCT_SRC="$HOME/dev/occt"
export OCCT_BUILD="$HOME/dev/occt-build-$(uname -m)"
export OCCT_ROOT="$HOME/opt/occt-7.6.3-$(uname -m)"

git clone --branch V7_6_3 --depth 1 \
  https://github.com/Open-Cascade-SAS/OCCT.git "$OCCT_SRC"
cmake -S "$OCCT_SRC" -B "$OCCT_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$OCCT_ROOT" \
  -DUSE_TCL=OFF \
  -DUSE_TK=OFF \
  -DBUILD_MODULE_Draw=OFF \
  -DUSE_VTK=OFF \
  -DUSE_FREEIMAGE=OFF \
  -DUSE_FFMPEG=OFF \
  -DUSE_RAPIDJSON=OFF \
  -DUSE_DRACO=OFF \
  -DUSE_FREETYPE=ON \
  -DUSE_TBB=OFF
cmake --build "$OCCT_BUILD" --parallel
cmake --install "$OCCT_BUILD"
```

`USE_TCL=OFF`, `USE_TK=OFF`, and `BUILD_MODULE_Draw=OFF` avoid the unused
DRAW/Tcl/Tk dependency. `USE_TBB=OFF` reflects the current ParametricCAD
source, which does not link TBB. If OCCT cannot find FreeType, inspect its
configure summary and pass the release-specific `3RDPARTY_FREETYPE_*` cache
variable pointing at `$(brew --prefix freetype)`.

Check for the installed package file:

```bash
find "$OCCT_ROOT" -name OpenCASCADEConfig.cmake -print
```

Set `OpenCASCADE_DIR` to the directory containing that file.

## 4. IfcOpenShell C++ support (optional)

IFC is disabled by default. The validated native C++ baseline is IfcOpenShell
v0.7.1 at commit `ed8cbff3d253691ac81450eaf16cad46bf6149e5`, built against
the same OCCT headers, libraries, compiler, and architecture as ParametricCAD.
The Python package is not a ParametricCAD runtime dependency.

Install common source-build dependencies:

```bash
brew install boost zlib
```

Build the C++ core outside the project. The v0.7.1 checkout uses
`BUILD_IFCPYTHON`, rather than the option names used by newer IfcOpenShell
documentation. Build only the C++ geometry core:

```bash
export IFCOS_SRC="$HOME/dev/ifcopenshell"
export IFCOS_BUILD="$HOME/dev/ifcopenshell-build-$(uname -m)"
export IFCOS_ROOT="$HOME/opt/ifcopenshell-v0.7.1-$(uname -m)"

git clone --branch v0.7.1 --recursive \
  https://github.com/IfcOpenShell/IfcOpenShell.git "$IFCOS_SRC"
git -C "$IFCOS_SRC" checkout ed8cbff3d253691ac81450eaf16cad46bf6149e5
cmake -S "$IFCOS_SRC" -B "$IFCOS_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$IFCOS_ROOT" \
  -DCMAKE_PREFIX_PATH="$OCCT_ROOT;$(brew --prefix);$(brew --prefix boost)" \
  -DMINIMAL_BUILD=ON \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_IFCGEOM=ON \
  -DBUILD_IFCPYTHON=OFF \
  -DBUILD_CONVERT=OFF \
  -DBUILD_GEOMSERVER=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_DOCUMENTATION=OFF
cmake --build "$IFCOS_BUILD" --parallel
cmake --install "$IFCOS_BUILD"
```

The install must contain `ifcgeom_schema_agnostic/IfcGeomIterator.h`,
`IfcGeom`, `IfcParse`, `IfcGeom_ifc2x3`, and `IfcGeom_ifc4`. Do not mix a
prebuilt IfcOpenShell binary with another OCCT version; unresolved symbols
usually indicate exactly that ABI mismatch.

## 5. Build ParametricCAD

```bash
mkdir -p "$HOME/projects3"
git clone https://github.com/coltrack-dev/ParametricCAD.git "$HOME/projects3/ParametricCAD"
cd "$HOME/projects3/ParametricCAD"

export Qt6_ROOT="$QT_ROOT"
export OpenCASCADE_DIR="$(dirname "$(find "$OCCT_ROOT" -name OpenCASCADEConfig.cmake -print -quit)")"
export CMAKE_PREFIX_PATH="$QT_ROOT;$OCCT_ROOT;$(brew --prefix minizip)"
```

### Release without IFC

```bash
cmake -S . -B build-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH" \
  -DOpenCASCADE_DIR="$OpenCASCADE_DIR" \
  -DPARAMETRIC_CAD_BUILD_TESTS=ON \
  -DPARAMETRIC_CAD_ENABLE_IFC=OFF
cmake --build build-macos --parallel
ctest --test-dir build-macos --output-on-failure
```

### Release with IFC

```bash
cmake -S . -B build-macos-ifc -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH;$IFCOS_ROOT" \
  -DOpenCASCADE_DIR="$OpenCASCADE_DIR" \
  -DPARAMETRIC_CAD_BUILD_TESTS=ON \
  -DPARAMETRIC_CAD_ENABLE_IFC=ON \
  -DPARAMETRIC_CAD_IFCOPENSHELL_ROOT="$IFCOS_ROOT"
cmake --build build-macos-ifc --parallel
ctest --test-dir build-macos-ifc --output-on-failure
```

### Debug

Use a separate cache:

```bash
cmake -S . -B build-macos-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH" \
  -DOpenCASCADE_DIR="$OpenCASCADE_DIR" \
  -DPARAMETRIC_CAD_BUILD_TESTS=ON
cmake --build build-macos-debug --parallel
ctest --test-dir build-macos-debug --output-on-failure
```

## 6. Run, bundle, and runtime libraries

`src/CMakeLists.txt` sets `MACOSX_BUNDLE` on Apple platforms:

```bash
open build-macos/src/ParametricCAD.app
build-macos/src/ParametricCAD.app/Contents/MacOS/ParametricCAD
```

Deploy Qt libraries and plugins with:

```bash
"$Qt6_ROOT/bin/macdeployqt" build-macos/src/ParametricCAD.app -always-overwrite
```

OCCT, minizip, and IfcOpenShell are not discovered by `macdeployqt`. For
development, diagnose missing libraries with:

```bash
otool -L build-macos/src/ParametricCAD.app/Contents/MacOS/ParametricCAD
export DYLD_LIBRARY_PATH="$OCCT_ROOT/lib:$IFCOS_ROOT/lib:${DYLD_LIBRARY_PATH:-}"
```

For distribution, use proper `@rpath`/install-name handling and code signing;
do not make `DYLD_LIBRARY_PATH` the deployment mechanism.

## 7. Cleaning and troubleshooting

```bash
rm -rf build-macos build-macos-debug build-macos-ifc
```

Use explicit build directories when changing architecture, Qt, OCCT, or
IfcOpenShell. Common failures:

- **Qt not found:** check `Qt6_ROOT` and `lib/cmake/Qt6/Qt6Config.cmake`.
- **OCCT not found:** `OpenCASCADE_DIR` must contain `OpenCASCADEConfig.cmake`.
- **arm64/x86_64 mismatch:** compare `uname -m`, `file` and `lipo -info` on all dylibs.
- **minizip not found:** add `$(brew --prefix minizip)` to `CMAKE_PREFIX_PATH`; expected headers are `minizip/unzip.h` and `minizip/zip.h`.
- **IfcOpenShell unresolved symbols:** rebuild it against the same OCCT version and architecture.
- **OpenGL/Qt viewer failure:** run headless CTest first, then inspect the app bundle and Cocoa/OCCT dylibs.

## Verification status

The repository CMake project, preset logic, and tests are verified on Ubuntu.
The commands in this guide require a real macOS Ventura host; native macOS
configure, linking, bundle deployment, and GUI smoke testing still need to be
performed there.
