# ParametricCAD

ParametricCAD is an experimental desktop CAD application written in C++ using Qt 6 and Open CASCADE Technology (OCCT).

The project is intended as a public portfolio project for exploring CAD architecture, B-Rep geometry, interactive 3D modeling, parametric operations, and SketchUp-like direct modeling workflows.

## Current Features

### Primitive geometry

- Box creation
- Cylinder creation
- Open CASCADE B-Rep geometry
- Interactive 3D viewport

### Selection

The viewer supports selection of different topology levels:

- Object
- Edge
- Face

Keyboard shortcuts:

| Key | Action |
|---|---|
| `1` | Object selection |
| `2` | Edge selection |
| `3` | Face selection |
| `Esc` | Clear selection / cancel current tool |

Geometry under the mouse cursor is highlighted before selection.

### Push / Pull

A SketchUp-inspired Push/Pull tool is available for planar faces.

Workflow:

1. Press `P` or select **Push/Pull** from the toolbar.
2. Move the cursor over a planar face.
3. Click the face.
4. Move the mouse to define the extrusion distance.
5. Click again to confirm.
6. Press `Esc` to cancel.

Push/Pull uses Open CASCADE operations internally:

- `BRepPrimAPI_MakePrism`
- `BRepAlgoAPI_Fuse`
- `BRepAlgoAPI_Cut`

Positive extrusion extends the solid.

Negative extrusion can create a cut into the existing solid.

> Push/Pull is currently an experimental direct-modeling operation. Integration with the parametric feature history is planned.

## X-Ray / Select Through

ParametricCAD includes an experimental X-Ray mode for working with geometry hidden behind visible faces.

Press:

```text
X
```

or use the **X-Ray** button on the toolbar.

When X-Ray is enabled:

- solids become semi-transparent;
- hidden geometry remains visible;
- the toolbar clearly shows that X-Ray mode is active;
- `Alt + Left Click` can cycle through detected faces under the cursor.

Example:

```text
P
→ X-Ray
→ move cursor over the model
→ Alt + Left Click until the required rear face is highlighted
→ move mouse
→ Left Click to confirm Push/Pull
```

X-Ray can be disabled by pressing `X` again.

## Toolbar

The viewport contains a modeling toolbar with the main interaction modes:

```text
Object | Edge | Face | Push/Pull | X-Ray | Fit
```

The toolbar remains synchronized with keyboard shortcuts.

The currently active tool or mode is visually highlighted.

When X-Ray is enabled, an additional visible status indicator is displayed.

## View Navigation

Mouse controls are inspired by common 3D modeling applications.

| Input | Action |
|---|---|
| Middle Mouse + drag | Orbit |
| Shift + Middle Mouse + drag | Pan |
| Mouse Wheel | Zoom |
| `F` | Fit model to view |

## Technology Stack

- C++17/20
- Qt 6
- Open CASCADE Technology
- CMake
- Ninja / Make
- OpenGL

The project currently targets Linux and is being developed and tested primarily on Linux.

## Project Structure

```text
ParametricCAD/
├── CMakeLists.txt
├── src/
│   ├── app/
│   │   └── main.cpp
│   ├── model/
│   │   ├── Document.cpp
│   │   └── Feature.cpp
│   ├── operations/
│   │   ├── BoxFeature.cpp
│   │   └── CylinderFeature.cpp
│   └── viewer/
│       ├── CadViewer.cpp
│       ├── CadViewer.h
│       ├── MainWindow.cpp
│       └── MainWindow.h
└── README.md
```

## Build

### Requirements

Install:

- C++ compiler with C++17 or newer support
- CMake
- Qt 6
- Open CASCADE Technology

Example packages on Debian/Ubuntu-based systems may include:

```bash
sudo apt install \
    build-essential \
    cmake \
    ninja-build \
    qt6-base-dev \
    libocct-foundation-dev \
    libocct-modeling-data-dev \
    libocct-modeling-algorithms-dev \
    libocct-visualization-dev
```

Package names may vary depending on the Linux distribution and Open CASCADE version.

### Configure

```bash
cmake -S . -B build -G Ninja
```

### Build

```bash
cmake --build build -j
```

### Run

The executable is currently generated under:

```bash
./build/src/ParametricCAD
```

Depending on the selected CMake build directory, for example when using CLion, it may instead be located under:

```bash
./cmake-build-debug/src/ParametricCAD
```

## Development Status

ParametricCAD is an early-stage experimental project.

Implemented:

- [x] Qt/Open CASCADE viewport integration
- [x] Box primitive
- [x] Cylinder primitive
- [x] Orbit
- [x] Pan
- [x] Zoom
- [x] Object selection
- [x] Edge selection
- [x] Face selection
- [x] Hover highlighting
- [x] Push/Pull for planar faces
- [x] Push/Pull preview
- [x] X-Ray display mode
- [x] Select-through / cycling detected geometry
- [x] Modeling toolbar

Planned:

- [ ] Parametric Push/Pull feature
- [ ] Undo / Redo
- [ ] Feature history
- [ ] Rectangle tool
- [ ] Line tool
- [ ] Circle tool
- [ ] Sketches on planar faces
- [ ] Move tool
- [ ] Rotate tool
- [ ] Scale tool
- [ ] Offset
- [ ] Snapping to endpoints, midpoints and intersections
- [ ] Dimensions and constraints
- [ ] Groups / Components
- [ ] Model tree
- [ ] STEP import/export
- [ ] STL export
- [ ] Improved X-Ray and hidden geometry interaction

## Architecture Direction

The long-term goal is to combine two modeling approaches.

### Direct modeling

Fast SketchUp-like interaction:

```text
Select face
→ Push/Pull
→ Move
→ Cut
→ Offset
```

### Parametric modeling

Operations are stored as editable features:

```text
Document
└── Body
    ├── Box
    ├── Sketch
    ├── Pad
    └── Pocket
```

This should allow simple direct manipulation while preserving an editable CAD feature history.

## License

ParametricCAD is licensed under the MIT License.

See:

```text
LICENSE
```

Third-party libraries used by the project retain their respective licenses.

The main external dependencies include:

- Qt 6
- Open CASCADE Technology

See `THIRD_PARTY_LICENSES.md` for additional information.

## Purpose

This project is developed primarily for:

- learning modern CAD architecture;
- experimenting with Open CASCADE;
- implementing interactive 3D modeling tools;
- studying parametric and direct modeling approaches;
- demonstrating practical C++ and desktop application development skills.

Contributions, experiments, bug reports and technical discussions are welcome.
