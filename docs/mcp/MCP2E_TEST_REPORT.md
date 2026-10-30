# MCP-2E Test Report

Date: 2026-10-16  
Build: CMake/Ninja, C++20, Qt 6.4.2, OCCT on Linux x86_64

## Implemented scope

The MCP server now exposes `cad.mirror`, `cad.translate`, `cad.rotate`, and
`cad.transform_model`. Each operation creates a persistent `Transform` feature
with explicit source dependencies. `copy` and `replace_result` are explicit;
the latter hides sources in the same Undo/Redo transaction. `transform_history`
is documented but not implemented.

## Verification matrix

| Scenario | Result | Evidence |
|---|---|---|
| Tool discovery through MCP stdio | PASS | `mcp_modeling_integration_tests` checks all four names |
| Translation of a solid | PASS | B-Rep valid, one solid, dimensions and translated bbox checked |
| Mirror by arbitrary plane | PASS | B-Rep valid, one solid, volume preserved |
| Rotation around arbitrary axis | PASS | 90-degree rotation swaps non-symmetric XY dimensions |
| Multiple source features | PASS | Unit test verifies valid compound and summed volume |
| `replace_result` visibility | PASS | MCP integration checks one-step Undo/Redo |
| Dependency recompute | PASS | Upstream Extrude edit changes transformed volume |
| Save/reopen | PASS | Transform feature and result remain inspectable |
| OCCT validity | PASS | `BRepCheck_Analyzer` and MCP shape inspection |
| Invalid vectors/axes/arguments | PASS | MCP validation returns structured errors |
| Mirror of final Pocket preserves two through-openings | PASS | Live stdio fixture: valid one-solid B-Rep, unchanged volume, empty Common probe before/after Mirror |
| `replace_result` hides Pocket ancestors | PASS | Live fixture confirms both Pocket and wall Extrude are hidden while Transform result is visible |
| Upstream Extrude mirror is distinguished | PASS | Negative live case returns full-wall volume (20,000 mm³), unlike Pocket result (17,500 mm³) |

## Commands and results

```text
cmake --build build -j
```

PASS.

```text
ctest --test-dir build -R 'model_tests|mcp_modeling_integration_tests' --output-on-failure
```

PASS: 2/2 tests.

```text
ctest --test-dir build --output-on-failure
```

PASS: 28/28 tests.

The mirror regression test is part of `mcp_modeling_integration_tests`. It
uses the real ParametricCAD executable and the real `parametriccad-mcp` stdio
process over the local automation socket; no mock response or direct project
file editing is used.

## Root cause and fix

The regression had two cooperating causes. A caller could pass an upstream
Extrude together with its downstream Pocket; `TransformFeature::build()`
transformed every selected shape into one compound, so the full wall filled
the Pocket openings. Separately, `replace_result` hid only the explicitly
selected Pocket, leaving its visible upstream Extrude to overlap the result.

The fix keeps the explicit source dependency history, but emits only maximal
selected results and serializes `effectiveSourceIds`. `replace_result` now
hides selected sources and their upstream dependencies. No openings are
recreated and no solids are silently fused.

The MCP integration test launches the built ParametricCAD GUI process with
automation enabled, connects through its local socket, and invokes the actual
stdio `parametriccad-mcp` executable. It does not use a mock application or
direct controller calls for the MCP scenarios.
