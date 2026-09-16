# ParametricCAD `.pcad` File Format

## Implemented format (version 1)

`src/model/ProjectFile.cpp` currently reads and writes UTF-8 JSON with this root:

```json
{
  "format": "ParametricCAD",
  "version": 1,
  "features": [{"type": "Box", "width": 10, "depth": 20, "height": 30}],
  "body": [{"id": "box-001", "name": "Box001", "type": "Box", "width": 10, "depth": 20, "height": 30}]
}
```

- `features` contains legacy Box/Cylinder parameters without IDs.
- `body` contains ID/name/type plus parameters for Box, Cylinder, Cone, Sphere,
  Torus, Hexagon, Boolean, Sketch and Face. Type names are case-sensitive.
- Boolean dependencies are stored as `left`/`right` IDs and `operation`
  (`Fuse`, `Cut`, `Common`); sources must precede the dependent entry.
- Other body parameters: Cylinder (`radius`, `height`), Cone (`bottomRadius`,
  `topRadius`, `height`), Sphere (`radius`), Torus (`majorRadius`, `minorRadius`),
  Hexagon (`acrossFlats`, `height`).
- Loading validates the root, fields, IDs and supported references and rebuilds
  temporary models before replacing the active Document/Body. Unknown versions
  and unsupported types are rejected. Saving uses `QSaveFile` for atomic replacement.
- Sketch uses `type: "Sketch"`, `plane: "XY"`, `width` and `height`.
  Face uses `type: "Face"` and `sourceFeatureId`, referencing a preceding Sketch in `body`.
  IDs, names and editable dependencies survive loading. Missing, forward and wrong-type
  Face references are rejected with both IDs in the error message.
- Extrude is not supported by this serializer. No migration to version 2 is implemented.
  Geometry and viewer-only Push/Pull edits are not stored.
- Save and close use the current filename or the AppDataLocation `autosave.pcad`
  fallback. There is no periodic autosave timer or recovery UI.

## Proposed format (not implemented)

The numbered sections below preserve the design proposal. In particular,
`application`/`formatVersion`/`document`, lowercase type names and
nested feature layouts are **not** accepted by the current loader. Only the flat
Sketch/Face v1 records described above are implemented. The complete
Sketch -> Face -> Extrude example is a future acceptance target, not a loadable file.
See [ROADMAP.md](ROADMAP.md) for status and remaining work.

## 1. Purpose

The `.pcad` format stores ParametricCAD documents.

The main goal of the format is to preserve the **parametric model**, not only the final OpenCASCADE geometry.

A saved document must retain enough information to rebuild editable features after loading.

The format should support:

* feature parameters;
* stable feature identifiers;
* feature dependencies;
* document structure;
* future format migration;
* validation during loading.

Raw `TopoDS_Shape` geometry is not the primary persisted representation.

---

## 2. General principles

### 2.1 Parametric model first

A feature should be saved using its defining parameters.

Example:

```json
{
  "id": "feature-001",
  "type": "box",
  "width": 100.0,
  "depth": 70.0,
  "height": 30.0
}
```

The resulting `TopoDS_Shape` should be rebuilt when the file is loaded.

---

### 2.2 Stable feature IDs

Every feature should have a stable identifier.

Example:

```json
{
  "id": "sketch-001",
  "type": "sketch"
}
```

Feature IDs are used by dependent features.

Example:

```json
{
  "id": "face-001",
  "type": "face",
  "sourceFeatureId": "sketch-001"
}
```

IDs must remain stable during save/load.

Do not use:

* raw pointers;
* memory addresses;
* indexes in a vector as permanent references.

---

### 2.3 Dependencies

Dependencies between features must be stored explicitly.

Example:

```text
Sketch001
    ↓
Face001
    ↓
Extrude001
```

Serialized form:

```json
{
  "id": "face-001",
  "type": "face",
  "sourceFeatureId": "sketch-001"
}
```

and:

```json
{
  "id": "extrude-001",
  "type": "extrude",
  "sourceFeatureId": "face-001",
  "length": 40.0
}
```

After loading, the same dependency graph must be reconstructed.

---

## 3. File extension

ParametricCAD project files use:

```text
.pcad
```

Example:

```text
nut.pcad
bracket.pcad
test-part.pcad
```

---

## 4. Encoding

The initial format uses JSON.

Recommended encoding:

```text
UTF-8
```

The file should be human-readable during early development.

---

## 5. Root object

Example:

```json
{
  "application": "ParametricCAD",
  "formatVersion": 2,
  "document": {
    "features": []
  }
}
```

Required root fields:

| Field           | Type    | Description             |
| --------------- | ------- | ----------------------- |
| `application`   | string  | Must be `ParametricCAD` |
| `formatVersion` | integer | File format version     |
| `document`      | object  | Parametric document     |

---

## 6. Document structure

Example:

```json
{
  "document": {
    "features": [
      {
        "id": "sketch-001",
        "type": "sketch",
        "name": "Sketch001",
        "profile": {
          "type": "rectangle",
          "width": 100.0,
          "height": 60.0
        }
      },
      {
        "id": "face-001",
        "type": "face",
        "name": "Face001",
        "sourceFeatureId": "sketch-001"
      },
      {
        "id": "extrude-001",
        "type": "extrude",
        "name": "Extrude001",
        "sourceFeatureId": "face-001",
        "length": 40.0
      }
    ]
  }
}
```

---

## 7. Supported feature types

### 7.1 BoxFeature

Example:

```json
{
  "id": "box-001",
  "type": "box",
  "name": "Box001",
  "width": 100.0,
  "depth": 70.0,
  "height": 30.0
}
```

Validation:

```text
width > 0
depth > 0
height > 0
```

---

### 7.2 CylinderFeature

Example:

```json
{
  "id": "cylinder-001",
  "type": "cylinder",
  "name": "Cylinder001",
  "radius": 40.0,
  "height": 80.0
}
```

Validation:

```text
radius > 0
height > 0
```

---

### 7.3 SketchFeature

Initial implementation supports a rectangle profile.

Example:

```json
{
  "id": "sketch-001",
  "type": "sketch",
  "name": "Sketch001",
  "plane": "XY",
  "profile": {
    "type": "rectangle",
    "width": 100.0,
    "height": 60.0
  }
}
```

Validation:

```text
width > 0
height > 0
```

Initial plane:

```text
XY
```

Future versions may support:

```text
XY
XZ
YZ
custom plane
```

---

### 7.4 FaceFeature

A FaceFeature is derived from another feature that produces a closed wire.

Example:

```json
{
  "id": "face-001",
  "type": "face",
  "name": "Face001",
  "sourceFeatureId": "sketch-001"
}
```

Validation:

* source feature must exist;
* source feature must produce a valid closed wire;
* the resulting face must be valid.

---

### 7.5 ExtrudeFeature

Example:

```json
{
  "id": "extrude-001",
  "type": "extrude",
  "name": "Extrude001",
  "sourceFeatureId": "face-001",
  "length": 40.0,
  "direction": [0.0, 0.0, 1.0]
}
```

Validation:

```text
length > 0
```

The source feature must exist and produce a valid face.

Initial implementation may always use:

```text
direction = +Z
```

The explicit direction field is retained for future compatibility.

---

## 8. Load sequence

Loading should happen in several stages.

### Stage 1 — Parse

Read JSON and validate basic syntax.

Do not modify the active document yet.

---

### Stage 2 — Validate root metadata

Validate:

```text
application
formatVersion
document
features
```

Reject unsupported file versions with a clear error.

---

### Stage 3 — Create feature definitions

Create feature objects with their parameters and IDs.

Do not resolve dependencies through raw pointers during parsing.

---

### Stage 4 — Resolve dependencies

Resolve references such as:

```text
sourceFeatureId
```

Example:

```text
face-001 -> sketch-001
extrude-001 -> face-001
```

Missing references must cause a loading error.

---

### Stage 5 — Rebuild

Rebuild features in dependency order.

Example:

```text
Sketch
Face
Extrude
```

---

### Stage 6 — Replace active document

Only after the complete file has been successfully parsed, validated and rebuilt should the current document be replaced.

A failed load must not destroy the currently open document.

---

## 9. Save sequence

Recommended save process:

1. validate the active document;
2. serialize all feature IDs;
3. serialize feature types;
4. serialize parameters;
5. serialize dependencies;
6. create JSON;
7. write through `QSaveFile`;
8. commit the file atomically.

Use `QSaveFile` where possible to reduce the risk of corrupting an existing document if saving fails.

---

## 10. Autosave

If the document already has a filename, automatic save should use that file.

If the document has never been saved manually, use an application data location.

Example:

```text
<application-data>/ParametricCAD/autosave.pcad
```

Use:

```cpp
QStandardPaths::AppDataLocation
```

The autosave file is a normal `.pcad` document.

---

## 11. Format versions

The file format must be versioned.

Example:

```json
{
  "formatVersion": 2
}
```

Suggested history:

```text
Version 1
- BoxFeature
- CylinderFeature

Version 2
- stable FeatureId
- SketchFeature
- FaceFeature
- ExtrudeFeature
- feature dependencies
```

Never silently interpret an unknown future format version.

---

## 12. Backward compatibility

When possible, newer ParametricCAD versions should continue reading older `.pcad` files.

For example, a version 1 file containing:

```json
{
  "type": "box",
  "width": 100,
  "depth": 70,
  "height": 30
}
```

may be converted internally to a version 2 feature by generating a new stable ID.

Example:

```text
box-001
```

The loader may perform migration in memory.

Do not rewrite the source file automatically unless the user explicitly saves it.

---

## 13. Error handling

Loading errors should be understandable.

Examples:

```text
Unsupported ParametricCAD file version: 5
```

```text
Feature face-002 references missing feature sketch-004
```

```text
Sketch001 does not contain a closed wire
```

```text
Extrude001 has invalid length: -20
```

Avoid generic messages such as:

```text
Load failed
```

when more precise information is available.

---

## 14. Future extensions

The format should remain extensible for:

* CircleSketch;
* PolylineSketch;
* constrained sketches;
* RevolveFeature;
* LoftFeature;
* SweepFeature;
* BooleanCutFeature;
* BooleanFuseFeature;
* FilletFeature;
* ChamferFeature;
* transformation features;
* assemblies;
* custom coordinate systems;
* materials;
* visual properties;
* document metadata.

Feature-specific data should remain isolated inside each serialized feature.

---

## 15. Non-goals

The `.pcad` format should not initially be treated as:

* STEP;
* IGES;
* STL;
* BREP exchange format.

Those formats serve different purposes.

`.pcad` is primarily the **editable ParametricCAD project format**.

Export to STEP/STL/etc. should be implemented separately.

---

## 16. Example complete document

```json
{
  "application": "ParametricCAD",
  "formatVersion": 2,
  "document": {
    "features": [
      {
        "id": "sketch-001",
        "type": "sketch",
        "name": "Sketch001",
        "plane": "XY",
        "profile": {
          "type": "rectangle",
          "width": 100.0,
          "height": 60.0
        }
      },
      {
        "id": "face-001",
        "type": "face",
        "name": "Face001",
        "sourceFeatureId": "sketch-001"
      },
      {
        "id": "extrude-001",
        "type": "extrude",
        "name": "Extrude001",
        "sourceFeatureId": "face-001",
        "length": 40.0,
        "direction": [
          0.0,
          0.0,
          1.0
        ]
      }
    ]
  }
}
```
