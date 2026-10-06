# Building ParametricCAD on Windows 11

This guide builds ParametricCAD natively with MSVC from the same repository
used on Ubuntu and macOS. Keep independent build directories:

```text
ParametricCAD/  source checkout
build-win/      Windows build
build/          Ubuntu build
build-macos/    macOS build
```

Do not create Windows source forks, copy dependencies into the repository, or
add user-specific paths to CMake files.

## Supported toolchain

- Windows 11 x64;
- Visual Studio 2026 Community or compatible MSVC x64 tools;
- Windows 11 SDK;
- CMake 4.3.1 or newer;
- Ninja 1.13.2 or newer;
- Qt 6.12.0 built for MSVC x64;
- OCCT 7.9 built for MSVC x64;
- vcpkg with `minizip:x64-windows`;
- optional IfcOpenShell v0.7.1 built with the same MSVC/OCCT ABI.

The project requires C++20. The Windows viewer already uses OCCT's native
`WNT_Window` path. Linux X11/XCB and macOS Cocoa paths are not used on Windows.

## 1. Visual Studio and command shell

Install Visual Studio 2026 Community from the official Visual Studio download
site. Select these Installer components:

- **Desktop development with C++**;
- MSVC x64/x86 build tools;
- Windows 11 SDK;
- C++ CMake tools for Windows;
- Git integration if Git is not installed separately.

Use an **x64 Native Tools Command Prompt for VS** or **Developer PowerShell for
VS**. A normal PowerShell does not necessarily define `cl.exe`. Alternatively,
run the Visual Studio `vcvars64.bat` script before using CMake. Do not use an
x86 Developer Prompt for this project.

Verify the active environment:

```powershell
cl
where.exe cl
cmake --version
ninja --version
git --version
```

The compiler banner and paths must correspond to an x64 MSVC environment.
PowerShell execution-policy changes are not part of the normal setup; use them
only as local troubleshooting when a trusted script is blocked.

## 2. vcpkg and minizip

Clone vcpkg outside the source tree and bootstrap it:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
& C:\dev\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = 'C:\dev\vcpkg'
& "$env:VCPKG_ROOT\vcpkg.exe" install minizip:x64-windows
```

`ProjectArchive.cpp` includes `minizip/unzip.h` and `minizip/zip.h`. The root
CMake file first looks for the CONFIG package and links the imported target
`unofficial::minizip::minizip` (or its static variant). The target propagates
headers and transitive libraries to both `ParametricCAD` and
`model_test_support`, which also compiles `ProjectArchive.cpp`. No global
include directory points into vcpkg.

`VCPKG_MANIFEST_MODE=OFF` is intentional: this repository has no vcpkg
manifest, and OCCT/IfcOpenShell source builds must not trigger an unrelated
manifest dependency graph.

## 3. Qt 6.12 from source

Download the official `qt-everywhere-src-6.12.0` source archive and unpack it
outside the repository. Build from an x64 Developer PowerShell:

```powershell
$env:QT_VERSION = '6.12.0'
$qtSrc = "C:\dev\qt\qt-everywhere-src-$env:QT_VERSION"
$qtBuild = "C:\dev\qt-build\$env:QT_VERSION-msvc-x64"
$qtRoot = "C:\Qt\$env:QT_VERSION-msvc-x64"

New-Item -ItemType Directory -Force C:\dev\qt-build, C:\Qt | Out-Null
cmake -E make_directory $qtBuild
Set-Location $qtBuild
# Download and unpack the source archive so that $qtSrc exists first.
& "$qtSrc\configure.bat" `
  -prefix $qtRoot `
  -release `
  -opensource `
  -confirm-license `
  -nomake examples `
  -nomake tests `
  -submodules qtbase,qtsvg,qttools
cmake --build . --parallel
cmake --install .
```

ParametricCAD requires Qt components `Core`, `Gui`, `Widgets`, `OpenGL`,
`OpenGLWidgets`, and `Concurrent`. The source recipe does not build WebEngine,
Qt Quick, or other large modules. Verify the install:

```powershell
$env:Qt6_ROOT = $qtRoot
& "$env:Qt6_ROOT\bin\qtpaths.exe" --qt-version
& "$env:Qt6_ROOT\bin\qmake.exe" -v
Test-Path "$env:Qt6_ROOT\lib\cmake\Qt6\Qt6Config.cmake"
```

If a different Qt 6 patch release is selected, record it and keep it x64/MSVC
compatible. Do not mix x86 Qt libraries with an x64 build.

## 4. OCCT 7.9 and FreeType

Build OCCT from source in a separate tree. The following switches match the
current ParametricCAD needs and avoid unused DRAW/Tcl/Tk and optional stacks:

```powershell
git clone --branch V7_9_0 --depth 1 `
  https://github.com/Open-Cascade-SAS/OCCT.git C:\dev\occt
$env:OCCT_ROOT = 'C:\OCCT\7.9'

cmake -S C:\dev\occt -B C:\dev\occt-build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_INSTALL_PREFIX="$env:OCCT_ROOT" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_MANIFEST_MODE=OFF `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DUSE_TCL=OFF `
  -DUSE_TK=OFF `
  -DBUILD_MODULE_Draw=OFF `
  -DUSE_VTK=OFF `
  -DUSE_FREEIMAGE=OFF `
  -DUSE_FFMPEG=OFF `
  -DUSE_RAPIDJSON=OFF `
  -DUSE_DRACO=OFF `
  -DUSE_FREETYPE=ON `
  -DUSE_TBB=OFF
cmake --build C:\dev\occt-build --parallel
cmake --install C:\dev\occt-build
```

If OCCT cannot find FreeType, install the x64 vcpkg package before configuring:

```powershell
& "$env:VCPKG_ROOT\vcpkg.exe" install freetype:x64-windows
```

`USE_TBB=OFF` reflects the current CMake/source audit: ParametricCAD does not
link TBB or call its API. If a future OCCT setup enables TBB internally, audit
and rebuild the complete native dependency set together.

Check the package location:

```powershell
Get-ChildItem "$env:OCCT_ROOT" -Recurse -Filter OpenCASCADEConfig.cmake
```

`OpenCASCADE_DIR` must point to the directory containing that file, commonly
`C:\OCCT\7.9\cmake` for this layout.

## 5. IfcOpenShell C++ support (optional)

IFC is disabled by default. The validated native baseline is IfcOpenShell
v0.7.1 at commit `ed8cbff3d253691ac81450eaf16cad46bf6149e5`, built with the
same MSVC x64 compiler and OCCT headers/libraries as ParametricCAD. A Python
wheel is not a substitute for the C++ libraries and Python is not a runtime
dependency of ParametricCAD.

IfcOpenShell source builds may need Boost, Eigen, GMP, MPFR, CGAL, zlib, and
other optional build dependencies. Install them for the same x64 triplet or
use the dependency helper belonging to the pinned IfcOpenShell checkout. Keep
all prefixes and the compiler architecture consistent. The v0.7.1 checkout
uses `BUILD_IFCPYTHON`, not the `WITH_PYTHON`/`WITH_SWIG` names used by newer
documentation.

```powershell
git clone --branch v0.7.1 --recursive `
  https://github.com/IfcOpenShell/IfcOpenShell.git C:\dev\ifcopenshell
Set-Location C:\dev\ifcopenshell
git checkout ed8cbff3d253691ac81450eaf16cad46bf6149e5

$env:IFCOPENSHELL_ROOT = 'C:\dev\ifcopenshell-install'
cmake -S C:\dev\ifcopenshell -B C:\dev\ifcopenshell-build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_INSTALL_PREFIX="$env:IFCOPENSHELL_ROOT" `
  -DCMAKE_PREFIX_PATH="$env:OCCT_ROOT;$env:VCPKG_ROOT\installed\x64-windows" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_MANIFEST_MODE=OFF `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DMINIMAL_BUILD=ON `
  -DBUILD_SHARED_LIBS=ON `
  -DBUILD_IFCGEOM=ON `
  -DBUILD_IFCPYTHON=OFF `
  -DBUILD_CONVERT=OFF `
  -DBUILD_GEOMSERVER=OFF `
  -DBUILD_EXAMPLES=OFF `
  -DBUILD_DOCUMENTATION=OFF
cmake --build C:\dev\ifcopenshell-build --parallel
cmake --install C:\dev\ifcopenshell-build
```

The v0.7.1 install must provide `ifcgeom_schema_agnostic/IfcGeomIterator.h`,
`IfcGeom`, `IfcParse`, `IfcGeom_ifc2x3`, and `IfcGeom_ifc4`. If symbols fail to
resolve, verify the exact IfcOpenShell commit, OCCT version, MSVC runtime,
architecture, and DLL search path before changing source code.

## 6. Clone and configure ParametricCAD

```powershell
New-Item -ItemType Directory -Force C:\dev\projects3 | Out-Null
git clone https://github.com/coltrack-dev/ParametricCAD.git C:\dev\projects3\ParametricCAD
Set-Location C:\dev\projects3\ParametricCAD

$env:Qt6_ROOT = 'C:\Qt\6.12.0-msvc-x64'
$env:OCCT_ROOT = 'C:\OCCT\7.9'
$env:OpenCASCADE_DIR = "$env:OCCT_ROOT\cmake"
$env:VCPKG_ROOT = 'C:\dev\vcpkg'
```

### Release, no IFC

The repository preset has no absolute machine paths and uses these variables:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --test-dir build-win --output-on-failure
```

### Release with IFC

The preset deliberately keeps IFC off. Enable it only after installing a
compatible native IfcOpenShell build:

```powershell
$env:IFCOPENSHELL_ROOT = 'C:\dev\ifcopenshell-install'
cmake -S . -B build-win-ifc -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$env:Qt6_ROOT;$env:OCCT_ROOT;$env:IFCOPENSHELL_ROOT" `
  -DOpenCASCADE_DIR="$env:OpenCASCADE_DIR" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_MANIFEST_MODE=OFF `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DPARAMETRIC_CAD_BUILD_TESTS=ON `
  -DPARAMETRIC_CAD_ENABLE_IFC=ON `
  -DPARAMETRIC_CAD_IFCOPENSHELL_ROOT="$env:IFCOPENSHELL_ROOT"
cmake --build build-win-ifc --parallel
ctest --test-dir build-win-ifc --output-on-failure
```

### Debug

```powershell
cmake -S . -B build-win-debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH="$env:Qt6_ROOT;$env:OCCT_ROOT" `
  -DOpenCASCADE_DIR="$env:OpenCASCADE_DIR" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_MANIFEST_MODE=OFF `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DPARAMETRIC_CAD_BUILD_TESTS=ON
cmake --build build-win-debug --parallel
ctest --test-dir build-win-debug --output-on-failure
```

## 7. Run and deploy

```powershell
.\build-win\src\ParametricCAD.exe
```

The runtime directory must contain matching x64 libraries:

- Qt6 `Core`, `Gui`, `Widgets`, `OpenGL`, `OpenGLWidgets`, `Concurrent`;
- `platforms\qwindows.dll`;
- OCCT `TKernel`, `TKMath`, `TKBRep`, `TKTopAlgo`, `TKV3d`, `TKOpenGl`, and
  the modeling toolkit DLLs;
- minizip and zlib from vcpkg;
- FreeType and other OCCT transitive DLLs when applicable;
- IfcOpenShell DLLs for an IFC-enabled build;
- the matching MSVC redistributable.

Use Qt deployment for Qt files and plugins:

```powershell
& "$env:Qt6_ROOT\bin\windeployqt.exe" `
  --release --compiler-runtime .\build-win\src\ParametricCAD.exe
```

`windeployqt` does not deploy arbitrary OCCT, minizip, or IfcOpenShell DLLs.
Place matching third-party DLLs beside the executable or on `PATH`; never
commit generated DLLs to the source tree. Use `dumpbin /DEPENDENTS` from the
Developer shell to diagnose missing dependencies.

## 8. Optional SOCKS5 proxy

For a shell that needs a local proxy during vcpkg/source downloads only:

```cmd
set ALL_PROXY=socks5h://127.0.0.1:4712
set HTTP_PROXY=socks5h://127.0.0.1:4712
set HTTPS_PROXY=socks5h://127.0.0.1:4712
```

The proxy is optional and is not part of the required build configuration.

## 9. Cleaning and troubleshooting

```powershell
Remove-Item -Recurse -Force build-win
```

Use a new build directory after changing compiler architecture, Qt, OCCT,
vcpkg triplet, or IfcOpenShell. Common failures:

- **`minizip/unzip.h` missing:** install `minizip:x64-windows`, use the vcpkg
  toolchain, delete the build cache, and reconfigure. Do not add a global
  vcpkg include directory.
- **`tcl.h`/Tcl/Tk errors in OCCT:** use `USE_TCL=OFF`, `USE_TK=OFF`, and
  `BUILD_MODULE_Draw=OFF`.
- **FreeType not found:** install `freetype:x64-windows` and inspect OCCT's
  `3RDPARTY_FREETYPE_*` cache values.
- **gperf/source helper failure:** use the x64 Developer shell and clean
  Debug/Release build directories.
- **vcpkg download failure:** set the optional proxy variables and retry.
- **x86 selected:** run `where.exe cl`, reopen an x64 Native Tools shell, and
  rebuild every dependency for x64.
- **Qt/OCCT not found:** `Qt6_ROOT` is the Qt prefix; `OpenCASCADE_DIR` is the
  directory containing `OpenCASCADEConfig.cmake`.
- **IfcOpenShell unresolved symbols/DLLs:** align pinned commit, OCCT, MSVC
  runtime, architecture, and runtime search paths.

## Verification status

The Windows preset and minizip target propagation are statically checked from
Ubuntu; the Ubuntu build and tests pass. Native Windows configure/build,
runtime DLL loading, and GUI smoke testing require a real Windows 11 x64 host
and have not been claimed as executed here.
