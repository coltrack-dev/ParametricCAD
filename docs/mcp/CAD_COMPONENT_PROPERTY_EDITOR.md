# Component Property Editor

The Feature Editor now exposes component-producing parametric features instead of
showing the generic history placeholder.

## Displayed information

`Component Transform` and `Component Split` show the parent feature, stable
component ID, direct dependencies, the first upstream Sketch (when present),
operation/result mode, effective visibility, bounding box, volume, OCCT B-Rep
validity and recompute status.

## Editable parameters

- Component Translate: X, Y and Z, using one `QUndoStack` command per edit.
- Component Split: plane origin, plane normal and negative/positive segment side.

All edits go through `ModelingController::setFeatureProperty`. The controller
validates the property and the command recomputes the model before committing.
Undo and Redo use the same command path. A zero-length split normal is rejected
without changing the feature.

Legacy component transforms store only an OCCT transformation matrix. Therefore
the semantic axis/angle for Rotate and origin/normal for Mirror cannot be
reconstructed reliably from those old records. They are displayed read-only with
an explanation to recreate the operation; no B-Rep is mutated directly.

## Verification

The Qt regression test `undo_panel_tests` covers:

- Translate editing through the panel, Undo and Redo;
- persistence through save/reopen;
- Split side editing through the panel, Undo and Redo;
- rejection of an invalid split normal.

The complete CTest suite was run with:

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

GUI component editing tests use an offscreen Qt application and actual
`ModelingController`/`QUndoStack` operations; they are not mock property
responses. The currently running GUI process must be restarted after rebuilding
to load this editor implementation.
