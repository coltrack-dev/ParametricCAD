# ParametricCAD v0.1.0-alpha Release Readiness

Audit date: 2026-10-10

This is a source and verification audit. No product code was changed. A row is
marked **verified** only when the current source and an executed test or build
provide evidence. Backend classes alone do not establish that a native GUI
workflow or a distributable package is ready.

## Executive summary

The Linux development build is healthy: the existing `build/` tree is a CMake
Debug/Ninja build with IFC enabled, it rebuilt successfully (nothing to do),
and all 17 registered CTest tests passed. The model, persistence, selection,
Sketch service, visibility, IFC import, and undo/redo tests provide substantial
alpha coverage.

The release is not ready to claim native desktop readiness yet. The current
CTest invocation runs `main_window_tests` with `DISPLAY` and `WAYLAND_DISPLAY`
cleared and `QT_QPA_PLATFORM=offscreen`; 23 GUI/viewer scenarios are skipped
and only four execute. No Windows or macOS build was executed. There is no
install/package target, and the repository has no top-level `LICENSE` file even
though `THIRD_PARTY_LICENSES.md` states that the project is MIT licensed.

### Release decision

**Status: conditional alpha / not release-ready for a native binary release.**

The model/backend can proceed to a controlled developer alpha after the P0
verification gate below. A public binary release should wait for native GUI
smoke coverage, a packaging decision, and licensing metadata.

## Priority classification

| Priority | Finding | Evidence | Release impact |
|---|---|---|---|
| P0 | Native 3D GUI workflow is not verified in this environment | `tests/main_window_tests.cpp` skips native/viewer cases when no display is present; verbose run: 4 passed, 23 skipped | Blocks claiming that Create Sketch, selection, Sketch tools, Sweep, Revolve, and viewer presentation work in a release build |
| P0 | No release-build/native smoke gate is recorded | `build/CMakeCache.txt` is `CMAKE_BUILD_TYPE=Debug`; no CI or packaging workflow was found | A passing Debug/headless test set is insufficient evidence for v0.1.0-alpha desktop release |
| P1 | No install/package/CPack pipeline | `CMakeLists.txt` has no `install()` or CPack configuration | Users receive no reproducible Linux/Windows/macOS artifact or dependency bundle |
| P1 | Project license metadata is incomplete | No top-level `LICENSE*` file; `THIRD_PARTY_LICENSES.md` references MIT but does not provide the project license text | Legal/release distribution ambiguity |
| P1 | Large IFC acceptance executable is not a registered CTest | `tests/CMakeLists.txt` builds `ifc_building_acceptance` but has no `add_test`; the audit launch did not complete and was stopped | IFC large-model readiness is not part of the normal release gate |
| P1 | Windows and macOS are unverified | Only Linux build/test commands were executed; `CMakePresets.json` and scripts are configuration evidence, not platform results | Cross-platform claims would be unsupported |
| P1 | Advanced feature UI/persistence coverage is uneven | `docs/ROADMAP.md`; no MainWindow actions for Loft or Offset; many tests are controller/model-level | Marketing every backend feature as a polished GUI feature would overstate readiness |
| P2 | Solver, topology recovery, and very-large-model limits remain documented | `docs/ROADMAP.md`, `docs/TOPOLOGICAL_REFERENCES.md`, `docs/PERFORMANCE.md` | Follow-up hardening after the alpha gate |

## Implemented feature inventory

### Parametric CAD model and geometry

Evidence: `src/operations/ParametricFeatures.h`, `src/operations/BasicFeatures.cpp`,
`src/application/ModelingController.h`, and the factory registry in
`src/model/ProjectFile.cpp`.

| Area | Implemented source capability | Persistence | Executed coverage | Assessment |
|---|---|---|---|---|
| Primitives | Box, Cylinder, Cone, Sphere, Torus, Hexagon | Factory entries and parameter writers | `model_tests`, `controller_tests`, `boolean_cut_tests` | Backend verified; native creation not verified in this audit |
| Sketch placement | Global XY/XZ/YZ and planar-face support through `SketchSupportType`, `currentFrame()`, and topology references | `ProjectFile` Sketch factory | `controller_tests`, `sketch_face_tests`, `sketch_trim_tests` | Model and persistence verified; native viewer placement is skipped |
| Sketch entities | Line, Circle, Arc, three-point Arc conversion, Center Arc, construction geometry | Entity/constraint serialization | `controller_tests`, `sketch_trim_tests`, partial `main_window_tests` | Backend verified; native Arc/Rectangle tests skipped |
| Sketch constraints | Coincident, Horizontal, Vertical, Distance, Radius, Horizontal/Vertical Distance, Angle, Parallel, Perpendicular, Angle Between Lines, Tangent, Equal, Chamfer | Variant serialization/deserialization | `sketch_trim_tests`, `controller_tests` | Solver coverage is real but not a complete general-purpose CAD solver |
| Sketch editing services | Trim, Extend, Line-Line Fillet, Line-Line Chamfer, profile builder, path builder | Resulting entities/constraints persist | `sketch_trim_tests`, `controller_tests` | Service/model coverage verified; native interactive coverage is not |
| Profile extraction | Closed profiles, holes/disconnected regions, construction geometry exclusion | Via source Sketch | `controller_tests` | Verified at model level |
| Extrude/Pocket | Sketch-based parametric Extrude and Pocket, including face-attached profiles | Factory and parameters | `controller_tests`, `sketch_face_tests` | Model-level workflows verified |
| Revolve | Global axes, Sketch Line axis, Model Edge axis, persistent axis definitions | Factory and stable IDs/references | `controller_tests`; GUI tests skipped | Model-level verified; native axis picking not verified |
| Sweep | Model Edge path and SketchPath with Line/Arc chain, stable path references/entity IDs | Factory supports both path types | `controller_tests`; GUI tests skipped | Backend/persistence verified; native acceptance is unverified |
| Shell/Offset/Loft | Feature classes and OCCT builders exist | Factory and parameter writers exist | Limited or no dedicated end-to-end coverage in registered tests | Treat as backend/experimental until dedicated workflows pass |
| 3D Fillet/Chamfer | Edge-based feature classes with persistent topology references | Factory supports topology references and legacy indices | `controller_tests`, `boolean_cut_tests`-style geometry coverage | Model path exists; native feature dialog coverage skipped |
| Boolean | Fuse, Cut, Common | Factory and parameters | `boolean_cut_tests`, `controller_tests`, `undo_tests` | Cut and history verified; all native combinations not verified |
| Patterns | LinearPattern and PathPattern feature classes | Factory and parameters | `controller_tests`, `undo_panel_tests` | Model/property coverage verified; native creation skipped |
| Transform/PushPull | Transform commands, duplicate/delta transforms, Push/Pull | Commands and feature parameters | `push_pull_drag_tests`, `controller_tests`, `undo_tests` | Strong model/controller coverage; viewer gestures unverified without display |

### Persistence and history

`ProjectArchive` writes a ZIP-compatible `.pcad` container containing a
manifest and compressed geometry entries. `ProjectFile` validates a temporary
model before replacing the active project, supports canonical Body features,
and retains legacy JSON/Box/Cylinder compatibility. Undo/redo is implemented
with `QUndoStack` commands, but undo history is intentionally not serialized.

Evidence: `src/model/ProjectArchive.*`, `src/model/ProjectFile.*`,
`src/commands/FeatureCommands.*`, `tests/project_file_tests.cpp`,
`tests/project_archive_tests.cpp`, `tests/undo_tests.cpp`.

Persistence tests cover canonical and legacy files, Sketch placements/entities,
feature dependencies, visibility state, topology references, IFC payloads, and
selected advanced features. They do not prove that every UI editor can edit
every feature after reload.

### BIM and IFC

IFC is optional and enabled by `PARAMETRIC_CAD_ENABLE_IFC`. The build expects a
separately installed/pinned IfcOpenShell v0.7.1 tree, plus Qt, OCCT, minizip,
and a C++20 toolchain. `IfcOpenShellAdapter`, `IfcImporter`, `ImportedFeature`,
`BimNavigationModel`, `BimNavigationPanel`, and `BimInspectorPanel` are present.

The executed Linux build had IFC enabled. `ifc_import_tests` and
`ifc_workflow_tests` passed in the full CTest run; `bim_navigation_tests` also
passed. `ifc_building_acceptance` is built but not registered with CTest and
did not complete during the audit, so large-model IFC acceptance remains
unverified.

### UI and interaction

`MainWindow` creates menus and grouped toolbars for File/History, Sketch,
Constraints, Solid Modeling, Modify, Transform, Selection, View, Visibility,
and BIM. It uses shared `QAction` instances, QSettings layout persistence,
SVG resources, selection modes, Sketch tools, staged Revolve/Sweep pickers,
and panels for feature/BIM editing.

Implemented source actions include Create/Edit/Finish Sketch, Line,
Rectangle, Circle, Arc, Center Arc, Trim, Extend, Sketch Fillet/Chamfer,
constraint actions, Box/Cylinder, Extrude, Pocket, Revolve, Sweep, patterns,
3D Fillet/Chamfer, Shell, Booleans, File/History, selection modes, section
clipping, display modes, visibility actions, and optional IFC/BIM panels.

There are no normal MainWindow actions for every backend class: notably Loft
and Offset are not exposed as primary modeling actions in the inspected UI.
The source documentation explicitly warns that UI and persistence coverage
varies by feature (`README.md`, `docs/ROADMAP.md`).

## Test and workflow coverage

| User workflow | Relevant tests | Result in this audit | Gap |
|---|---|---|---|
| Create global Sketch, draw profile, Extrude | `controller_tests`, `sketch_face_tests` | Model-level pass | Native Create Sketch/tool/mouse path skipped |
| Face-attached Sketch, edit, Pocket | `controller_tests`, `sketch_face_tests` | Model-level pass | Native face selection/presentation skipped |
| Sketch constraints, Trim, Extend, Arc | `sketch_trim_tests`, `controller_tests` | Model/service pass | Native tool interaction skipped |
| Sketch Fillet/Chamfer and atomic history | `controller_tests` | Service/controller pass | Native hover/panel/commit skipped |
| Sweep Model Edge and SketchPath | `controller_tests` | Backend/persistence pass | Native staged Pick Path/Commit skipped |
| Revolve global/Sketch Line/Model Edge axes | `controller_tests` | Backend/persistence pass | Native axis picker skipped |
| Boolean Cut and invalid geometry | `boolean_cut_tests`, `controller_tests` | Pass | Native Boolean UI not verified |
| Push/Pull and transform gestures | `push_pull_drag_tests`, `controller_tests` | Math/controller pass | Native OCCT viewer skipped |
| Visibility/groups/filters/presets/saved views | `model_tests`, `undo_tests` | Model pass | Native camera/viewer presentation skipped |
| Undo/redo and property panels | `undo_tests`, `undo_panel_tests` | Pass | Feature-specific native editors not all covered |
| Save/load `.pcad` and legacy files | `project_file_tests`, `project_archive_tests` | Pass | Cross-platform archive/runtime validation missing |
| IFC import, metadata, navigation | `ifc_import_tests`, `ifc_workflow_tests`, `bim_navigation_tests` | Pass for standard fixtures | Large building acceptance incomplete; native BIM panels skipped |
| Full native GUI startup and toolbars | `main_window_tests` | 4 pass, 23 skipped | Requires X11/Wayland or compatible virtual display |

## Build, dependency, platform, and packaging audit

### Executed

| Check | Command/result | Evidence |
|---|---|---|
| Existing build | `cmake --build build -j` → exit 0, `ninja: no work to do` | `build/CMakeCache.txt`, command output |
| Registered tests | `ctest --test-dir build --output-on-failure` → 17/17 passed | Full CTest output; total 8.00 s |
| GUI test detail | `ctest -V -R ...` → `main_window_tests`: 4 passed, 23 skipped | QtTest skip messages cite missing native display |
| Formatting sanity | `git diff --check` is required after this audit document is created | Must be rerun before commit |

### Not executed or not verified

- Clean Release configure/build was not run; the inspected build is Debug.
- Windows MSVC/vcpkg preset was not run.
- macOS bundle build and `macdeployqt` deployment were not run.
- Linux install/package artifact was not produced; CMake has no install/CPack rules.
- Native X11/Wayland GUI smoke was not run.
- `ifc_building_acceptance` did not complete in the audit run and is not a CTest case.
- No code-signing, notarization, Windows signing, installer, or AppImage validation exists in the repository.

### Dependency and licensing risks

Required build dependencies are CMake 3.24+, C++20, Qt6 Core/Gui/Widgets/
OpenGL/OpenGLWidgets/Concurrent, OCCT modeling/visualization libraries, and
minizip. IFC additionally requires the pinned IfcOpenShell v0.7.1 installation.
The CMake configuration fails when IFC is ON and that root is absent; the
optional path must be explicitly disabled for a dependency-light build.

`THIRD_PARTY_LICENSES.md` documents Qt LGPL obligations, OCCT LGPL-2.1 plus
exception, CMake/Ninja and system libraries. There is no top-level project
license text despite the MIT statement, so distribution should not proceed
until licensing ownership and notices are made explicit.

## Recommended release blockers and fixes

1. **P0:** run a native Linux smoke suite under a real X11/Wayland session or
   validated Xvfb setup, execute the skipped MainWindow scenarios, and record
   the exact alpha workflows: Create Sketch on all planes, face attachment,
   Rectangle/Arc, Extrude/Pocket, Revolve, Sweep Model Edge and SketchPath,
   Fillet/Chamfer, visibility, and Save/Load.
2. **P0:** configure a clean Release build with the intended dependency set and
   rerun the complete test gate. Do not use the existing Debug no-op build as
   the release artifact evidence.
3. **P1:** decide and implement the distribution format (or explicitly state
   source-only alpha), including Qt/OCCT/minizip/IfcOpenShell runtime handling
   and license notices.
4. **P1:** register the large IFC acceptance executable as a test with an
   explicit fixture/timeout, or document why it is a manual stress test.
5. **P1:** publish a feature support table that distinguishes backend class,
   UI action, persistence, and tested workflow; keep Loft/Offset marked as
   non-primary UI features until verified.
6. **P2:** expand solver/topology/performance coverage and add cross-platform
   CI before moving beyond alpha.

