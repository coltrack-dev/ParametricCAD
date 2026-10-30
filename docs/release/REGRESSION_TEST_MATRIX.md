# ParametricCAD v0.1.0-alpha regression matrix

Status values are `PASS`, `FAIL`, `BLOCKED`, `NOT RUN`, and `MANUAL REQUIRED`.
The new `release_regression_tests` target is the fast end-to-end controller gate;
specialized tests remain the source of detailed operation coverage.

| Scenario | Subsystems | Existing/new coverage | Expected result | Priority | Status |
|---|---|---|---|---|---|
| Electronics enclosure: sketch → extrude → face sketch → pocket → fillet → save/load | ModelingController, Sketch, dependencies, OCCT B-Rep, ProjectFile | New `electronicsEnclosureRoundTrip`; existing `sketchExtrudeAndPocketAreParametric`, fillet tests | Valid solid, positive volume, stable IDs, equivalent loaded geometry | P0 | PASS |
| Dependency recompute after profile edit | Body dirty propagation, recompute, feature identity | New `dependencyRecomputeKeepsIdentityAndChangesGeometry`; existing undo/recompute tests | Dependents rebuild once, geometry changes, second recompute is stable | P0 | PASS |
| Face-attached sketch after source resize | TopologicalReference, frame, face sketch, pocket | New `faceAttachedSketchResolvesOrFailsExplicitly`; existing topology recovery tests | Reference resolves to intended face or produces explicit failure | P0 | PASS |
| Undo/redo representative lifecycle | QUndoStack, Body, feature graph | New `undoRedoPreservesFeatureGraph`; existing `undo_tests`, `undo_panel_tests` | No stale shape or lost feature identity | P0 | PASS |
| Empty/invalid sketch lifecycle | Sketch build, profile validation, model persistence | New `emptyAndInvalidSketchesRemainSafe`; existing `sketch_face_tests` | Empty feature is valid and geometry-free; operations reject it | P0 | PASS |
| Boolean and visibility persistence | BooleanFeature, VisibilityManager, ProjectFile | New `booleanVisibilityAndPersistenceRegression`; existing boolean tests | Valid result and visibility survive save/load | P1 | PASS |
| Sketch constraints and profile validation | Constraint solver, profile builder | Existing controller and sketch tests | Constraints solve; invalid/open profiles fail deterministically | P1 | PASS |
| Fillet/chamfer/topological naming | TopologicalReference, OCCT modifiers | Existing `persistentTopologySurvivesDetailAndShellResize`, fillet/chamfer tests | Supported references survive edits; invalid references fail explicitly | P0 | PASS |
| Revolve/sweep/shell/pattern/push-pull/transform | Parametric operation graph | Existing controller and project-file tests | Operations recompute and persist | P1 | PASS |
| IFC2X3 Duplex import | IfcOpenShell, metadata, BIM navigation | Existing `ifc_import_tests`, `ifc_workflow_tests` | 286 products, 215 geometry/imported, 71 skipped, 0 failed | P0 | NOT RUN (current build IFC=OFF) |
| IFC4 BuildingBIMModel import | IfcOpenShell, metadata, BIM navigation | Existing `ifc_building_acceptance` | IFC4 import and spatial metadata acceptance | P0 | NOT RUN (current build IFC=OFF) |
| Qt GUI smoke workflow | MainWindow, CadViewer, OpenGL/input | Existing `main_window_tests`; manual checklist | Stable actions and real mouse/viewer behavior | P1 | MANUAL REQUIRED |
| Windows high-DPI input behavior | Qt coordinate conversion, wheel zoom | No automated coverage | No offset or zoom regression | P1 | MANUAL REQUIRED |
| macOS Intel OpenGL/viewer behavior | Qt/OCCT viewer, app bundle | No automated coverage | Create/edit/select/save workflow works in app | P1 | MANUAL REQUIRED |

## Commands

```sh
cmake --build build-cae --parallel
ctest --test-dir build-cae -L regression --output-on-failure
ctest --test-dir build-cae -R 'release_regression_tests|controller_tests|project_file_tests|sketch_face_tests' --output-on-failure
```

IFC tests are registered only when `PARAMETRIC_CAD_ENABLE_IFC=ON` and the
IfcOpenShell installation is complete. Gmsh tests are registered only when the
`gmsh` executable is found.
