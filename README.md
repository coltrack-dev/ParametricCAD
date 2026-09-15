# ParametricCAD

pet-project CAD-системы на **C++20 + Qt 6 + Open CASCADE (OCCT)**,
подготовленный для разработки в **CLion**.

## Что уже реализовано

MVP-0:

- CMake-проект для CLion;
- Qt 6 desktop application;
- интеграция с Open CASCADE;
- 3D viewer;
- координатный trihedron;
- создание Box / Cylinder;
- Fit All;
- удаление объектов сцены;
- простая модель `Document`;
- заготовка feature-архитектуры;
- unit-тест базовой геометрии без внешнего test framework.

Следующие этапы:

1. selection граней/рёбер;
2. Sketch;
3. Extrude/Revolve;
4. Feature tree;
5. dependency graph + recompute;
6. Boolean operations;
7. Fillet/Chamfer;
8. STEP import/export;
9. undo/redo;
10. persistence.

## Зависимости

Нужны:

- CMake >= 3.24
- C++20 compiler
- Qt 6
- Open CASCADE Technology (OCCT)
- CLion

### Ubuntu/Debian

Названия пакетов OCCT зависят от версии дистрибутива. Qt обычно:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build qt6-base-dev libgl1-mesa-dev
```

Для OCCT найдите доступные пакеты:

```bash
apt search opencascade
apt search occt
```

На системах, где доступны Debian-пакеты OCCT, потребуются development-пакеты
модулей Foundation/Modeling/DataExchange/Visualization.

Если OCCT установлен вручную, укажите CLion путь к CMake config, например:

```text
-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64;/path/to/occt/install
```

или:

```text
-DOpenCASCADE_DIR=/path/to/occt/lib/cmake/opencascade
```

## Открытие в CLion

1. File -> Open.
2. Выберите каталог `ParametricCAD`.
3. CLion обнаружит `CMakeLists.txt`.
4. Settings -> Build, Execution, Deployment -> Toolchains.
5. Выберите GCC или Clang.
6. В CMake profile рекомендуется Ninja.
7. Reload CMake Project.
8. Запустите target `ParametricCAD`.

## Архитектура

```text
src/
├── app/
│   └── main.cpp
├── geometry/
│   ├── Point3D.h
│   └── Vector3D.h
├── model/
│   ├── Document.*
│   └── Feature.*
├── operations/
│   ├── BoxFeature.*
│   └── CylinderFeature.*
└── viewer/
    ├── CadViewer.*
    └── MainWindow.*
```

Идея архитектуры:

```text
UI
 │
 ▼
Document ───────► Feature graph
                    │
                    ▼
              OCCT operations
                    │
                    ▼
                TopoDS_Shape
                    │
                    ▼
                 Viewer
```

`Feature` — не QWidget и не viewer object. Это элемент CAD-модели.
Он хранит параметры и создаёт `TopoDS_Shape`.

## Первая учебная задача

Поставьте breakpoint в:

```cpp
BoxFeature::recompute()
```

и проследите цепочку:

```text
MainWindow::createBox()
        ↓
Document::addFeature()
        ↓
BoxFeature::recompute()
        ↓
BRepPrimAPI_MakeBox
        ↓
TopoDS_Shape
        ↓
CadViewer::display()
```

Это хорошая отправная точка для понимания того, как UI, document model,
геометрическое ядро и visualization разделяются в CAD.

## Roadmap

### Milestone 1 — Viewer
- [x] Qt window
- [x] OCCT viewer
- [x] Box
- [x] Cylinder
- [ ] selection
- [ ] highlighted face/edge
- [ ] orthographic/perspective camera

### Milestone 2 — Modeling
- [ ] primitive parameters dialog
- [ ] transform feature
- [ ] Boolean Fuse
- [ ] Boolean Cut
- [ ] Boolean Common
- [ ] Fillet
- [ ] Chamfer

### Milestone 3 — Parametric model
- [ ] Feature ID
- [ ] feature dependencies
- [ ] dirty flag
- [ ] topological sort
- [ ] incremental recompute
- [ ] failed feature state

### Milestone 4 — Sketch
- [ ] 2D points
- [ ] lines/arcs/circles
- [ ] dimensions
- [ ] constraints
- [ ] profile validation
- [ ] Extrude from Sketch

### Milestone 5 — Exchange
- [ ] STEP import
- [ ] STEP export
- [ ] STL export
- [ ] project serialization

## License

Choose a license before publishing the repository. For a learning project,
MIT or Apache-2.0 are common choices, but check compatibility requirements
of dependencies and your intended distribution model.
