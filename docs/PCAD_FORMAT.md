# ParametricCAD `.pcad` format

The current format is version 1 UTF-8 JSON. It stores editable parametric
definitions and selected presentation metadata; it does not store OCCT B-Rep
data, AIS handles, undo history, selection, hover, or active transform state.

## Root

```json
{
  "format": "ParametricCAD",
  "version": 1,
  "features": [],
  "body": [],
  "visibility": {},
  "views": []
}
```

`visibility` and `views` are optional. Old files without them remain valid.
New saves may omit empty optional arrays/objects.

## Canonical model

`body` is an ordered feature history. Each record contains a stable `id`,
`name`, case-sensitive `type`, parameters, and supported dependency IDs.
Dependencies must reference preceding features. The loader validates IDs,
types, parameters, references, and placement before replacing the active model.

The legacy `features` array is accepted for old Box/Cylinder input and is
converted to canonical `Body` features with generated legacy IDs. New saves
write the canonical Body; legacy `Feature` objects are not retained as the
runtime model.

Supported serialized feature records depend on the current registry. The core
records include Box, Cylinder, Cone, Sphere, Torus, Hexagon, Boolean, Sketch,
Face, and Extrude; newer feature classes may have limited or pending
persistence coverage. See `src/model/ProjectFile.cpp` for the authoritative
registry.

## Placement and visibility

An optional `placement` array contains twelve finite values: the first three
rows of an OCCT `gp_Trsf` (3x3 rigid rotation plus translation). Missing
placement means identity. Scale and shear are rejected.

An optional `visible` boolean is persistent `ParametricFeature::userVisible()`.
Missing visibility defaults to true. Dependency and technical hiding remain
computed presentation policy and are not encoded as feature overrides.

Example:

```json
{
  "id": "box-001",
  "name": "Box001",
  "type": "Box",
  "width": 10,
  "depth": 20,
  "height": 30,
  "visible": true,
  "placement": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]
}
```

## Visibility metadata

The optional `visibility` object contains persistent groups, filters, and
visibility presets. It does not contain active isolation, Ghost Others,
selection, spatial box state, section clipping, or camera state.

```json
{
  "visibility": {
    "groups": [],
    "filters": {
      "types": [],
      "roles": [],
      "categories": []
    },
    "presets": []
  }
}
```

Group records use stable IDs, names, optional `parentId`, `mode`, and feature
ID members. Filter records use type IDs, roles, or category IDs with
`visible`, `ghosted`, or `hidden` modes. Visibility presets store rules and
persistent feature visibility overrides, not resolved AIS state.

## Saved views

Saved views are optional root-level presentation metadata:

```json
{
  "views": [
    {
      "id": "view-1",
      "name": "Roof",
      "groupModes": [],
      "filters": {"types": [], "roles": [], "categories": []},
      "featureOverrides": [],
      "isolatedFeatureIds": [],
      "ghostedSelectionIds": [],
      "spatial": {
        "enabled": false,
        "min": [0, 0, 0],
        "max": [1, 1, 1],
        "mode": "hidden"
      },
      "section": {
        "active": false,
        "axis": 2,
        "flipped": false,
        "origin": [0, 0, 0]
      },
      "camera": {
        "valid": true,
        "eye": [10, 10, 10],
        "center": [0, 0, 0],
        "up": [0, 0, 1],
        "scale": 1
      }
    }
  ]
}
```

`axis` is 0/1/2 for X/Y/Z. Saved views capture presentation configuration,
camera eye/center/up/scale, isolation, Ghost Others, spatial rule, and section
state. They do not capture selection, hover, active transform drag, snap
candidates, or AIS objects. Restoring a view does not recompute the Body.

## Load and compatibility

`ProjectFile::load()` validates a temporary `Document`, `Body`, and
`VisibilityManager` state. Only successful validation replaces the active
project. Unknown format versions and unsupported feature types are rejected.
Missing optional visibility/views metadata defaults to empty state. Saved view
references to missing features or groups are ignored safely by visibility
application.

Undo history, periodic autosave/recovery state, raw geometry, viewer-only
Push/Pull preview, active spatial box editing, active section plane, and current
camera outside a saved view are not persisted.
