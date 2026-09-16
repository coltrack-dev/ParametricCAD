# ParametricCAD Roadmap

Status reflects the current source tree. **Done** means implemented in the stated
scope; **In progress** means a partial implementation exists; **Planned** means
missing. Kernel helpers alone do not count as complete editable UI operations.
Sketch/Face construction uses the existing Body architecture; Boolean Cut is not extended.

## Phase 1 — Core primitives

**Done**

- BoxFeature and CylinderFeature, plus additional parametric primitives.
- Document ownership of legacy primitive features.
- Body feature tree, object/edge/face selection, and FeatureEditorPanel.
- .pcad v1 save/load for supported primitives and Boolean dependencies.
- Save on close and autosave.pcad fallback for unnamed documents.

**In progress**

- Split Document/Body ownership is retained; unify only if a concrete requirement warrants it.
- Autosave: close-time saving exists; periodic saving and recovery do not.

**Planned**

- Vertex selection mode (current viewer exposes Object, Edge and Face).

## Phase 2 — Parametric construction

**Done**

- Kernel rectangle-to-closed-wire and wire-to-face helpers in BasicFeatures.
- Stable string IDs, lookup and duplicate rejection in Body/ParametricFeature.
- Explicit dependency registration and Dirty/UpToDate/Failed states.
- Existing dependency persistence for Boolean left/right IDs.
- Rectangle SketchFeature with editable width/height and Sketch -> Wire rebuild.
- FaceFeature from a selected Sketch, via Modeling -> Create Face.
- Sketch/Face .pcad persistence, sourceFeatureId validation and editable round-trip.
- Face -> Extrude creation, positive Length editor and v1 dependency/vector persistence.
- QUndoStack commands for create/edit/delete/clear; selective dependency rebuild.

**In progress**

- Direct model callers must explicitly call markDirtyFrom()/recompute(); UI commands
  already propagate changes to registered dependents without rebuilding unrelated branches.

**Planned**

- Automatic dependency propagation, topological ordering and cycle detection.
- Rebuild only affected dependents; define safe source deletion behavior.
- Reassess ownership only when needed; keep new feature integration in Body for now.

## Phase 3 — Boolean modeling

**Done**

- Existing Body BooleanFeature supports Cut, Fuse and Common, with UI and v1 persistence.

**Planned**

- Integrate Boolean Cut/Fuse/Common with the future Sketch/Face/Extrude chain.
- Hole operation.

Further Boolean work is deferred until Phase 2 is reliable.

## Phase 4 — Detail operations

**In progress**

- Fillet, Chamfer, Shell and Offset have kernel helpers and parametric wrappers;
  editor and persistence integration remain missing.

**Planned**

- Draft.
- Complete editable UI and persistence for detail operations.

## Phase 5 — Advanced sketches

**In progress**

- Circle wire kernel helper exists; no editable circle sketch feature.

**Planned**

- Line, circle, arc and polyline sketch entities.
- Constraints, dimensions and sketch solver.

## Phase 6 — Advanced features

**In progress**

- Revolve, Sweep and Loft have kernel helpers and parametric wrappers;
  editor and persistence integration remain missing.

**Planned**

- Complete editable Revolve/Sweep/Loft integration.
- Pattern and Mirror.

## Phase 7 — Exchange

**Planned**

- STEP import.
- STEP export.
- STL export.
- Optional BREP import/export.

## Phase 8 — Editing infrastructure

**Done**

- Undo/Redo and command history for model operations; see [UNDO_REDO.md](UNDO_REDO.md).
- Cut operand visibility derives from active history and follows Undo/Redo.

**Planned**

- Dependency visualization.
- Feature suppression.
- Reorder feature history if dependency validation and architecture permit.

## Phase 9 — UX

**Done**

- Basic feature tree and properties panel for supported features.
- Dirty/FAILED markers in the tree and error details in the properties panel.
- Tree selection uses existing AIS objects without clear/redisplay/Fit All.
- Parameter refresh no longer calls Fit All; load and explicit Fit still do.

**In progress**

- Improved tree/properties panel, including upcoming construction features.
- Incremental viewer updates are implemented; topology selection preservation needs further work.

**Planned**

- Context menus and feature icons.
- Per-feature visibility toggle and isolate/hide objects.
- Preserve topology selections where possible when geometry changes.

## Audit findings and TODOs

- Model ownership is split between Document/Feature and Body/ParametricFeature.
  Only the latter has IDs, dependency registration and error states.
- Dependencies remain pointers in memory with stable persisted IDs. Insertion validates
  source order, and removal rejects sources with active dependents.
- markDirtyFrom() now marks only the source and its registered dependents.
- ParametricFeature now handles both standard and OCCT exceptions as failed rebuilds.
- A failed Body recompute stops at the first error; invalid/dirty presentations are removed.
  Further error isolation across independent dirty branches remains future work.
- Parameter refresh updates changed AIS shapes in place and removes absent presentations.
- Viewer Push/Pull edits are not reflected in the parametric model or .pcad.
- Face reference errors include both IDs; older Boolean reference errors remain generic.
- UI Save rebuilds the Body before serialization. Low-level ProjectFile::save callers
  should also validate their model before writing.

## Automated verification

Run without a display:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- geometry_tests: existing vector math checks.
- project_file_tests: existing v1 round-trip, IDs/types/parameters and references,
  editing after load, malformed JSON/version/parameters/references/duplicate IDs,
  failed-load preservation, empty projects and write failure.
- model_tests (Qt Test, no QApplication): rectangle dimension validation, closed
  wire/face geometry and area changes, invalid/open profile rejection, extrusion
  volume/vector changes and errors, Document ownership, Body identity/dependency
  resolution and explicit rebuild/recovery, unsupported version/type handling.

- sketch_face_tests: production Sketch/Face geometry, validation, dependency rebuild,
  expired/wrong sources, mixed legacy primitive and Sketch/Face round-trip, editing after
  load, and broken sourceFeatureId rejection without replacing the active document.

- undo_tests: stable IDs, add/remove/clear, ordered restoration, typed parameter edits,
  Sketch/Face/Extrude dependency rebuild, Cut visibility, clean state and save/load.
- undo_panel_tests: offscreen editingFinished grouping and Ctrl+Z/Ctrl+Y in editors.
GUI camera behavior requires the manual check below; it is not covered by CTest.

## Manual smoke test

### Current implementation

1. Start ParametricCAD and create a Box from the Model panel.
2. Zoom, pan and orbit to a distinctive view.
3. Select different features in the tree; verify the view does not move.
4. Edit the Box width in Properties; verify geometry changes and camera/zoom/pan stay fixed.
5. Save As test.pcad, close, reopen and edit the Box again.
6. Verify object/edge/face selection, X-Ray, navigation and explicit Fit still work.

### Sketch -> Face

1. Choose Modeling -> Add Rectangle Sketch; verify the selected sketch appears in the tree.
2. Set a distinctive camera/zoom/pan, then choose Modeling -> Create Face.
3. Verify the Face appears, its source ID is shown in Properties and the view stays fixed.
4. Select the Sketch and edit Width/Height; verify the dependent Face updates in place.
5. Try Create Face with no selection, a primitive, a Face or multiple features selected;
   verify a clear message and no new feature.
6. Save As test.pcad, close, reopen, select the Sketch and edit its dimensions again.

### Target Sketch -> Face -> Extrude workflow

Implemented through Body; run this manual scenario to verify viewport behavior.

1. Start ParametricCAD.
2. Create Rectangle Sketch.
3. Create Face.
4. Extrude.
5. Change sketch width.
6. Verify Face and Extrude update.
7. Verify camera zoom/pan are preserved.
8. Change extrusion length.
9. Save as test.pcad.
10. Close application.
11. Open test.pcad.
12. Verify Sketch -> Face -> Extrude dependencies.
13. Edit parameters again after loading.
