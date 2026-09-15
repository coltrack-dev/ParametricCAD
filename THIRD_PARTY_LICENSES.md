# Third-Party Licenses

ParametricCAD uses third-party open-source libraries and components.

The licenses listed below apply to those third-party components only.
The ParametricCAD source code itself is licensed separately under the MIT License.

## Qt 6

ParametricCAD uses Qt 6 for the desktop user interface, window management, input handling, and OpenGL integration.

Qt is available under multiple licensing options, including commercial and open-source licenses.

The open-source Qt components used by this project are distributed under licenses including the GNU Lesser General Public License version 3 (LGPL-3.0), depending on the specific Qt module.

Project website:

https://www.qt.io/

Licensing information:

https://www.qt.io/licensing/

Qt source code:

https://code.qt.io/

When distributing binaries that use Qt under the LGPL, the applicable LGPL requirements must be respected.

## Open CASCADE Technology

ParametricCAD uses Open CASCADE Technology (OCCT) for:

* B-Rep geometry
* topology
* solid modeling
* Boolean operations
* visualization
* interactive selection
* 3D view management

Open CASCADE Technology is distributed under the GNU Lesser General Public License version 2.1 with an additional Open CASCADE exception.

Project website:

https://www.opencascade.com/

Source code:

https://github.com/Open-Cascade-SAS/OCCT

License:

https://github.com/Open-Cascade-SAS/OCCT/blob/master/LICENSE_LGPL_21.txt

Additional exception:

https://github.com/Open-Cascade-SAS/OCCT/blob/master/OCCT_LGPL_EXCEPTION.txt

The OCCT license and exception remain applicable to Open CASCADE Technology independently of the ParametricCAD license.

## OpenGL

ParametricCAD uses OpenGL indirectly through Qt and Open CASCADE Technology for 3D rendering.

OpenGL is an open graphics API specification maintained by the Khronos Group.

Website:

https://www.khronos.org/opengl/

The actual OpenGL implementation used at runtime is provided by the operating system, graphics driver, or Mesa implementation and may have its own license.

## CMake

CMake is used as the build system generator for ParametricCAD.

Project website:

https://cmake.org/

Source code:

https://github.com/Kitware/CMake

CMake is distributed under the BSD 3-Clause License.

## Ninja

Ninja may be used as the build backend when building ParametricCAD.

Project website:

https://ninja-build.org/

Source code:

https://github.com/ninja-build/ninja

Ninja is distributed under the Apache License 2.0.

## Compiler and System Libraries

ParametricCAD may be compiled using GCC, Clang, or another compatible C++ compiler.

Depending on the target platform and Linux distribution, the resulting application may dynamically link against additional system libraries, including:

* GNU C Library
* libstdc++
* pthread
* X11
* GLX
* OpenGL
* FreeType
* Fontconfig
* FreeImage
* Intel oneTBB

These components are provided by the operating system or development environment and retain their respective licenses.

## License Responsibility

This file is provided for informational purposes.

Each third-party component remains subject to its own license terms. Before distributing ParametricCAD binaries, packaging the application, or redistributing third-party libraries, the applicable license requirements should be reviewed for the exact versions and modules being distributed.
