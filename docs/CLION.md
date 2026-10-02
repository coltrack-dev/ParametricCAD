# CLion setup

## Linux

Рекомендуемый toolchain:

- GCC или Clang
- CMake
- Ninja
- GDB

В CLion:

```text
Settings
  Build, Execution, Deployment
    Toolchains
      C Compiler   = gcc
      C++ Compiler = g++
      Debugger     = gdb
      CMake        = bundled/system
```

CMake profile:

```text
Build type: Debug
Generator: Ninja
```

Если Qt/OCCT не находятся автоматически, добавьте CMake options:

```text
-DCMAKE_PREFIX_PATH=/opt/Qt/6.x/gcc_64;/opt/occt
```

## Debugging

Поставьте breakpoint в:

```text
src/operations/BoxFeature.cpp
BoxFeature::recompute()
```

Запустите приложение и нажмите `Box`.

Это позволит пройти из UI до вызова геометрического ядра.

## macOS Intel

Use the Apple Clang toolchain and Ninja. Install dependencies with Homebrew:

```bash
brew install cmake ninja qt opencascade
```

Recommended CMake options:

```text
-DCMAKE_BUILD_TYPE=Debug
-DCMAKE_OSX_ARCHITECTURES=x86_64
-DCMAKE_PREFIX_PATH=$(brew --prefix qt);$(brew --prefix opencascade)
```

The viewer uses OCCT `Cocoa_Window` around Qt's native `NSView`; no X11 or
XQuartz configuration is required.
