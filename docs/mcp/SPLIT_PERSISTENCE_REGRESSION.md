# Split Persistence Regression

## Root cause

Split and component-transform parameters were already serialized by
`ProjectFile`, including the source component signature and replacement
relationships. The failure appeared after a loaded Split result was edited and
recomputed: `Body::recompute()` rebuilt the feature shape but did not refresh
the persisted `ModelComponent` bounding-box and volume signature.

`ComponentResolver` consequently compared the new geometry with the stale
signature. A moved component could no longer be resolved reliably after
recompute, even though the feature parameters and B-Rep were correct.

## Fix

`Body::finishIncrementalRecompute()` now refreshes component geometry metadata
after a successful recompute. The persistent component ID, parent/replacement
links, visibility and technical-hidden state are preserved. For an unambiguous
one-component/one-solid source the bounding box and volume are updated. Ambiguous
multi-solid mappings are left unchanged rather than silently remapping a
component to unrelated geometry.

This applies to both loaded and newly created component Split/Transform
features. Legacy SplitSegment/SplitAssembly features retain their existing
serialization and loading behavior.

## Regression coverage

The real `mcp_modeling_integration_tests` executable starts ParametricCAD,
connects through the actual stdio MCP server and verifies:

- component Split, negative/positive results and valid B-Reps;
- Translate of the negative result by `(0, -1000, 0)`;
- unchanged positive result;
- save/open and explicit recompute behavior;
- preserved component IDs and replacement visibility;
- editing a loaded transform followed by Undo/Redo;
- legacy SplitAssembly save/open and solid count.

The test uses temporary `.pcad` files and does not modify user projects.

Commands executed:

```bash
cmake --build build -j
ctest --test-dir build -R '^mcp_modeling_integration_tests$' --output-on-failure
ctest --test-dir build --output-on-failure
```

The focused MCP persistence test passed after the fix. Full CTest also passed:
29/29 tests, including MCP, project persistence, GUI and IFC regressions.
