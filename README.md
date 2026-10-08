# ParametricCAD

For IFC BIM navigation, see [docs/BIM_NAVIGATION.md](docs/BIM_NAVIGATION.md).

ParametricCAD is a desktop parametric CAD application written in C++20 with
Qt 6 and Open CASCADE Technology (OCCT). It keeps an editable feature history,
rebuilds OCCT B-Rep geometry from parameters, and provides direct viewer
interaction for selection, transformation, snapping, visibility, spatial
inspection, section clipping, and saved views.

Projects use the native editable `.pcad` format. New files are portable
compressed containers; legacy JSON projects remain readable.

## Current capabilities

### Modeling

- Parametric Box, Cylinder, Cone, Sphere, Torus, and Hexagon primitives.
- Unified Sketch creation on the XY, XZ, and YZ global planes or a selected
  planar face, with Rectangle, Line, Circle, and Arc drawing tools.
- Extrude, Pocket, Push/Pull, Revolve, Boolean Fuse/Cut/Common, Fillet,
  Chamfer, Shell, Offset, Loft, and Sweep feature classes.
- Linear and Path Pattern feature classes.
- Feature placement for Move/Rotate transforms.
- Stable feature IDs and dependency-aware recompute.
- QUndoStack-based model editing and undo/redo.

The availability of an operation in the UI and its persistence coverage can
vary by feature. The canonical runtime model is `Body` containing
`ParametricFeature` objects; legacy `Document`/`Feature` classes remain only
for compatibility and legacy input conversion.

### Sketch

Create sketches through one command: choose `XY Plane`, `XZ Plane`, `YZ Plane`,
or (when one planar face is selected) `Selected Planar Face`. The command then
enters normal Sketch edit mode, where Rectangle, Line, Circle, Arc, Trim,
Extend, and constraint tools create the same stable-ID Sketch entities. A
 global XY rectangle can be finished and extruded; XZ and YZ sketches are
 available for profiles and independent Sweep paths. The sketch
subsystem is not yet a full general-purpose constraint CAD system; advanced
profile and solver coverage is still limited. See
[docs/PARAMETRIC_FEATURES.md](docs/PARAMETRIC_FEATURES.md) for the model scope.

`Arc` is the three-point tool: pick start, end, then a point on the arc.
`Center Arc` retains the center, start, end workflow. Both create the same
persistent Arc entity.

The main window groups existing commands into movable, dockable File & History,
Sketch, Constraints, Solid Modeling, Modify, Transform, Selection, View,
Visibility, and BIM toolbars. Toolbar visibility, ordering, docking, and window
geometry are persisted with Qt `QSettings`.
The toolbars use a bundled, theme-safe SVG icon set from
`src/resources/toolbar_icons.qrc`.

### Revolve from a Sketch

The complete native workflow is:

```text
Create Sketch -> draw closed profile -> draw separate axis Line
-> mark the Line Construction -> Finish Sketch
-> select the Sketch in the model tree -> Revolve
-> Axis = Sketch Line -> Pick Axis -> click the Line in the viewport
-> verify the selected Sketch Line/entity -> set Angle (for example 360°)
-> confirm/create Revolve
```

Selecting `Sketch Line` in the axis control does not select a line by itself;
`Pick Axis` must be followed by a viewport click. The chosen line is stored by
stable `SketchEntityId`, and the profile and axis may be in the same Sketch.
Construction geometry remains visible but does not participate in profile
boundary extraction, so a Rectangle plus a Construction Line remains valid.
An ordinary extra open Line still follows the normal profile validation rules.

For other axes, choose Global X/Y/Z directly. Choose `Model Edge`, press
`Pick Axis`, and click a linear model Edge to use a persistent topology
reference.

The Construction control is also a drawing default when no existing Sketch
edge is selected: enabled mode makes newly drawn Lines construction geometry.
When an existing Sketch Line is selected, it toggles that entity's Construction
property instead.

### Selection and interaction

- Object, edge, and face selection modes.
- OCCT selection normalized to stable feature IDs and application selection
  state, with tree/viewer synchronization.
- Transform gizmo with Move/Rotate and duplicate-transform workflows.
- Cached endpoint, midpoint, and bounded intersection snapping.
- Push/Pull preview and face interaction.
- Zoom around the cursor, pan, orbit, standard views, and locked-Z turntable
  orbit. Free orbit remains available.
- XYZ orientation widget and X-Ray/select-through support.

### Visibility and model inspection

The visibility policy is owned by `VisibilityManager` and projected by
`ModelPresenter` into `CadViewer` presentations.

- Persistent feature Hide/Show.
- Temporary Isolate and Show All.
- Persistent visibility groups, nested groups, and multiple group membership.
- Type, role, and derived category filters.
- Ghost Others with `Visible`, `Ghosted`, and `Hidden` presentation modes.
- Feature-level spatial visibility using cached world-space bounding boxes.
- Spatial box interaction with Hidden Outside and Ghost Outside modes.
- Viewer-only section clipping on X, Y, or Z with Flip and Clear actions.
- Saved views containing presentation and camera state.

Spatial visibility filters features using bounding boxes. It does not clip
topology. Section clipping uses OCCT `Graphic3d_ClipPlane` through
`V3d_View`; it does not modify B-Rep geometry. Saved views are presentation
metadata, not modeling features.

### Project format

`.pcad` has logical schema version 1 and uses a ZIP-compatible container for
new saves. `manifest.json` stores canonical feature parameters, stable IDs,
dependencies, placement, persistent feature visibility, visibility
groups/filters/presets, and saved views; imported B-Rep geometry is stored in
compressed `geometry/` entries. Legacy plain JSON files, including embedded
ImportedFeature payloads, remain readable. Geometry is rebuilt from parameters
where applicable, and imported IFC geometry remains available without the
source IFC. Undo history is not stored. Legacy Box/Cylinder input is converted
to canonical Body features.

The loader validates a temporary model before replacing the active project.
Files without newer optional metadata remain valid. See
[docs/PCAD_FORMAT.md](docs/PCAD_FORMAT.md).

### IFC import

IFC-enabled builds expose `File -> Import IFC...`. Parsing and geometry
conversion run in a worker thread with cooperative cancellation and progress;
the prepared result is committed to the current Body in one bulk, undoable
operation. Imported products are read-only `ImportedFeature` objects and use
the normal viewer, selection, visibility, and `.pcad` persistence paths. IFC
support is optional; builds without the pinned IfcOpenShell dependency omit
the action. See [docs/IFC_IMPORT.md](docs/IFC_IMPORT.md).

Selecting an imported IFC product also exposes a read-only BIM Inspector with
stored identity, type, material, property-set, quantity, spatial, and source
metadata. It does not require the original IFC file.

### Performance

Large project Open uses a worker phase for file parsing and temporary model
assembly, followed by GUI-thread time-budgeted recompute and presentation
continuations. CadViewer uses bulk updates, deferred selection activation, and
incremental visibility deltas. Visibility-only operations do not recompute
the Body.

The detailed timber-frame fixture
`examples/house_1_5_storey_side_dormer.pcad` is used for large-project loading,
placement/transform, visibility, spatial, section, and saved-view checks.
See [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

### Architecture

```text
Qt UI (MainWindow, FeatureEditorPanel, CadViewer)
                         |
Application controllers/services
                         |
Body -> ParametricFeature -> OCCT TopoDS_Shape
                         |
ModelPresenter -> AIS presentations / viewer interaction
                         |
ProjectFile -> .pcad JSON
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/diagrams/architecture-overview.puml](docs/diagrams/architecture-overview.puml).

## Build

### Platform build guides

- [Ubuntu/Linux](docs/CLION.md)
- [macOS Ventura](docs/BUILD_MACOS_VENTURA.md)
- [Windows 11](docs/BUILD_WINDOWS_11.md)

### Dependencies

- CMake 3.24 or newer;
- Ninja (recommended) or another supported CMake generator;
- a C++20 compiler;
- Qt 6 Core, Gui, Widgets, OpenGL, OpenGLWidgets, and Concurrent;
- Open CASCADE Technology, including visualization and modeling libraries;
- TBB may be pulled by the OCCT installation, depending on the platform.

Linux development uses one canonical build tree. IFC support is enabled in the
default development configuration, and the scripts below always build and run
the matching binary from `build/`:

```bash
./configure.sh   # first time or after changing dependencies/options
./run.sh         # rebuild and run build/src/ParametricCAD
```

On macOS, `run.sh` uses a separate `build-macos/` tree and launches the
application executable from `ParametricCAD.app`. Configure Qt and Open CASCADE
with `Qt6_ROOT`, `OpenCASCADE_DIR`, or `CMAKE_PREFIX_PATH` as needed. IFC is
disabled by default on macOS; set
`PARAMETRIC_CAD_IFCOPENSHELL_ROOT` to an IfcOpenShell installation to enable it.

To run the tests separately:

```bash
ctest --test-dir build --output-on-failure
```

The old `build-ifc/` directory is not a normal development target. If it is
the only correctly configured tree on an existing checkout, migrate it once
with `mv build-ifc build` (after moving or removing the old `build/` directory)
or regenerate `build/` with `./configure.sh`. The scripts never delete build
directories automatically.

The test suite is headless where possible. GUI camera, clipping, and
interaction checks require a desktop session.

### Sweep from a Sketch

Select a valid closed Sketch and activate `Sweep`. In the staged dialog click
`Pick Path`, then select either one model Edge in the viewport or an open Sketch
path in the model tree. Sketch paths automatically use non-construction Line
and Arc entities, order them by endpoint connectivity, and display the entity
count before Commit. Model Edge paths retain their owner ID and persistent
`TopologicalReference`; Sketch paths retain the path Sketch ID and stable
`SketchEntityId` values. Both are rebuilt into a `TopoDS_Wire` and use the same
Frenet/tangent-following Sweep backend.

## Example project

```text
examples/house_1_5_storey_side_dormer.pcad
```

This is a detailed timber-frame demonstration model and a large-project
loading fixture. It exercises many placed and transformed features and is
useful for checking visibility groups/filters, spatial bounding-box focus,
section clipping, and saved-view restoration.

## Further documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Parametric feature architecture](docs/PARAMETRIC_FEATURES.md)
- [.pcad format](docs/PCAD_FORMAT.md)
- [Performance and project loading](docs/PERFORMANCE.md)
- [Ubuntu/Linux build](docs/CLION.md)
- [macOS Ventura build](docs/BUILD_MACOS_VENTURA.md)
- [Windows 11 build](docs/BUILD_WINDOWS_11.md)
- [Roadmap](docs/ROADMAP.md)
- [Undo/Redo](docs/UNDO_REDO.md)
