# Presentation Pipeline Performance Report

Date: 2026-10-17  
Environment: Ubuntu 24.04, Qt 6, OpenCASCADE, C++20, existing `build` configuration,
X11 display `:0`.

## Summary

The measured regression was in `ModelPresenter::refreshModel()`. Every feature was
enumerated as a component candidate, and each candidate was resolved again. Both
paths recalculated OCCT bounding boxes and volume properties. For the 245-feature
house this produced repeated B-Rep traversal and mass-property work on the GUI
thread.

The fix resolves all solids of one feature in one pass and calculates metrics once
per candidate. Single-solid features are sent directly to the existing feature AIS
presentation path. Compound features use the resolved component AIS presentations;
the whole compound is not displayed alongside its components.

## Before/after measurements

Measurements used the same source files and the real GUI plus real stdio MCP server.
The before values are from the timeout live run recorded in
`docs/mcp/MCP_TIMEOUT_LIVE_TEST_REPORT.md`; the after values were collected after
the optimization with `PARAMETRIC_CAD_PERF=1`.

| Model | Before open | After open | After recompute/load | After presentation | Result |
|---|---:|---:|---:|---:|---|
| `box4.pcad` | 176 ms | 173 ms | 11 ms | 67 ms | PASS |
| `examples/mcp_brick_house_split_test.pcad` | 22,494 ms | 988 ms | 341 ms | 388 ms | PASS |

The house presentation refresh therefore dropped from approximately 22.1 seconds
to 388 ms, and end-to-end MCP open dropped by approximately 95.6%.

Final live house presentation counters:

| Counter | Value |
|---|---:|
| Features in model | 245 |
| Feature AIS presentations | 182 |
| Component AIS presentations | 338 |
| AIS objects created during refresh | 520 |
| AIS objects reused during refresh | 0 |
| Viewer updates | 1 |
| View redraws | 1 |
| Component resolver calls | 238 |
| Resolved components | 338 |
| Resolver time | 63 ms |
| FitAll | 246 ms |

The zero reuse count is expected for a fresh project load. Repeated refreshes use
the existing `AIS_Shape` maps and are counted as reuse; no per-feature benchmark
logging was added.

## Live MCP validation

Commands used against a freshly launched application:

```sh
PARAMETRIC_CAD_PERF=1 PARAMETRICCAD_ENABLE_AUTOMATION=1 \
PARAMETRICCAD_AUTOMATION_SOCKET=/tmp/parametriccad-perf-final \
build/src/ParametricCAD --enable-automation

PARAMETRICCAD_AUTOMATION_SOCKET=/tmp/parametriccad-perf-final \
build/src/parametriccad-mcp
```

The live MCP calls included:

- `initialize`
- `tools/list`
- `cad.open_project`
- `cad.get_application_status`
- `cad.get_model_tree`
- `cad.inspect_model`
- `cad.get_components`
- `cad.inspect_shape`
- `cad.get_view_state`
- `cad.get_selection`
- `cad.screenshot`
- invalid `cad.inspect_shape` with `missing-feature`

Observed live results:

- `cad.open_project` returned `featureCount: 245` for the house.
- `cad.inspect_model` returned `valid: true`, `solidCount: 1`, no invalid features,
  and volume `17653075.20000017` for the final result.
- `cad.inspect_shape` returned a valid shape with `solidCount: 7`, `faceCount: 66`,
  `edgeCount: 312`, and volume `53041360000.00002` for the selected transform
  feature.
- `cad.get_view_state` returned a valid 1445 x 1110 viewport.
- `cad.get_selection` returned an empty selection without error.
- `cad.screenshot` returned a nonempty PNG beginning with the PNG signature
  (`iVBORw0KGgo`).
- The invalid feature returned structured MCP tool error `-32004`, without a GUI
  crash or hang.

These are live responses from the running application, not mocks.

## Complexity audit

Before the change, the effective work for a compound feature was approximately:

1. enumerate solids and calculate metrics;
2. resolve each generated component separately;
3. recalculate metrics and scan the same candidate list for every component.

The implementation now performs one candidate extraction and one metric calculation
per solid for each feature. Resolution remains linear in the number of persisted
components and candidate solids for that feature. Presentation still performs one
bulk refresh, one selection activation, one viewer update and one redraw.

## Source changes

- `src/model/ModelComponent.h/.cpp`: added `ComponentResolver::resolveFeature()`
  and removed repeated per-component metric resolution from the presentation path.
- `src/viewer/ModelPresenter.cpp`: uses the bulk feature resolver for compound
  features and records aggregate resolver/presentation timings under
  `PARAMETRIC_CAD_PERF`.
- `src/viewer/CadViewer.h/.cpp`: exposes presentation counts and records aggregate
  AIS creation/reuse, viewer update and redraw counters during bulk refresh.

No geometry algorithms, project format, controller commands, or modeling behavior
were changed.

## IFC status

`examples/ifc/Duplex_A_20110505.ifc` and `examples/ifc/BuildingBIMModel.ifc` are
available and the existing IFC import acceptance tests passed. The current MCP
`cad.open_project` contract accepts `.pcad` paths only, so a live IFC import could
not be benchmarked through MCP without using the GUI import workflow or adding a
new MCP tool. It is therefore explicitly **SKIP**, not a performance pass.

## Regression and correctness

Executed:

```sh
cmake --build build -j
ctest --test-dir build --output-on-failure
git diff --check
```

Results:

- Build: PASS.
- CTest: **29/29 passed**, 0 failed, total test time 147.52 s.
- Existing MCP stdio integration test: PASS (`mcp_modeling_integration_tests`).
- Existing IFC import/building acceptance tests: PASS.
- Live house model B-Rep inspection: PASS.
- Live viewport state and PNG screenshot: PASS.

## Remaining limitations

- IFC presentation benchmarking remains pending because MCP has no IFC import
  operation and `cad.open_project` rejects non-`.pcad` paths.
- Reuse counters are meaningful across refreshes within one application session;
  fresh project load necessarily creates the initial AIS objects.
- The profile is aggregate by design. It does not identify individual expensive
  imported features, avoiding benchmark log volume and timing distortion.

## Recommendation for MCP-2

Keep modeling operations serialized on the GUI thread and expose operation progress
or status for large IFC/project workflows. An explicit IFC import operation could
then be benchmarked and monitored without conflating GUI menu automation with the
CAD API.
