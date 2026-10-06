# Windows 11 build

ParametricCAD uses the same source tree on Ubuntu, macOS, and Windows. Keep a
separate build directory for Windows; do not copy headers, libraries, or DLLs
into the repository.

## Requirements

- Windows 11 x64;
- Visual Studio 2026 with the MSVC x64 C++ workload;
- CMake 4.3.1 or newer;
- Ninja 1.13.2 or newer;
- Qt 6.12.0 with Core, Gui, Widgets, OpenGL, OpenGLWidgets, and Concurrent;
- Open CASCADE Technology 7.9 built for MSVC x64;
- vcpkg with the `minizip` port installed for `x64-windows`.

The current CMake files do not require a Windows-specific source tree. OCCT's
native window adapter is selected in `CadViewer.cpp` through the existing
`WNT_Window` branch; Linux uses `Xw_Window` and macOS uses `Cocoa_Window`.

## Environment setup

Run these commands from a Visual Studio x64 Developer PowerShell, or from a
shell where `cl.exe` is already available. The values below are examples for
the documented installation layout; they are environment variables, not paths
embedded in the project:

```powershell
$env:Qt6_ROOT = 'C:\Qt\6.12-custom'
$env:OpenCASCADE_DIR = 'C:\OCCT\7.9\cmake'
$env:VCPKG_ROOT = 'C:\dev\vcpkg'

& "$env:VCPKG_ROOT\vcpkg.exe" install minizip:x64-windows
```

The vcpkg toolchain is used only for dependency discovery. The project links
the imported `unofficial::minizip::minizip` target, so its include directories
and transitive libraries propagate to both `ParametricCAD` and the test support
target that compiles `ProjectArchive.cpp`.

## Configure and build

From the repository root, the recommended preset is:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
```

Equivalent explicit commands are:

```powershell
cmake -S . -B build-win -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$env:Qt6_ROOT;$env:OpenCASCADE_DIR" `
  -DOpenCASCADE_DIR="$env:OpenCASCADE_DIR" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_MANIFEST_MODE=OFF `
  -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build build-win --parallel
```

Run tests with:

```powershell
ctest --test-dir build-win --output-on-failure
```

Run the application with:

```powershell
.\build-win\src\ParametricCAD.exe
```

If a multi-configuration generator is used instead of Ninja, use the
corresponding configuration subdirectory, for example
`build-win\src\Release\ParametricCAD.exe`.

## Runtime DLLs

The executable needs the matching runtime libraries from the same x64 builds:

- Qt 6 DLLs: `Qt6Core`, `Qt6Gui`, `Qt6Widgets`, `Qt6OpenGL`,
  `Qt6OpenGLWidgets`, and `Qt6Concurrent`;
- OCCT DLLs used by the application, including `TKernel`, `TKMath`, `TKBRep`,
  `TKTopAlgo`, `TKV3d`, `TKOpenGl`, and the modeling/toolkit DLLs;
- vcpkg minizip DLL and its zlib dependency;
- the MSVC runtime, normally provided by the Visual C++ Redistributable.

For a deployable test directory, use Qt's `windeployqt` against the built
executable and place the matching OCCT/vcpkg DLLs beside it or on `PATH`.
Do not commit those generated runtime files.

## Troubleshooting

### `minizip/unzip.h` cannot be found

Ensure `VCPKG_ROOT` points to the active vcpkg checkout and that
`minizip:x64-windows` is installed. Delete `build-win` or reconfigure it after
changing the toolchain. CMake should report the imported target
`unofficial::minizip::minizip`; it must not require a manually added include
directory.

### Qt or OCCT is not found

Check that `Qt6_ROOT` points to the Qt installation root and
`OpenCASCADE_DIR` points to the directory containing the OCCT CMake package
files. Keep the paths in the shell environment or pass them on the configure
command; do not add user-specific paths to `CMakeLists.txt`.

### Linker errors for OCCT libraries

Confirm that Qt, OCCT, and the compiler are all x64 and that `OpenCASCADE_DIR`
belongs to the same OCCT installation as the headers and DLLs. A stale build
directory can retain Unix or another architecture's cache values.

### Application starts but cannot create the viewer

Check that the OCCT visualization DLLs, Qt OpenGL DLLs, and the graphics driver
are available. The Windows viewer uses the native `WNT_Window` path; no X11 or
XCB setup is required.

### IFC support

The Windows preset keeps `PARAMETRIC_CAD_ENABLE_IFC=OFF`. IFC can be enabled
only after providing a Windows-compatible IfcOpenShell build compiled against
the same OCCT runtime; it is not silently enabled by this preset.
