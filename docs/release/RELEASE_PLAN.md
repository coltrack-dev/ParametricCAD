# ParametricCAD v0.1.0-alpha Release Plan

This plan is based on the v0.1.0 source audit in
[RELEASE_READINESS.md](RELEASE_READINESS.md). It does not add features or
change architecture; it defines the evidence required to ship the current
scope as an alpha.

## Release scope

The alpha scope is the existing Body/ParametricFeature model, Sketch profiles,
core solid operations, selection/visibility/history, `.pcad` persistence, and
optional IFC import/BIM inspection. Advanced feature classes are included in
the scope only where both UI exposure and a passing workflow are documented.
Loft and Offset remain backend capabilities without a primary MainWindow
workflow unless explicitly promoted and tested.

## Prioritized implementation and verification plan

### Gate 0 — establish a reproducible release build (P0)

Acceptance criteria:

- A clean Release build is configured from a clean build directory.
- The intended dependency versions and IFC ON/OFF choice are recorded.
- `cmake --build <release-build> -j` succeeds from a clean tree.
- The produced executable is the one used by the smoke tests.
- `ctest --test-dir <release-build> --output-on-failure` completes with no
  failures; skipped tests are explained and accepted explicitly.

Suggested Linux commands:

```bash
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPARAMETRIC_CAD_BUILD_TESTS=ON \
  -DPARAMETRIC_CAD_ENABLE_IFC=OFF
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
```

Repeat with IFC enabled and the pinned IfcOpenShell root when IFC is part of
the release artifact.

### Gate 1 — native Linux GUI acceptance (P0)

Run with a real display or a validated virtual display that supports OCCT
OpenGL. Do not treat Qt offscreen skips as native GUI evidence.

Acceptance criteria:

1. Application starts and creates the CadViewer without crash.
2. New project, Open, Save, Undo, Redo, and toolbar layout persistence work.
3. `Create Sketch` works on XY, XZ, YZ, and a selected planar face.
4. Rectangle, Line, Circle, Arc/Center Arc, Trim, Extend, constraints, Sketch
   Fillet, and Sketch Chamfer complete without losing geometry.
5. Closed Sketch profiles extrude and pocket; face-attached profiles remain
   correctly placed.
6. Revolve works with a global axis, Sketch Line axis, and Model Edge axis.
7. Sweep explicitly stages Pick Path and commits both Model Edge and SketchPath
   workflows only after a valid path is selected.
8. 3D Fillet, Chamfer, Shell, Boolean, patterns, Push/Pull, Move/Rotate, and
   selection modes show a visible result or an explicit failure message.
9. Visibility, section clipping, saved views, and BIM panels behave in the
   native viewer.
10. Save/Load after each representative workflow preserves editable history.

The existing `main_window_tests` should be run natively, not only under its
current offscreen configuration. Capture the skipped-test count and any
viewer/OpenGL warnings.

### Gate 2 — persistence and dependency integrity (P1)

Acceptance criteria:

- Canonical `.pcad` ZIP contents validate (`manifest.json`, geometry entries,
  stable IDs and dependencies).
- Global and face-attached Sketches, Sketch constraints, Sweep paths,
  Revolve axes, topology references, visibility state, IFC metadata, and
  advanced features covered by the release scope survive Save/Load.
- Legacy JSON/legacy primitive input remains readable or is explicitly removed
  from the release contract.
- Invalid loads leave the current project unchanged.
- No dependency is loaded from an undocumented developer-only path.

### Gate 3 — IFC acceptance (P1, if IFC is shipped)

Acceptance criteria:

- `ifc_import_tests` and `ifc_workflow_tests` pass with the release dependency.
- `ifc_building_acceptance` is either registered in CTest with a bounded runtime
  or run as a documented manual stress test to completion.
- IFC2X3 and IFC4 fixtures import with non-null valid shapes, metadata,
  hierarchy/navigation, and `.pcad` round-trip.
- Import cancellation does not partially mutate the active Body.
- Runtime search paths work without a developer's `LD_LIBRARY_PATH` or local
  build directory.

### Gate 4 — packaging, licensing, and support statement (P1)

Acceptance criteria:

- A decision is recorded: source-only alpha, or supported binary artifacts.
- If binaries ship, Qt plugins, OCCT, minizip, IFC libraries, and runtime
  search paths are bundled and tested on each target OS.
- Linux packaging/install, Windows deployment, and macOS app-bundle deployment
  have repeatable commands and artifact checksums.
- The project license text is present and consistent with
  `THIRD_PARTY_LICENSES.md`; third-party notices and LGPL obligations are
  included with artifacts.
- Version `0.1.0-alpha` is visible in the application/package metadata.
- Known unsupported features and platform limitations are included in release
  notes.

### Gate 5 — post-alpha hardening (P2)

- Expand constraint-solver and topology-recovery property tests.
- Add feature-specific native GUI tests for every exposed action.
- Add cross-platform CI for Linux, Windows MSVC, and macOS Apple Clang.
- Add performance budgets for large IFC and large feature histories.
- Decide whether Loft and Offset receive supported UI workflows.
- Add feature suppression/history reorder/dependency visualization only as a
  separately scoped release item.

## Required test matrix

| Test layer | Required before alpha sign-off | Current status |
|---|---|---|
| Geometry/unit | All registered geometry and service tests | Pass in current Linux build |
| Model/controller | Feature dependencies, recompute, persistence, undo/redo | Pass in current Linux build |
| IFC standard fixtures | Import/workflow/navigation tests | Pass in current Linux build |
| Large IFC acceptance | Building fixture and bounded runtime | Not complete; not registered in CTest |
| Native GUI | MainWindow/CadViewer mouse, selection, OpenGL presentation | 23 scenarios skipped headlessly |
| Release build | Clean Release configure/build/test | Not run; current build is Debug |
| Windows | MSVC preset/build/test/package | Not run |
| macOS | App bundle/build/test/deploy | Not run |
| Packaging | Installable artifact and runtime dependency audit | No project packaging target |
| Licensing | Project license and third-party notices | Third-party notice exists; project license file missing |

## Final release checklist

- [ ] Release scope and unsupported features approved.
- [ ] Clean Linux Release build passes.
- [ ] IFC ON/OFF configuration decision recorded.
- [ ] All registered CTest tests pass in the release build.
- [ ] Native GUI tests run with a real/validated display; no unexplained skips.
- [ ] Create Sketch and Sketch editing acceptance completed on all placements.
- [ ] Extrude/Pocket/Revolve/Sweep acceptance completed with visible results.
- [ ] Fillet/Chamfer, Shell, Boolean, patterns, Push/Pull and transforms checked.
- [ ] Save/Load and legacy compatibility checked for the supported matrix.
- [ ] Visibility, sections, saved views, undo/redo checked natively.
- [ ] IFC standard and large-model acceptance completed if IFC ships.
- [ ] Windows and macOS results recorded, or explicitly excluded from alpha.
- [ ] Packaging/runtime libraries checked, or release declared source-only.
- [ ] Project license file and third-party notices included.
- [ ] Release notes list known limitations and exact build instructions.
- [ ] Tag/version metadata and artifact provenance recorded.

## Current recommendation

Do not label a downloadable desktop binary as release-ready yet. First close
Gate 0 and Gate 1. If the project intentionally ships a developer/source alpha
only, publish the current model/test results with the native GUI, platform,
packaging, IFC stress, and licensing limitations clearly called out.

