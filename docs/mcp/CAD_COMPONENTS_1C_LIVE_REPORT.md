# CAD-Components-1C Live Integration Report

## Scope

This report covers the actual MCP stdio server connected to a running
`ParametricCAD` GUI process. The test used the QLocalSocket automation service;
it did not call controllers directly, edit `.pcad` files, or use mouse/keyboard
automation.

## Environment

- OS: Ubuntu 24.04, x86_64
- Build: CMake/Ninja, C++20, Qt 6.4.2, OCCT system packages
- Build tree: `build/`
- GUI: `build/src/ParametricCAD --enable-automation`
- MCP server: `build/src/parametriccad-mcp`
- Test: `tests/mcp_modeling_integration_tests`

## Executed command

```bash
cmake --build build -j
ctest --test-dir build -R mcp_modeling_integration_tests --output-on-failure
```

The targeted test passed on the final run:

```text
1/1 Test #10: mcp_modeling_integration_tests ... Passed   83.66 sec
100% tests passed, 0 tests failed out of 1
```

The test started the real GUI and MCP executable and verified 19 QtTest
scenarios. The CAD-Components-1C scenario was
`McpModelingIntegrationTests::componentOperationsThroughMcp`.

## CAD-Components-1C scenario

| Check | Result |
|---|---|
| `tools/list` discovers `cad.get_components` | PASS |
| `tools/list` discovers `cad.get_component` | PASS |
| `tools/list` discovers `cad.transform_component` | PASS |
| `tools/list` discovers `cad.split_component` | PASS |
| New project and rectangular sketch/extrude through MCP | PASS |
| One persistent component ID discovered | PASS |
| Component inspection and valid OCCT shape | PASS |
| Translate one component with `replace_result` | PASS |
| Unrelated source geometry remains addressed separately | PASS |
| Split transformed component into exactly two results | PASS |
| Both split results are valid single solids | PASS |
| Source replacement is technically hidden | PASS |
| Undo/Redo replacement visibility | PASS |
| Save and reopen | PASS |
| Reopened split result remains valid | PASS |
| Unknown component returns structured error | PASS |

The fixture used a 10 x 4 x 2 mm prism. After translation, its bounding box
started at X=20 mm. It was split by the plane X=25 mm, producing two returned
component IDs; each inspected result had `solidCount=1` and valid OCCT B-Rep.

## Implementation issue found and fixed

The first live attempt used an oriented finite box as the split half-space.
Although the operation returned through MCP, the plane-side construction did
not reliably produce two component results. It was replaced with OCCT's
`BRepPrimAPI_MakeHalfSpace` using an explicit planar face and reference point.
The final live test passed with this implementation.

The first test version also checked feature-level visibility for a component
replacement. That was incorrect: technical replacement visibility is
component-level. The API now reports `effectiveVisibility="hidden"` when the
persisted component is technically replaced, while preserving feature-level
visibility semantics.

## Not covered by this fixture

The test does not claim validation of the seven-wall house fixture, arbitrary
non-axis-aligned split planes, or upstream edits that change a component's
signature. Those require dedicated fixtures. Current signature mismatch is
reported as an explicit resolution error rather than silently remapped.

