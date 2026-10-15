# ParametricCAD MCP automation

Phase MCP-1 provides read-only inspection of a running ParametricCAD process.
The GUI application owns all model, OCCT, and widget access. `parametriccad-mcp`
is a stdio MCP server and forwards tool calls through a local Qt socket.

## Startup

Start the GUI with automation explicitly enabled:

```sh
PARAMETRICCAD_AUTOMATION_SOCKET=parametriccad-demo \
  ./build/src/ParametricCAD --enable-automation
```

Then configure an MCP client to launch:

```json
{
  "mcpServers": {
    "parametriccad": {
      "command": "/absolute/path/to/build/src/parametriccad-mcp",
      "env": { "PARAMETRICCAD_AUTOMATION_SOCKET": "parametriccad-demo" }
    }
  }
}
```

Automation is disabled unless `--enable-automation` (or the equivalent
`PARAMETRICCAD_ENABLE_AUTOMATION=1`) is present. The socket uses Qt's
`UserAccessOption`; choose a unique name when multiple sessions are running.

## MCP-1 tools

`cad.get_model_tree`, `cad.get_feature`, and `cad.inspect_shape` expose stable
feature IDs, types, dependencies, recompute state, OCCT validity, topology
counts, volume, and bounds. `cad.get_selection`, `cad.get_view_state`,
`cad.screenshot`, and `cad.get_application_status` expose current GUI state.
There are no modeling mutation tools in MCP-1.

## Example agent scenario

1. Initialize the MCP server and call `tools/list`.
2. Call `cad.get_model_tree`; select the last feature ID from the result.
3. Call `cad.get_feature` and verify its dependencies and `up_to_date` state.
4. Call `cad.inspect_shape` and check `valid`, `solidCount`, `volume`, and
   `boundingBox`.
5. Capture `cad.screenshot` and compare it with the visible model.
6. Close the GUI, verify a structured disconnect error, restart it with the
   same socket name, and repeat the status call.

## Future phases

MCP-2 may add modeling commands only through `ModelingController` and its
`QUndoStack` command paths; direct Body mutation is out of scope. MCP-3 may
add GUI automation through a separate, explicitly enabled command surface for
menus, panels, and pointer/keyboard events. Both phases must retain GUI-thread
execution, argument validation, local access restrictions, and clean
disconnect behavior.
