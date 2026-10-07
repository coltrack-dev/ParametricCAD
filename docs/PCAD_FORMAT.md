# ParametricCAD `.pcad` format

The logical ParametricCAD schema is version 1. New `.pcad` files use a ZIP
compatible container with Deflate compression. The container stores a UTF-8
`manifest.json` plus one raw native OCCT B-Rep entry per imported feature.
Older version-1 `.pcad` files that are plain UTF-8 JSON remain readable. AIS
handles, undo history, selection, hover, and active transform state are not
stored.

## Root

```json
{
  "format": "ParametricCAD",
  "version": 1,
  "storage": "archive",
  "containerVersion": 1,
  "features": [],
  "body": [],
  "visibility": {},
  "views": []
}
```

The JSON above is the archive's `manifest.json`. A new file has the logical
layout:

```text
model.pcad
  manifest.json
  geometry/
    <sha256(feature-id)>.brep
```

`storage` and `containerVersion` describe the physical storage layer; they do
not change the logical model schema version. Archive entry names are generated
from ParametricCAD feature IDs and are validated on read.

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
Face, Extrude, and Revolve. See `src/model/ProjectFile.cpp` for the
authoritative registry.

Revolve records contain `sourceFeatureId`, `angleDegrees`, and a typed `axis`
object. Global axes use `GlobalX`, `GlobalY`, or `GlobalZ`; a Sketch-line axis
stores `sketchFeatureId` and stable `entityId`; a model-edge axis stores its
owning `featureId` and serialized `TopologicalReference`. The older Revolve
form containing `axisOrigin*`, `axis*`, and `angleDegrees` remains readable.

```json
{
  "type": "Revolve",
  "sourceFeatureId": "sketch-1",
  "axis": { "type": "SketchLine", "sketchFeatureId": "sketch-1", "entityId": "line-7" },
  "angleDegrees": 270.0
}
```

`IfcImported` records contain generic provenance fields (`sourceFormat`,
`sourceFile`, `ifcGlobalId`, `ifcEntityType`, `ifcBuilding`, and `ifcStorey`)
and a `shapePayload` reference:

```json
"shapePayload": {
  "storage": "archive",
  "path": "geometry/<sha256(feature-id)>.brep"
}
```

The entry contains raw native B-Rep bytes; it is not Base64 encoded. The
legacy `geometryPayload` Base64 field is accepted only when loading old plain
JSON files. `ShapePayload` is the encoding boundary and `sourceFile` is not a
load-time dependency.

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

`ProjectFile::load()` detects an archive by its ZIP signature, otherwise parses
the file as legacy plain JSON. It validates a temporary `Document`, `Body`, and
`VisibilityManager` state. Only successful validation replaces the active
project. Unknown format versions and unsupported feature types are rejected.
Missing optional visibility/views metadata defaults to empty state. Saved view
references to missing features or groups are ignored safely by visibility
application.

Undo history, periodic autosave/recovery state, viewer-only
Push/Pull preview, active spatial box editing, active section plane, and current
camera outside a saved view are not persisted.
