# MCP-2E Spatial Transformations

ParametricCAD exposes spatial transformations as persistent parametric
features. They operate on explicitly supplied feature IDs and do not infer or
transform every intermediate result in a dependency chain.

## Tools

- `cad.mirror`: `origin`, `normal`, `featureIds`, `mode`, `units: "mm"`.
- `cad.translate`: `vector`, `featureIds`, `mode`, `units: "mm"`.
- `cad.rotate`: `axisOrigin`, `axisDirection`, `angleDegrees`, `featureIds`,
  `mode`, `units: "mm"`.
- `cad.transform_model`: the same schema with an explicit `operation` of
  `mirror`, `translate`, or `rotate`.

`mode` is either `copy` or `replace_result`. Both modes create a persistent
`Transform` feature with dependencies on the selected source features.
`replace_result` additionally hides the source features as one Undo/Redo
operation; it does not delete or rewrite their history. Multiple sources are
returned as a compound and are not incorrectly required to be one solid.

Invalid feature IDs, zero-length directions, malformed vectors, and invalid
angles are rejected before a model command is committed.

## History transformation is intentionally not implemented

The future `transform_history` mode must transform construction history rather
than only a resulting B-Rep. Before it can be implemented, the design must
define how to transform:

- Sketch coordinate frames and all sketch entities/constraints;
- Extrude and Pocket directions;
- face-attached Sketch support planes;
- Revolve axes and Sweep paths;
- persistent `TopologyReference` values and their remapping after topology
  changes;
- dependency ordering and references across transformed branches.

Until those rules are complete, MCP-2E transforms the selected feature result
only. A transformed Sketch is therefore not advertised as an editable,
history-transformed construction frame.

## Verification

The real MCP stdio integration test covers tool discovery, translation,
mirroring, rotation, `replace_result` visibility Undo/Redo, dependencies,
save/reopen, upstream extrusion editing, and recompute. Unit tests additionally
cover OCCT validity, multiple-source compounds, mirror/rotation volume
preservation, and Transform serialization.
