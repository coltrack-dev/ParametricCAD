# ParametricCAD Roadmap

Status is based on the current source tree. A feature is marked **Done** only
for the scope that is actually implemented in the application.

## Completed

### Canonical parametric model

- Body-owned ordered `ParametricFeature` history with stable IDs.
- Dependency registration, dirty/recompute/failed states, placement, and OCCT
  shape generation.
- Legacy Document/Feature compatibility and legacy primitive conversion.
- Parametric primitive, profile, operation, pattern, transform, and Push/Pull
  paths present in the current source tree.

### Selection subsystem migration

- OCCT selection normalized through `SelectionAdapter` and
  `CadViewer::SelectionState`.
- Stable feature IDs and topology-reference restoration across model refresh.
- Tree/viewer selection synchronization.
- Transform gizmo and SnapManager use normalized primary selection rather than
  arbitrary OCCT selected-owner iteration.

### Visibility and inspection

- Separated model refresh/recompute from visibility refresh.
- Visible/Ghosted/Hidden presentation modes.
- Persistent feature Hide/Show, groups, nested groups, filters, visibility
  presets, isolation, Ghost Others, and Show All.
- Incremental effective-mode cache and viewer deltas.
- Feature-level spatial visibility using cached world-space bounding boxes,
  interactive spatial box, Hidden Outside, and Ghost Outside.
- Viewer-only OCCT section clipping with Section X/Y/Z, Flip, Clear, grid,
  normal indicator, and normal-constrained interaction.
- Persistent saved views for presentation, camera, spatial, section, isolation,
  and Ghost Others state.

### Large-project loading

- Temporary model construction during file loading.
- GUI-thread time-budgeted recompute continuations.
- Bulk AIS presentation updates and deferred selection/cache synchronization.
- Incremental visibility presentation updates.
- Large house regression/stress fixture and deterministic 100/500/5000
  visibility evaluation coverage.

### Editing infrastructure

- QUndoStack commands for supported feature/model edits, placement, persistent
  visibility groups/filters/presets, and property changes.
- Save/load validation and failed-load preservation.

## Current limitations / planned work

- Complete UI and persistence coverage for every advanced feature class remains
  incomplete even though several wrappers and geometry operations exist.
- Sketch remains an evolving subsystem rather than a complete constraint CAD
  editor; advanced profile/solver behavior needs more coverage.
- Topology-reference restoration is geometry-signature based and can be
  ambiguous after substantial topology changes.
- Snap candidate types and intersection coverage can be extended.
- Feature suppression and dependency visualization are not implemented.
- Feature-history reorder and automatic dependency graph tooling are not
  implemented.
- STEP/STL exchange and additional import/export formats are not implemented.
- Spatial indexing and more granular snap invalidation may be needed for much
  larger models.
- Multiple simultaneous section planes and true section capping workflows are
  future extensions; the current viewer supports one temporary clipping plane.

## Verification

```bash
./configure.sh
cmake --build build -j
ctest --test-dir build --output-on-failure
```

See [ARCHITECTURE.md](ARCHITECTURE.md), [PERFORMANCE.md](PERFORMANCE.md), and
[PCAD_FORMAT.md](PCAD_FORMAT.md) for implementation boundaries.
