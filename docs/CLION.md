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
