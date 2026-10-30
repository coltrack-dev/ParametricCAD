# MCP-2C Full Integration Validation Report

## Test boundary

All operation scenarios below used the actual `parametriccad-mcp` executable
over stdio and a freshly launched real `ParametricCAD` GUI process with
automation enabled. No mock application, direct controller calls, direct
`.pcad` edits, mouse input, or keyboard input was used.

## Environment and commands

- Linux x86_64, Ubuntu 24.04 environment
- C++20, CMake/Ninja, Qt 6.4.2, OpenCASCADE
- GUI: `build/src/ParametricCAD`
- MCP server: `build/src/parametriccad-mcp`
- Private `QLocalSocket` name per test process

Executed commands:

```bash
cmake --build build -j
ctest --test-dir build -R mcp_modeling_integration_tests --output-on-failure
ctest --test-dir build --output-on-failure
```

Results:

- isolated live MCP integration: PASS, 1/1, 10.43 seconds;
- full CTest: PASS, 28/28, 75.52 seconds.

## Protocol validation

| Scenario | Result | Evidence |
|---|---:|---|
| `initialize` with protocol version `2025-06-18` | PASS | Negotiated response returned the same version |
| Complete `tools/list` inventory | PASS | All MCP-2C tools were present |
| Invalid tool name | PASS | JSON-RPC `-32601` remained a protocol error |
| Malformed tool arguments | PASS | MCP result used `isError: true`, structured error retained |
| Tool execution failure | PASS | `isError: true`, actionable structured error |
| Application disconnect | PASS | MCP tool result `isError: true`, code `-32001` |
| Application reconnect | PASS | New GUI process accepted MCP calls after restart |
| Clean shutdown | PASS | QTest cleanup terminated the GUI without hang |

The adapter now distinguishes JSON-RPC protocol errors from tool execution
errors: unknown MCP methods/tools return JSON-RPC `error`, while errors from
the connected ParametricCAD tool execution are returned as a normal MCP
`result` with `isError: true` and `structuredContent.error`.

## Operation matrix

All rows are live MCP calls. “Dependencies” means `cad.get_feature_dependencies`
was checked for the source feature IDs.

| Operation | Fixture and measured result | Dependencies | Undo/Redo | Save/reopen | Result |
|---|---|---:|---:|---:|---:|
| Boolean Cut, overlap | 20 x 20 x 10 mm minus overlapping 10 x 10 x 10 mm; one solid; volume 3000 mm³ | PASS | PASS | PASS | PASS |
| Boolean Common, overlap | Common region 10 x 10 x 10 mm; one solid; volume 1000 mm³ | PASS | covered by same live fixture | covered by same fixture | PASS |
| Boolean Cut, contained | 20 x 20 x 10 mm minus contained 5 x 5 x 10 mm; volume 3750 mm³ | PASS | covered | covered | PASS |
| Boolean Common, disjoint | Rejected as invalid one-solid result; no corrupted feature committed | n/a | n/a | n/a | PASS |
| Fillet | Box 20 x 20 x 10 mm, one referenced edge, radius 1 mm; valid one solid | PASS | PASS | PASS | PASS |
| Chamfer | Same box and edge, distance 1 mm; valid one solid | PASS | covered | PASS | PASS |
| Shell | Box 20 x 20 x 10 mm, top face removed, thickness 1 mm; valid one solid; measured bounds about 22.0223 x 22.0223 x 11.0223 mm; volume 1296.3422 mm³ | PASS | PASS | PASS | PASS |
| Revolve | XZ rectangle 10 x 10 mm revolved around GlobalZ; one solid; bounds 40 x 40 x 10 mm; volume about 9424.778 mm³ | PASS | PASS | PASS | PASS |
| Sweep | 4 x 4 mm profile along 30 mm path; one solid; bounds about 30 x 4 x 4 mm; volume 480 mm³ | PASS | PASS | PASS | PASS |
| Linear Pattern | 5 x 5 x 5 mm source, count 3, spacing 10 mm; valid compound with 3 solids; X extent 25 mm; volume 375 mm³ | PASS | PASS | PASS | PASS |
| Path Pattern | Same source along 30 mm Sketch path, count 3; valid compound with 3 solids; volume 375 mm³ | PASS | covered | PASS | PASS |
| Sketch Arc | Three-point arc created and returned by `cad.get_sketch_entities` | Sketch fixture | covered | not required | PASS |
| Constraint lifecycle | Distance constraint created, edited 20→25, removed, then Undo/Redo verified | Sketch fixture | PASS | not required | PASS |
| Dependency deletion | Sketch→Extrude chain deleted through `cad.delete_feature`; both dependent features removed; Undo/Redo restored them | PASS | PASS | not required | PASS |

Patterns were explicitly validated as multi-solid compounds; the single-solid
assertion was not applied to those operations.

## Invalid-operation safety

- Fillet with an excessive radius: structured `isError`, feature count
  unchanged.
- Shell with an empty opening-face list: structured `isError`, feature count
  unchanged.
- Sweep using the same Sketch as profile and path: structured `isError`, no
  corrupted feature committed.
- Invalid Boolean Common on disjoint solids: structured `isError` after the
  single-solid result check; the failed operation was undone by the adapter.
- Unknown feature IDs and malformed required arguments: structured errors;
  application remained responsive.

## Bugs found and fixed during validation

1. MCP adapter propagated GUI tool failures as JSON-RPC errors. Fixed by
   returning MCP `result.isError=true` for tool execution/IPC failures while
   preserving JSON-RPC errors for unknown methods/tools.
2. The live Revolve fixture initially used a profile plane normal to the
   GlobalZ axis, producing a valid but zero-volume degenerate result. The
   fixture was corrected to XZ, where the axis lies in the Sketch plane.
3. The live Shell result intentionally offsets outward for positive
   thickness; its measured bounds and volume are recorded above instead of
   incorrectly imposing the source box bounds.
4. The integration fixture initially used point-array arguments for
   `cad.add_line`; the published schema requires `x1`, `y1`, `x2`, `y2`.

No unexecuted scenario is marked PASS in this report.
