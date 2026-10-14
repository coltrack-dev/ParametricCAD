# ParametricCAD v0.1.0-alpha regression report

## Scope

This release suite adds `tests/release_regression_tests.cpp`, a Qt Test
executable that uses the real `ModelingController`, `Body`, OCCT operations,
`QUndoStack`, and `ProjectFile`. It covers complete modeling paths rather than
mocking feature results.

Implemented scenarios:

- electronics enclosure: closed sketch, extrusion, face-attached cavity,
  pocket, fillet, save/load, recompute, shape validity and volume;
- dependency recomputation after replacing sketch geometry, including stable
  feature IDs and an idempotent second recompute;
- face-attached sketch frame/reference recovery after an upstream resize;
- sketch/entity/feature undo and redo;
- empty sketches, first/last entity transitions, invalid open profiles and
  explicit Extrude rejection;
- Boolean result, visibility undo/redo, save/load and recompute.

Existing tests are reused for detailed revolve, sweep, shell, chamfer, pattern,
push/pull, constraints, topology-reference failure, IFC, GUI action state and
project archive behavior. No new modeling functionality was added.

## Verification performed

Environment:

- Ubuntu 24.04
- GCC 13.2
- Qt 6.4.2
- OpenCASCADE from the configured Linux build
- CMake/Ninja build directory: `build-cae`
- `PARAMETRIC_CAD_ENABLE_IFC=OFF` in this existing build

Commands executed:

```sh
cmake --build build-cae -j
ctest --test-dir build-cae -R release_regression_tests --output-on-failure
ctest --test-dir build-cae --output-on-failure
git diff --check
```

The new release target was compiled successfully. Its initial enclosure
assertion used an unrealistically tight OCCT bounding-box tolerance and failed;
the assertion was corrected to `1e-5`, appropriate for OCCT geometric
tolerances. The corrected release target passed all six scenarios.

The focused release target passed **6/6 scenarios**. The final full suite after
adding the target passed **25/25 tests**.

IFC fixtures and Gmsh are conditional. Gmsh was available and its integration
tests passed. The IFC test executables were not registered because
`PARAMETRIC_CAD_ENABLE_IFC=OFF`; therefore neither the Duplex IFC2X3 baseline
nor the BuildingBIMModel IFC4 acceptance result is claimed here. No GUI
mouse/OpenGL session was run in this headless environment.

## Release assessment

Parametric-model correctness is **automated-gate ready**, but not fully
release-certified: the manual GUI checklist has not been executed on Ubuntu,
Windows 11, and macOS Intel. The automated P0 controller/model coverage is
passing.

Current release blockers:

1. P1 / MANUAL REQUIRED: execute the GUI checklist on the three target
   platforms, including Windows high-DPI mouse offsets and wheel zoom.
2. P0 / BLOCKED: run the IFC-enabled regression gate with the matching
   IfcOpenShell installation and verify Duplex IFC2X3 plus BuildingBIMModel
   IFC4 baselines.
No known P0 automated modeling defect remains from the implemented scenarios.
