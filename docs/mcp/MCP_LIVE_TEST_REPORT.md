# ParametricCAD MCP-1 Live Test Report

Date: 2026-10-16

## Scope

This report covers live MCP-1 integration checks against a running
ParametricCAD process. Responses below came from the running application over
`QLocalSocket`; they were not mocked. MCP-2 mutation commands were not used.

## Environment

- OS: Ubuntu 24.04.5 LTS, Linux 7.0.0-38-generic, x86_64
- Build: C++20, Qt6, OpenCASCADE, CMake/Ninja
- MCP executable: `build/src/parametriccad-mcp`
- Application executable: `build/src/ParametricCAD`
- Codex registration: `/home/tatarino/.codex/config.toml`
- Registered command: `/home/tatarino/projects3/ParametricCAD/build/src/parametriccad-mcp`
- Automation was explicitly enabled with `--enable-automation` and
  `PARAMETRICCAD_AUTOMATION_SOCKET=parametriccad-live`.

## Live connection and discovery

The window title was:

`/home/tatarino/projects3/ParametricCAD/box4.pcad — ParametricCAD`

`cad.get_application_status` returned:

```json
{
  "application": "ParametricCAD",
  "automationEnabled": true,
  "dirty": false,
  "featureCount": 3,
  "pid": 29003,
  "thread": "7d353f3c9d00",
  "version": ""
}
```

The discovered tool set contained all seven MCP-1 tools:

| Tool | Result |
|---|---|
| `cad.get_model_tree` | PASS |
| `cad.get_feature` | PASS |
| `cad.inspect_shape` | PASS |
| `cad.get_selection` | PASS |
| `cad.get_view_state` | PASS |
| `cad.screenshot` | PASS: PNG produced |
| `cad.get_application_status` | PASS |

## Model inspection: `box4.pcad`

`cad.get_model_tree` returned three live features, with geometry revision 6:

| Feature | ID | Type | Dependencies | Recompute |
|---|---|---|---|---|
| Sketch | `sketch-8167e72f-5f2c-470d-8866-26815c6a34da` | Sketch | none | `up_to_date` |
| Extrude | `extrude-b5d9ebe1-313c-4e26-be8a-fc04832a010d` | Extrude | Sketch | `up_to_date` |
| Fillet | `fillet-e09c6feb-e299-4540-9ef0-49ab1cc5bb92` | Fillet | Extrude | `up_to_date` |

The final Fillet was inspected with both `cad.get_feature` and
`cad.inspect_shape`. Both returned the same values:

```text
valid:       true
shapeNull:   false
solidCount:  1
faceCount:   9
edgeCount:   38
volume:      11716.529906418207
boundingBox:
  min: [9.791068706487673, 20.25250720994938, -20.016529890507698]
  max: [42.914113319104814, 38.195094863738916, 0.016529890507696886]
```

The measured extents are approximately `33.1230 × 17.9426 × 20.0331`.
The Sketch entity coordinates span approximately `33.0900 × 17.9095`, and
the Extrude distance is 20 with Fillet radius 3. The small extent difference
is consistent with the filleted result and OCCT tolerances.

Result: **PASS** for live model tree, dependencies, recompute state and OCCT
shape inspection.

## Viewer inspection

`cad.get_view_state` returned a valid view with size `2495×1765`, display mode
1, scale `37.52189943637748`, and valid eye/center/up vectors.

`cad.get_selection` returned:

```json
{"items": []}
```

Result: **PASS** for view state and empty selection.

`cad.screenshot` returned `image/png`, `2495×1765`, with a nonempty payload.
The captured artifact is [box4-live.png](artifacts/box4-live.png).

The PNG is structurally valid, but visual inspection found an almost entirely
black viewer area with only the viewer toolbar visible. Result: **PASS** for
PNG generation/validation, **FAIL** for useful rendered content.

The application window was already large (`3247×1850`, viewer `2495×1765`).
An X11 resize attempt was ignored by the window manager, so the hypothesis
that an initial Qt resize/repaint is required remains plausible but was not
confirmed by this run. A manual resize or an explicit post-show viewer repaint
should be tested next.

## Empty project and persistence

An earlier live run against a newly started automation-enabled ParametricCAD
process returned `featureCount: 0`, an empty feature list and geometry revision
0. Result: **PASS** for empty-project inspection.

`box4.pcad` was opened using the existing ParametricCAD GUI workflow. The
report does not claim that opening or saving was performed through MCP: MCP-1
has no project open/save tools. The current live status reported `dirty:false`
and the window title identified `box4.pcad`.

## Failure and stability checks

| Check | Actual response | Result |
|---|---|---|
| Invalid feature ID | JSON-RPC `-32004`, `Feature not found`, feature ID data | PASS |
| Malformed tool arguments | JSON-RPC `-32602`, `featureId must be the only non-empty string argument` | PASS |
| Unknown tool | JSON-RPC `-32601`, `Unknown tool: cad.not_a_tool` | PASS |
| Malformed JSON | JSON-RPC `-32700`, `Invalid JSON` | PASS |
| Unavailable socket | JSON-RPC `-32001`, `ParametricCAD is not connected` | PASS |

The invalid-input checks did not modify the document and the running
application remained available for the live queries.

## Regression status

The existing CTest suite was previously run after the MCP implementation and
reported **27/27 tests passed**. This document is a live integration report;
those regression tests are not presented as live GUI tests or as mocked MCP
responses.

## Recommendations for MCP-2

1. Diagnose the black `CadViewer` screenshot after startup. Add an explicit
   post-show resize/repaint or wait for the viewer's OpenGL context before
   allowing screenshot capture, and add a regression test that checks rendered
   pixel content rather than only PNG validity.
2. Keep all future mutation commands behind the existing explicit automation
   flag and route them through the document/controller command path so Undo/Redo
   remains authoritative.
3. Add project lifecycle commands only after defining safe path validation,
   dirty-document handling and reconnect semantics.
4. For MCP-3 GUI automation, expose narrowly scoped, observable actions and
   preserve the GUI-thread requirement for all widget and viewer operations.
