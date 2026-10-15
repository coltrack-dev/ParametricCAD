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

## MCP-2A modeling tools

MCP-2A adds modeling commands without GUI automation. Every command is
forwarded to the GUI thread and uses the existing `ModelingController`,
`ProjectController`, feature dependency graph, and `QUndoStack`:

```text
cad.new_project       cad.open_project       cad.save_project
cad.create_sketch     cad.add_rectangle      cad.add_circle
cad.extrude           cad.pocket              cad.recompute
cad.edit_feature      cad.undo                cad.redo
```

Length arguments require `"units": "mm"`; the native ParametricCAD model
unit is millimeters. Project paths must be absolute `.pcad` paths. Creation
commands return stable feature IDs and structured errors on invalid input or
failed recompute.

`cad.create_sketch` accepts `support: XY`, `XZ`, or `YZ`, or a serialized
`TopologicalReference` for a planar face returned in `cad.get_feature` as
`topology.faces`. Face references are resolved semantically/signature-based;
ambiguous or missing references are rejected. No viewport coordinates are
used by MCP-2A modeling commands.

Example MCP-2A workflow:

```text
new_project
create_sketch(support=XY)
add_rectangle(120 x 80 mm)
extrude(30 mm)
get_feature(extrude) -> topology.faces
create_sketch(faceReference=<one planar face>)
add_rectangle(116 x 76 mm at 2,2 mm)
pocket(28 mm)
inspect_shape -> valid solid
save_project -> open_project -> edit_feature -> recompute -> undo/redo
```

## Example agent scenario

1. Initialize the MCP server and call `tools/list`.
2. Call `cad.get_model_tree`; select the last feature ID from the result.
3. Call `cad.get_feature` and verify its dependencies and `up_to_date` state.
4. Call `cad.inspect_shape` and check `valid`, `solidCount`, `volume`, and
   `boundingBox`.
5. Capture `cad.screenshot` and compare it with the visible model.
6. Close the GUI, verify a structured disconnect error, restart it with the
   same socket name, and repeat the status call.

## MCP-2B editing tools

MCP-2B adds safe editing of existing Sketches and topology inspection:

```text
cad.get_sketch_entities  cad.add_line       cad.edit_sketch_entity
cad.remove_sketch_entity cad.get_topology   cad.chamfer
```

Sketch corner chamfering accepts line-ID pairs and a distance. Four pairs on a
rectangle produce an eight-segment closed profile; circles and downstream
features are retained. The operation is a controller command, participates in
`QUndoStack`, recomputes dependants, and persists in `.pcad` archives. The
generic `cad.chamfer` edge form uses serialized `TopologicalReference` values;
viewport coordinates are not accepted as topology identity.

For primitive box-like supports, topology recovery also tolerates OCCT face
orientation changes and upstream corner edits by matching the stable planar
support geometry. Missing or ambiguous references remain structured errors.

## MCP-3 roadmap

MCP-3 is a separate future GUI automation surface. It may expose explicitly
scoped menus, panels, or pointer/keyboard actions, but must not be used for
modeling entity identity and must retain GUI-thread execution, argument
validation, local access restrictions, and clean disconnect behavior. MCP-2A
does not implement mouse clicking, keyboard simulation, widget trees, or
screenshot-based control.
