# CAD-Components-1B Live Integration Report

Date: 2026-10-17

## Scope

This report covers a live validation of the component presentation and visibility API against a separately started ParametricCAD GUI process. No mock application, direct `.pcad` editing, GUI automation, or modification of the original fixture was used.

Fixture: `examples/mcp_brick_house_split_test.pcad`  
Saved validation copy: `/tmp/mcp_components_house_live.pcad`  
Live socket: `/tmp/parametriccad-components-1b-live`

## Environment

- OS: Ubuntu 24.04.4, Linux 7.0.0-38-generic, x86_64
- Build: CMake/Ninja, Debug, C++ compiler `/usr/bin/c++`
- GUI: `build/src/ParametricCAD --enable-automation`
- MCP: `build/src/parametriccad-mcp` over stdio
- Automation: `PARAMETRICCAD_ENABLE_AUTOMATION=1`
- Live application PID: `86921`
- Loaded feature count: `245`

## Connection and protocol

`cad.get_application_status` returned `application=ParametricCAD`, `automationEnabled=true`, `dirty=false`, `featureCount=245`, and PID `86921`. This confirms that the responses below came from the live application.

`tools/list` returned 52 tools. The component-related tools were present: `cad.get_components`, `cad.get_component`, `cad.get_component_topology`, and `cad.set_component_visibility`.

The first live `cad.open_project` call returned `-32002 Timed out waiting for ParametricCAD`, although the GUI subsequently completed the load (`features: 245`). Reopening the saved copy had the same timeout behavior but completed asynchronously. This is recorded as a defect, not a pass.

## Results

| Scenario | Result | Actual observation |
|---|---|---|
| Live connection/status | PASS | Real GUI status returned, PID 86921, automation enabled. |
| Component discovery | PASS with limitation | Final `Split Assembly` feature `split-assembly-0da691ca-8f82-449b-beaf-a315b2c72959` returned 8 components; all 8 IDs were unique. Global discovery returned 451 components from all multi-solid features, not only the house assembly. |
| Component geometry | PASS | Assembly shape: non-null, valid OCCT B-Rep, 8 solids, 72 faces, 336 edges, volume `53041360000.00003`, bounds approximately X `0..13000`, Y `0..5000`, Z `0..2700`. |
| Component geometry comparison | PASS | All 8 component records returned non-null geometry, one solid each, stable bounds and volumes. |
| Whole-compound presentation count | SKIP | The live MCP API exposes no AIS presentation-count diagnostic. No numeric AIS count was claimed. |
| Duplicate whole-compound presentation | FAIL / not conclusively measurable | The screenshot was valid PNG but visually black except for the viewer toolbar, so rendered geometry could not be verified. The implementation path was exercised, but a live duplicate count cannot be asserted. |
| Hide one component | PASS | One wall component changed to `hidden`; `cad.get_component` returned `effectiveVisibility=hidden`, `visible=false`, while its geometry remained valid. |
| Restore visibility | PASS | The same component returned `effectiveVisibility=visible`, `visible=true`. |
| Ghost one component | PASS | The component returned `effectiveVisibility=ghosted`, `visible=true`. Selection exclusion was not independently verifiable because no selection-driving MCP tool exists. |
| Isolate one component | FAIL | Live call returned `isError=true`, `-32602`, `mode must be visible, hidden, or ghosted`. The requested isolate mode is not implemented by the current handler. |
| Component selection | SKIP | `cad.get_selection` was invoked and returned `items=[]`; there is no MCP operation to select a component without GUI interaction. |
| Face selection inside component | SKIP | No MCP selection-driving operation exists; no GUI automation was used. |
| Selection restoration after recompute | SKIP | Could not be executed without a selection-driving API. |
| Screenshot generation | PASS for transport, FAIL for rendering | `cad.screenshot` returned a nonempty PNG, 2495x1765, valid PNG signature. Visual inspection showed a black viewport with only the toolbar; geometry was not visible. Artifact: [`cad-components-1b-house-live.png`](artifacts/cad-components-1b-house-live.png). |
| Save validation copy | PASS with timeout defect | `/tmp/mcp_components_house_live.pcad` was created and nonempty (2,782,988 bytes), but the MCP request itself returned `-32002` before the GUI completed. |
| Reopen and component identity | PASS with timeout defect | After asynchronous load, the final assembly again returned 8 components with the same 8 IDs. The MCP request returned the same timeout during load. |
| Legacy project compatibility | PASS for tested fixture | The existing split-house `.pcad` loaded and recomputed with 245 features and the final assembly remained inspectable. |
| Invalid component reference | PASS | `cad.get_component` returned structured `-32004`, `Component could not be resolved`, with `data.componentId`. |
| Invalid visibility mode | PASS | Returned structured `isError=true`, `-32602`, with actionable message. |
| Disconnected application | PASS | After stopping the separate GUI process, status returned structured `-32001`, `Connection refused`. |
| Application stability after failures | PASS | The GUI stayed alive after invalid component/mode calls and completed asynchronous save/reopen. |
| Ambiguous component resolution | SKIP | No safe live fixture or MCP operation was available to manufacture ambiguity without modifying the production model. |
| Missing component after recompute | PARTIAL | Missing-reference behavior was tested with an invalid/superseded reference; a real recompute-induced disappearance was not manufactured. |

## Exact live calls

The live stdio server was invoked with `PARAMETRICCAD_AUTOMATION_SOCKET=/tmp/parametriccad-components-1b-live` and the following MCP methods were executed:

`tools/list`, `cad.get_application_status`, `cad.open_project`, `cad.get_components`, `cad.inspect_shape`, `cad.get_component`, `cad.set_component_visibility`, `cad.get_view_state`, `cad.get_selection`, `cad.screenshot`, and `cad.save_project`.

The server was started as:

```bash
DISPLAY=:0 QT_QPA_PLATFORM=xcb \
PARAMETRICCAD_ENABLE_AUTOMATION=1 \
PARAMETRICCAD_AUTOMATION_SOCKET=/tmp/parametriccad-components-1b-live \
build/src/ParametricCAD --enable-automation
```

The MCP client was run as:

```bash
PARAMETRICCAD_AUTOMATION_SOCKET=/tmp/parametriccad-components-1b-live \
build/src/parametriccad-mcp
```

## Regression suite

Before this live run, the full existing CTest suite was executed:

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Result: **PASS, 29/29 tests passed**. The live validation itself was not a mock/unit test and is reported separately above.

## Problems and recommendations

1. Fix the asynchronous IPC timeout contract for `cad.open_project` and `cad.save_project`. The GUI completes the operation, but the MCP caller receives `-32002` and cannot reliably know whether the operation committed.
2. Add a read-only `cad.get_presentation_state` diagnostic exposing component AIS count, whole-compound suppression, and effective visible component count.
3. Implement the documented `isolate` mode or remove it from the advertised behavior until supported.
4. Add a component selection request or a deterministic test hook that does not simulate mouse/keyboard input; otherwise selection scenarios remain untestable through MCP alone.
5. Make `cad.get_components` default to a clearly scoped assembly/result feature, or return explicit grouping by source feature. The current global result of 451 components is technically consistent but not practical for house-level inspection.
6. Investigate the black live screenshot. The PNG transport is correct, but the viewport geometry was not rendered in this run.

