# IFC Component Performance Report

Date: 2026-10-17  
Environment: Ubuntu 24.04, Qt 6, OpenCASCADE, C++20, existing `build` configuration,
X11 display `:0`, real ParametricCAD GUI and real `parametriccad-mcp` stdio server.

## Implementation

Added two MCP tools:

- `cad.import_ifc`: validates an absolute existing `.ifc` path and optional
  `replaceCurrent` flag, starts import asynchronously, and returns an operation ID.
- `cad.get_operation_status`: returns `running`, `completed`, or `failed` and the
  measured import phases.

The background phase uses the existing `IfcImporter::prepare()` and IfcOpenShell
adapter. Feature creation, insertion through `ModelingController::importFeatures()`,
and presentation refresh remain on the GUI thread. Import is one existing undo
command and uses the existing bulk presentation refresh. No GUI automation or direct
`.pcad` editing was used.

`cad.inspect_model` now distinguishes the final `Body` result from component/history
metadata:

- `featureCount`: number of parametric features.
- `modelComponentCount`: persisted component count, or solid components recovered
  from legacy/imported feature results when metadata is absent.
- `visibleGeometricSolidCount`, `totalVisibleGeometricVolume`, and
  `visibleBoundingBox`: metrics of the final `Body::shape()` result only.
- `overlapWarnings`: bounded heuristic warnings for intersecting solid bounding
  boxes; it does not claim a Boolean overlap without a Boolean test.
- `volumePolicy`: explicitly states that `BRepGProp::VolumeProperties` is applied
  to the final result and intermediate feature/component volumes are not summed.

## Why the previous values differed

`cad.inspect_shape` is a feature-level query. When called for
`transform-499dda87-8e01-4bfc-862c-5d6ae7952408`, it reports that TransformFeature's
own compound: 7 solids, 66 faces, 312 edges and volume approximately
`53,041,360,000 mm³`.

`cad.inspect_model` is a document-level query. It reports the current final
`Body::shape()`, which in that project is a later single-solid result with volume
approximately `17,653,075.2 mm³`. These are different feature states, not two views
of the same physical result. Summing all feature or component volumes would count
intermediate history and overlapping presentations multiple times, so the aggregate
inspection intentionally uses only the final Body result.

## Live MCP benchmark results

All values below came from actual MCP calls to the running GUI. The import call
returned immediately with `accepted: true`; the result was obtained by polling the
returned operation ID.

| Model | Schema | Considered | Imported | Skipped | Failed | Parse | Geometry | Feature construction | Bulk insertion | Presentation | Importer total | End-to-end |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `Duplex_A_20110505.ifc` | IFC2X3 | 286 | 215 | 71 | 0 | 59 ms | 1,620 ms | 3 ms | 0 ms | 1,374 ms | 1,689 ms | 3,079 ms |
| `BuildingBIMModel.ifc` | IFC4 | 5,832 | 5,586 | 246 | 0 | 499 ms | 30,284 ms | 30 ms | 2 ms | 57,070 ms | 30,792 ms | 88,037 ms |

The large-model operation stayed `running` while background IFC geometry was being
prepared. During that period status requests were accepted until the final GUI
presentation refresh, which is necessarily GUI-thread work. The measured live
presentation log for the large import was:

```text
features=5586
AISCreated=7112
AISReused=0
viewerUpdates=1
redraws=1
resolverMs=310
resolvedComponents=1948
presentation totalMs=55685
```

The later saved-project reopen measured 22,008 ms for parse/deserialize/recompute and
6,300 ms for presentation in the same environment. This confirms that IFC import
itself and persistence reopen have separate costs.

Peak memory was not instrumented in this environment, so it is **NOT MEASURED** and
is not claimed as a passing benchmark criterion.

## Validation

Live calls executed:

- `tools/list`
- `cad.import_ifc` for both IFC files
- `cad.get_operation_status` during and after both imports
- `cad.inspect_model`
- `cad.save_project`
- `cad.open_project` on `/tmp/building_bim_mcp_import.pcad`
- `cad.get_model_tree` after reopen
- `cad.get_feature` for an imported IFC feature

Observed validation:

- Duplex aggregate result: valid, one solid, volume `17,653,075.2 mm³`, no invalid
  features or overlap warnings.
- BuildingBIM aggregate result after import/reopen: valid shape, no invalid features,
  5,586 features, geometric volume `5,452,109.14 mm³`; the final result is a shell/
  compound-like shape with `solidCount: 0`, so no single-solid invariant is imposed.
- Reopened IFC feature retained `type: IfcImported`, IFC GlobalId, entity type,
  source file, building/storey IDs and property/quantity metadata.
- The saved `.pcad` was reopened successfully through MCP.
- Fresh import used 5,164 feature AIS objects and 1,948 component AIS objects for
  the large model, with one final viewer update and redraw.

## Correctness and limitations

The result is a real IFC performance PASS for parsing, geometry preparation,
controller insertion and presentation timing. It is not a claim that large IFC
presentation is fast: `BuildingBIMModel.ifc` still spends about 57 seconds creating
and presenting 7,112 AIS objects. The previous ComponentResolver optimization keeps
resolver work low (`310 ms`) and avoids a quadratic resolver pattern, but AIS object
creation and OCCT presentation remain the dominant bottleneck.

BIM Navigator/Inspector metadata paths were exercised indirectly by the persisted
`IfcImported` feature data and existing IFC CTest coverage. A separate pixel-level
GUI inspection of every BIM panel was not automated through MCP.

## Commands

```sh
cmake --build build -j
ctest --test-dir build --output-on-failure
git diff --check
```

The final full CTest run for this change is reported separately after completion.
