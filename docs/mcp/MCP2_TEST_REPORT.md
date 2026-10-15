# ParametricCAD MCP-2A Test Report

Date: 2026-10-16

## Scope

This report covers MCP-2A parametric modeling through the actual MCP stdio
server and a running ParametricCAD process. The test does not call
`ModelingController` directly. It starts `ParametricCAD`, connects to its
enabled local IPC endpoint, starts `parametriccad-mcp`, and sends JSON-RPC
`tools/call` requests over stdio.

MCP-2B and MCP-3 were not implemented.

## Implemented tools

- `cad.new_project`
- `cad.open_project`
- `cad.save_project`
- `cad.create_sketch`
- `cad.add_rectangle`
- `cad.add_circle`
- `cad.extrude`
- `cad.pocket`
- `cad.recompute`
- `cad.edit_feature`
- `cad.undo`
- `cad.redo`

All length-oriented tools require `units: "mm"`. Project paths must be
absolute `.pcad` paths. Modeling operations are dispatched on the GUI thread
and use the existing controller and command paths.

## Actual MCP integration scenario

The test `mcp_modeling_integration_tests` performed this sequence through MCP:

1. Start a new project.
2. Create a global XY sketch.
3. Add a 120 × 80 mm rectangle.
4. Extrude it 30 mm.
5. Retrieve the extrude's serialized topology face references.
6. Create a face-attached sketch using a `TopologicalReference`.
7. Add a 116 × 76 mm rectangle at `(2,2)` mm.
8. Pocket it to 28 mm.
9. Inspect the resulting shape with OCCT and assert a valid single solid with
   positive volume.
10. Save and reopen the `.pcad` project.
11. Edit the upstream Extrude distance to 32 mm.
12. Recompute and assert the downstream Pocket volume increases.
13. Undo and assert the volume returns below the edited state.
14. Redo and assert the edited volume is restored.
15. Test malformed arguments and an invalid feature ID.

The generated parametric example is
[mcp2_electronics_enclosure.pcad](../../examples/mcp2_electronics_enclosure.pcad).
It was produced by the same actual MCP workflow, not by directly constructing
or editing a Body in the test.

## Results

| Check | Result |
|---|---|
| MCP stdio server starts | PASS |
| ParametricCAD live IPC connection | PASS |
| New project | PASS |
| Global sketch creation | PASS |
| Rectangle entities | PASS |
| Extrude | PASS |
| Persistent face-reference discovery | PASS |
| Face-attached sketch | PASS |
| Pocket | PASS |
| OCCT shape validity / solid count / positive volume | PASS |
| Save `.pcad` | PASS |
| Open saved `.pcad` | PASS |
| Upstream edit and downstream recompute | PASS |
| Undo state restoration | PASS |
| Redo state restoration | PASS |
| Structured invalid-argument error | PASS |
| Structured invalid-feature error | PASS |

## Build and regression verification

Commands run:

```text
cmake -S . -B build -G Ninja
cmake --build build -j
ctest --test-dir build --output-on-failure
git diff --check
```

Final result: **28/28 CTest tests passed**. The new actual MCP integration test
is included in that count.

## Architecture and safety notes

- No mouse, keyboard, widget-tree, dialog, or screenshot-based modeling
  automation was added.
- `cad.create_sketch` face attachment uses serialized topology references and
  rejects missing/ambiguous/unresolvable references.
- Feature creation/editing uses `ModelingController` methods and
  `QUndoStack`; the MCP layer does not mutate `Body` directly.
- Project lifecycle uses `ProjectController` and native `.pcad` persistence.
- Automation remains disabled unless explicitly enabled at application start.

## Follow-up recommendations

MCP-2B should add one operation at a time with actual MCP tests for topology
references and recompute dependencies. Constraint editing should expose stable
entity/constraint IDs. Boolean, fillet, chamfer, shell, revolve, sweep, and
pattern commands should be added only after their controller command and
persistence behavior is covered by the same end-to-end test style.
