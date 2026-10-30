# ParametricCAD MCP-2B test report

Date: 2026-10-16

## Scope

This report covers MCP-2B editing of the existing
`examples/mcp_mounting_plate.pcad`. The integration test starts the actual
`build/src/ParametricCAD` executable with `--enable-automation`, starts the
actual `build/src/parametriccad-mcp` executable, and sends JSON-RPC calls over
stdio and the application's QLocalSocket. It does not use a mock application,
direct controller calls, GUI input, or direct project-file edits.

Environment: Ubuntu 24.04, x86_64, Qt 6.4.2, C++20, OpenCASCADE, Ninja build.

## MCP calls exercised

The live scenario invoked:

`cad.open_project`, `cad.get_model_tree`, `cad.inspect_shape`,
`cad.get_sketch_entities`, `cad.chamfer`, `cad.recompute`, `cad.undo`,
`cad.redo`, `cad.save_project`, and `cad.open_project` again.

The MCP protocol test target covers JSON-RPC initialize/request shape and
invalid tool-name handling. Tool schemas for all MCP-1, MCP-2A, and MCP-2B
tools are served by `tools/list` in the real MCP executable.

## Live results

| Check | Result | Observed data |
|---|---|---|
| Open existing model through MCP | PASS | Four-feature project opened; no model recreation was used |
| Model tree | PASS | Outer Sketch, Extrude, face-attached hole Sketch, Pocket; stable IDs and dependencies retained |
| Initial B-Rep | PASS | Valid, one solid, volume `33436.460394670736 mm^3` |
| Sketch edit | PASS | Four rectangle corner line pairs changed to an eight-segment profile |
| Hole preservation | PASS | Downstream hole Sketch still contains five circle entities |
| Corner cut result | PASS | Valid B-Rep, one solid; volume `33136.460394670736 mm^3`, exactly `300 mm^3` below the original within test tolerance |
| Bounding box | PASS | `100 x 60 x 6 mm` within `0.1 mm` OCCT bounding-box tolerance |
| Downstream recompute | PASS | Face-attached Sketch and Pocket recomputed after the upstream profile edit |
| Undo / Redo | PASS | Undo restores the original volume; redo restores the 300 mm³-reduced volume |
| Persistence | PASS | Saved through `cad.save_project`, reopened through `cad.open_project`, then revalidated as one valid solid |

## Regression results

Commands run:

```text
cmake --build build -j
ctest --test-dir build -R mcp_modeling_integration_tests --output-on-failure
ctest --test-dir build --output-on-failure
```

Results:

- MCP-2B live integration: PASS.
- Full CTest: **28/28 passed**, including `mcp_protocol_tests` and the real
  `mcp_modeling_integration_tests`.
- Build: PASS. Only pre-existing OCCT deprecation warnings were emitted.

## Issue found and fixed during testing

The existing archived face-attached Sketch initially failed after an upstream
corner edit because OCCT rebuilt the support prism with changed face
orientation/topology. The resolver now has a narrow geometric recovery for
stable planar primitive support faces; ambiguous and missing references still
fail explicitly. This preserves the support face without bypassing the
dependency graph.

## MCP-2B limitations and MCP-2 recommendations

The current implementation supports Sketch line inspection/addition/editing/
removal, Sketch corner chamfering, generic referenced-edge chamfer requests,
stable face/edge topology enumeration, recompute, persistence, and Undo/Redo.
It does not add GUI automation or MCP-2 modeling operations such as arcs,
constraints authoring, revolve, sweep, shell, booleans, patterns, or feature
deletion. Those should be added only through existing controller commands with
the same structured validation and topology-reference rules.
