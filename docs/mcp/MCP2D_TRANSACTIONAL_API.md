# MCP-2D Transactional Modeling API

MCP-2D adds atomic model editing to the existing stdio/QLocalSocket MCP
service. All changes are executed on the ParametricCAD GUI thread through
`ModelingController`; no GUI automation or direct `.pcad` mutation is used.

## Tools

### `cad.edit_sketch_profile`

Replaces one ordered closed line profile in a single Undo step.

```json
{
  "sketchId": "sketch-...",
  "segments": [
    {"x1": 0, "y1": 0, "x2": 100, "y2": 0},
    {"x1": 100, "y1": 0, "x2": 100, "y2": 60},
    {"x1": 100, "y1": 60, "x2": 0, "y2": 60},
    {"x1": 0, "y1": 60, "x2": 0, "y2": 0}
  ],
  "units": "mm"
}
```

The server rejects non-finite and zero-length segments, open chains,
self-intersections and branching profiles before changing the model. Existing
line IDs are preserved by position where possible. Downstream features are
recomputed only after the complete replacement is staged.

### `cad.batch`

Runs an allow-listed sequence as one logical Undo command. IDs from earlier
results can be referenced as `$0.featureId`, `$1.featureId`, and so on.

Eligible tools are `cad.create_sketch`, `cad.add_rectangle`, `cad.add_line`,
`cad.add_circle`, `cad.extrude`, `cad.pocket`, `cad.edit_feature`, and
`cad.delete_feature`. Nested batches and read-only or project I/O tools are
rejected. A failure explicitly undoes every command added since the transaction
started.

### `cad.find_topology`

Filters stable topology references by feature, kind, surface/curve type,
normal, approximate location, area, length and tolerance. Zero or multiple
matches produce a structured error; the server never silently chooses an
ambiguous reference.

### `cad.inspect_model`

Returns feature health, failed features, open Sketch profiles, aggregate shape
validity, solid/component counts, volume, bounding box, recompute error and
geometry revision. It does not impose a single-solid invariant on a project.

## Transaction behavior

`ModelTransaction` records the QUndoStack boundary, opens a logical macro,
recomputes the candidate state, and commits only after successful recompute.
Rollback closes the macro and explicitly undoes all commands after the saved
stack boundary, then restores a recomputed model. This is intentionally more
strict than relying on `QUndoStack` macros alone.

## Verification

The real MCP integration test covers profile replacement, preservation of line
IDs, dependent Extrude recompute, invalid-profile rollback, batch ID
substitution, one-step batch Undo/Redo, failed-batch rollback, aggregate model
inspection and ambiguous topology errors.

```bash
cmake --build build -j
ctest --test-dir build -R mcp_modeling_integration_tests --output-on-failure
```

Project mirroring, room detection and door/window creation are intentionally
outside MCP-2D.
