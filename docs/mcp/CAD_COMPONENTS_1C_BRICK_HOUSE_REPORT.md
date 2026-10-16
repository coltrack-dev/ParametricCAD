# CAD-Components-1C — Brick House Live Validation

## Result

The test used the real running ParametricCAD GUI and the real stdio MCP
server. The original user project was not overwritten. Before mutation, the
open project was saved through MCP as:

```text
/tmp/mcp_house_component_split_1c.pcad
```

The final result was saved and reopened through MCP as:

```text
/tmp/mcp_house_component_split_1c_result.pcad
```

## Environment

- Ubuntu 24.04 x86_64
- C++20 / Qt 6.4.2 / Open CASCADE
- GUI: `build/src/ParametricCAD --enable-automation`
- MCP: `build/src/parametriccad-mcp`
- Socket: `/tmp/parametriccad-automation`

All modeling calls below were MCP calls. No mouse, keyboard, Qt dialog, direct
controller call, or direct `.pcad` edit was used.

## MCP call sequence

1. `cad.get_application_status`
2. `cad.get_model_tree`
3. `cad.save_project` to the separate fixture
4. `cad.get_components`
5. `cad.get_component` for the wall candidate
6. `cad.get_component_topology` for the wall candidate
7. `cad.split_component` with origin `[3860,4620,0]`, normal `[1,0,0]`
8. `cad.transform_component` on the negative-X result with vector `[0,-1000,0]`, mode `replace_result`
9. `cad.recompute`
10. `cad.get_component` for source, both split outputs and moved output
11. `cad.get_components`, `cad.get_model_tree`, `cad.inspect_model`
12. `cad.undo`, inspection, `cad.redo`, inspection
13. `cad.save_project`, `cad.open_project`, `cad.recompute`, final inspection

## Selected component

The unambiguous wall candidate was:

```text
transform-499dda87-8e01-4bfc-862c-5d6ae7952408:solid:-0.000000:4620.000000:-0.000000:13000.000000:5000.000000:2700.000000:13338000000.000004
```

It resolved to one valid OCCT solid:

- bounds: X `0..13000`, Y `4620..5000`, Z `0..2700` mm;
- volume: `13,338,000,000.000004 mm³`;
- faces: `6`;
- edges: `24`;
- B-Rep: valid.

`cad.get_component_topology` returned six planar box faces. No door/window
opening intersects this selected wall component; the existing opening results
are represented by the five existing Pocket components elsewhere in the
house result. Their component IDs and volumes remained present after the
operation.

## Split and movement results

The split returned exactly two components:

| Component | Bounds (mm) | Volume (mm³) | B-Rep |
|---|---|---:|---|
| `component-split-negative-74dd8d5a-13ea-4335-bcfb-f82f49353a23:component` | X `0..3860`, Y `4620..5000`, Z `0..2700` | `3,960,360,000.000002` | 1 valid solid |
| `component-split-positive-64205f15-3906-41c8-b4d9-511f29f1244b:component` | X `3860..13000`, Y `4620..5000`, Z `0..2700` | `9,377,640,000.000004` | 1 valid solid |

The split volume sum is `13,338,000,000.000006 mm³`, differing from the
source by approximately `2e-6 mm³` due to OCCT tolerance, so volume is
conserved.

The negative-X segment was translated using:

```text
component-transform-771220f9-9901-494c-9b22-146eec0f56cb:component
```

Its final bounds are X `0..3860`, Y `3620..4000`, Z `0..2700` mm and its
volume remains `3,960,360,000.0000014 mm³`.

## Assertions

| Assertion | Result |
|---|---|
| Real GUI/MCP connection | PASS |
| Unambiguous source component | PASS |
| Exactly one wall split | PASS |
| Exactly two split components | PASS |
| Both outputs valid, one solid | PASS |
| Source replacement relationship persisted | PASS |
| Original wall technically hidden | PASS |
| Positive-X segment unchanged | PASS |
| Negative-X segment moved by Y=-1000 | PASS |
| Other final-transform components remain visible | PASS — 6 of 7 remain visible |
| Existing Pocket/opening components remain present | PASS — 5 Pocket components retained |
| No `SplitAssembly` feature created | PASS — 0 found; 2 `ComponentSplit` + 1 `ComponentTransform` |
| Volume conservation | PASS within ~`2e-6 mm³` |
| Recompute and component IDs | PASS |
| Undo/Redo | PASS; feature count changed 29→28→29 |
| Save/reopen | PASS; moved component resolved with same ID and valid B-Rep |
| Duplicate visible source component | PASS after component-presentation fix |

## Defect found and fixed during validation

The first live run exposed that `ModelPresenter` treated a single-solid
component result as a feature-level AIS presentation. This ignored technical
replacement visibility and could display a replaced source segment together
with its replacement. `ModelPresenter` now resolves persisted component
records for single-solid features and presents them only as component AIS
objects. Legacy solid features without component metadata retain feature-level
presentation.

The house validation also exposed a second visibility interaction: components
belonging to a feature with `userVisible=false` were still individually
presented when their component record had `visible=true`. This affected hidden
legacy Transform/Pocket history after mirroring and explained duplicate walls,
doors, and windows at the ends. Component presentation now requires both the
parent feature and the component to be visible; `cad.get_component` reports the
same effective visibility.

## Aggregate inspection note

`cad.inspect_model` reports the final `Body::shape`, which after a local
component operation is the last component result rather than a union of all
independent displayed components. Component-level conservation and unrelated
component preservation were therefore verified from `cad.get_component` and
`cad.get_components`; their volumes are not blindly summed into the aggregate
Body result.

