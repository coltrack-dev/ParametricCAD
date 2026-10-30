# MCP-2C Test Report

## Scope

This report distinguishes tests executed through the real stdio MCP server
from ordinary unit and controller tests. No GUI automation or direct project
file mutation is used by the MCP integration test.

## Environment

- Platform: Linux x86_64
- Build: CMake/Ninja, C++20, Qt 6, OpenCASCADE
- MCP executable: `build/src/parametriccad-mcp`
- GUI executable: `build/src/ParametricCAD`
- Automation: explicitly enabled by the integration test with a private Qt
  local socket

## Implemented MCP-2C surface

The server advertises handlers for Boolean Fuse/Cut/Common, fillet, shell,
revolve, sweep, dependency-aware deletion, linear and path patterns, Sketch
arcs and constraints, and feature dependency inspection. Arguments are
validated in the GUI-thread request dispatcher and successful solid operations
perform OCCT validity and single-solid checks.

## Executed tests

### PASS — real MCP Fuse integration

`mcp_modeling_integration_tests` launched the built GUI application, connected
through `QLocalSocket`, launched the built MCP executable over stdio, and used
MCP calls to:

1. create a 100 x 60 x 6 mm plate;
2. create a face-attached annular pipe, outer radius 16 mm and inner radius
   12 mm, height 20 mm;
3. invoke `cad.boolean` with explicit left/right feature IDs and `Fuse`;
4. verify a valid one-solid result, approximately 100 x 60 x 26 mm bounds,
   and persistent Boolean dependencies;
5. execute `cad.undo` and `cad.redo`;
6. save and reopen the project;
7. modify an upstream dimension, recompute, and verify the downstream solid
   remains valid and its height changes.

This is a live application test, not a mock response test.

### PASS — build and regression suite

```text
cmake --build build -j                         PASS
ctest --test-dir build -R mcp_modeling_integration_tests --output-on-failure  PASS
ctest --test-dir build --output-on-failure      28/28 PASS
git diff --check                               PASS
```

The full CTest run completed in 74.23 seconds. Existing compiler warnings are
OpenCASCADE `std::iterator` deprecation warnings and do not fail the build.

## Not yet live-validated

Operation-specific real MCP scenarios for Cut, Common, fillet, shell, revolve,
sweep, linear pattern, path pattern, and the complete constraint edit/remove
cycle are not claimed as passed by this report. They require dedicated
fixtures and should be added before MCP-2C is considered complete.

## Recommendations for MCP-2C follow-up

- Add isolated live fixtures for each OCCT operation and assert exact expected
  topology/volume where the operation is deterministic.
- Add negative cases for ambiguous topology, dependency-blocked deletion, and
  multi-solid pattern results.
- Extend protocol tests to assert the complete `tools/list` MCP-2C inventory,
  schema rejection, disconnect/reconnect, and clean shutdown.
