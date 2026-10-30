# CAD-Components-1C

CAD-Components-1C adds persistent operations on one independently addressable
`ModelComponent`. It does not replace `Body` or `ParametricFeature`, and it
does not create a project-wide `SplitAssembly` for local edits.

## MCP tools

### `cad.transform_component`

Transforms one component selected by its persistent `componentId`.

Arguments:

- `componentId`: stable component reference;
- `operation`: `translate`, `rotate`, or `mirror`;
- `mode`: `copy` or `replace_result`;
- operation-specific coordinates in world millimetres;
- `units`: currently `mm`.

`copy` leaves the source component visible. `replace_result` records a
replacement relationship, hides the source component technically, and presents
the new result. Unrelated components of the source feature are not transformed.

### `cad.split_component`

Splits one component with a plane described by `origin` and `normal`. The
operation returns exactly two component IDs only when both OCCT intersections
are valid single solids. A missing, ambiguous, degenerate, or invalid result
returns a structured MCP tool error and commits no feature.

The split uses `BRepPrimAPI_MakeHalfSpace` and OCCT Boolean Common. It does not
use viewport coordinates or a transient solid ordinal.

## Persistence and history

Component records persist their IDs, source feature, parent component,
visibility, bounds/volume signature, and replacement IDs. Component transform
and split features persist their source component signature and parameters.
Each operation is committed through `ModelTransaction` and one logical
Undo/Redo group. The original component remains in parametric history while
replacement results control the displayed geometry.

Legacy projects without component metadata continue to use resolver-generated
component records. Legacy `SplitAssembly` features are not reinterpreted.

## Limitations

Component resolution currently uses the persisted source-feature and geometry
signature (bounds and volume). If an upstream edit changes that signature so
that it cannot be resolved uniquely, the operation fails explicitly instead of
silently selecting another solid. A future resolver can add semantic topology
references without changing the MCP contract.

No modeling GUI automation is involved. All Qt/OCCT operations remain on the
application thread through the existing local IPC service.

