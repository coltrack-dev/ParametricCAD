# Brick-house split-history recovery

## Scope

The damaged source used for recovery was `examples/mcp_brick_house.pcad`. It was
opened through the live MCP server and was never overwritten. The recovered
project was written to `examples/mcp_brick_house_recovered.pcad`.

## Surviving history

The source contained 29 features, including the intact whole-house result
`transform-499dda87-8e01-4bfc-862c-5d6ae7952408` and the following downstream
history:

- `component-split-negative-9794311e-9e5d-4a99-93ba-1586cb8e592b`;
- `component-split-positive-f8f7dbf6-3d4e-424c-a66e-e2b57e6fe165`;
- `component-transform-e1e59cfd-8093-416b-b7d1-aa2bae508e58`.

The original wall component at `Y=4620..5000` was technically hidden and
replaced by the split components. Its upstream Sketch, Extrude and Pocket
history was still present.

## Recovery operation

The new narrowly scoped `cad.recover_pre_split` MCP tool accepts a root feature
ID. It atomically removes only descendant `ComponentSplitFeature` and
`ComponentTransformFeature` objects, restores the parent component's
replacement metadata and technical visibility, then recomputes. If another
kind of downstream feature is encountered, recovery fails instead of deleting
unrelated history.

The operation uses `ModelTransaction`, `RemoveFeatureCommand` and
`ReplaceComponentsCommand`; it does not edit the `.pcad` archive or move a
segment back by an inverse translation.

## Live MCP validation

Commands were executed against a real GUI process and real stdio MCP server,
not mocks:

1. `cad.initialize` / `tools/list` — recovery tool and existing MCP inventory
   were returned.
2. `cad.open_project` on `examples/mcp_brick_house.pcad` — `featureCount=29`.
3. `cad.recover_pre_split` with root
   `transform-499dda87-8e01-4bfc-862c-5d6ae7952408` — success; `featureCount=26`.
4. `cad.get_components` — 7 components for the whole-house Transform; the
   wall component is visible and no longer technically hidden or replaced.
5. `cad.inspect_shape` — `valid=true`, `shapeNull=false`, `solidCount=7`,
   `faceCount=66`, `edgeCount=312`, volume
   `53041360000.00002 mm^3`, bounding box approximately
   `[0,0,0]..[13000,5000,2700] mm`.
6. `cad.recompute` — success.
7. `cad.save_project` — saved as `examples/mcp_brick_house_recovered.pcad`.
8. A fresh ParametricCAD process reopened the saved file through MCP. The same
   feature count, valid B-Rep, seven solids, volume, bounding box and component
   metadata were observed after explicit recompute.

## Result

**PASS.** The pre-Split parametric state was recovered from surviving history,
saved under a new filename, reopened in a fresh process and recomputed without
duplicating or deleting the upstream wall/opening history.

## Limitation

The current MCP inspection reports the final Transform as a compound of seven
wall/floor solids, so `solidCount=7` is expected; it is not a single-solid
house union. No claim is made that the original user file was repaired in
place.
