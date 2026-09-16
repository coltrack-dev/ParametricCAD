# Parametric Feature Architecture

## Implementation status

The numbered sections below describe the target architecture, not a list of
implemented APIs. Current code has two model layers:

- `Document` owns legacy `Feature` primitives (Box/Cylinder), without IDs or dependencies.
- `Body` owns `ParametricFeature` history with stable string IDs, lookup, duplicate-ID
  rejection, dependency pointers and Dirty/UpToDate/Failed states.
- Callers explicitly use `Body::markDirtyFrom()` before `recompute()`. It dirties
  the whole history suffix, including unrelated features; there is no graph traversal.
- SketchFeature builds an XY rectangle wire; FaceFeature depends on a Sketch through
  the existing dependency mechanism. Both live in Body, have menu actions and editor
  support, and persist in .pcad v1 (Face stores sourceFeatureId).
- ExtrudeFeature still takes a profile pointer and vector, without editor or persistence support.
- Tree selection updates existing AIS selection without rebuilding the scene.
  Parameter edits update changed AIS shapes in place without Fit All or scene clearing.

See [ROADMAP.md](ROADMAP.md) for the missing integration and test coverage.
The Sketch -> Face -> Extrude acceptance scenario below remains a planned milestone.

## 1. Purpose

ParametricCAD should represent a CAD model as a sequence of editable features and dependencies rather than only as final OpenCASCADE shapes.

The basic architecture is:

```text
Document
   |
   +-- Feature
         |
         +-- BoxFeature
         +-- CylinderFeature
         +-- SketchFeature
         +-- FaceFeature
         +-- ExtrudeFeature
         +-- future features...
```

Every feature owns its parameters and can rebuild its OpenCASCADE representation.

---

## 2. Fundamental rule

`TopoDS_Shape` is the **result** of a feature.

It is not the feature definition itself.

For example:

```text
ExtrudeFeature
    parameters:
        sourceFeatureId
        length

    generated result:
        TopoDS_Shape
```

Saving only the resulting `TopoDS_Shape` would lose the parametric relationship.

---

## 3. Feature identity

Every feature should have a stable ID.

Conceptually:

```cpp
using FeatureId = std::string;
```

or an equivalent strongly typed identifier.

Example:

```text
sketch-001
face-001
extrude-001
```

Feature IDs are used for dependencies and persistence.

Do not use raw pointers as permanent feature identities.

---

## 4. Base Feature responsibilities

The base `Feature` class should conceptually provide:

```text
FeatureId
name
type
generated shape
rebuild state
error state
```

Possible API:

```cpp
class Feature
{
public:
    virtual ~Feature() = default;

    const FeatureId& id() const;
    const std::string& name() const;

    virtual FeatureType type() const = 0;
    virtual bool rebuild(const Document& document) = 0;

    const TopoDS_Shape& shape() const;

protected:
    FeatureId id_;
    std::string name_;
    TopoDS_Shape shape_;
};
```

Exact implementation should follow the existing project style.

---

## 5. Document responsibilities

`Document` owns all features.

Responsibilities:

* feature lifetime;
* feature lookup by ID;
* feature ordering;
* dependency resolution;
* rebuild propagation;
* adding/removing features;
* validation.

Conceptually:

```text
Document
 ├── Sketch001
 ├── Face001
 └── Extrude001
```

The document should allow:

```text
findFeature(featureId)
```

without exposing ownership through raw pointers.

---

## 6. First dependency chain

The first complete parametric workflow is:

```text
SketchFeature
      ↓
  FaceFeature
      ↓
 ExtrudeFeature
      ↓
    Solid
```

This is the foundation for later CAD operations.

---

## 7. SketchFeature

### Responsibility

A SketchFeature represents editable 2D construction geometry.

The first implementation only needs a rectangle.

Parameters:

```text
width
height
plane
```

Initial plane:

```text
XY
```

Generated result:

```text
TopoDS_Wire
```

Conceptually:

```text
SketchFeature
  width = 100
  height = 60
  plane = XY

        ↓ rebuild

closed TopoDS_Wire
```

---

## 8. Rectangle sketch construction

A rectangle can initially be generated from four points:

```text
P1 = (0, 0, 0)
P2 = (width, 0, 0)
P3 = (width, height, 0)
P4 = (0, height, 0)
```

Edges:

```text
P1 -> P2
P2 -> P3
P3 -> P4
P4 -> P1
```

The result must be a closed wire.

Possible OpenCASCADE tools:

```text
BRepBuilderAPI_MakeEdge
BRepBuilderAPI_MakeWire
```

---

## 9. FaceFeature

### Responsibility

A FaceFeature converts a closed profile into a surface.

Dependency:

```text
sourceFeatureId -> SketchFeature
```

Generated result:

```text
TopoDS_Face
```

Conceptually:

```text
Sketch001
    |
    | closed wire
    v
Face001
```

Possible OpenCASCADE implementation:

```cpp
BRepBuilderAPI_MakeFace faceMaker(wire);
```

The feature must validate:

* source feature exists;
* source feature produces a wire;
* wire is closed;
* face creation succeeds.

---

## 10. ExtrudeFeature

### Responsibility

An ExtrudeFeature creates a solid from a face.

Dependency:

```text
sourceFeatureId -> FaceFeature
```

Parameters:

```text
length
direction
```

Initial implementation:

```text
direction = +Z
```

Generated result:

```text
TopoDS_Solid / TopoDS_Shape
```

Possible OpenCASCADE implementation:

```cpp
gp_Vec direction(0.0, 0.0, length);

BRepPrimAPI_MakePrism prism(face, direction);

TopoDS_Shape result = prism.Shape();
```

---

## 11. Dependency graph

Feature relationships form a directed graph.

Example:

```text
Sketch001
    |
    v
Face001
    |
    v
Extrude001
```

Later:

```text
Sketch001
    |
    v
Extrude001
    |
    v
Fillet001
```

or:

```text
Box001 ------+
             |
             v
          Cut001
             ^
             |
Cylinder001--+
```

The architecture should therefore avoid assumptions that every feature is independent.

---

## 12. Rebuild propagation

When a source feature changes, dependent features must become dirty and rebuild.

Example:

```text
Sketch001 width:
100 -> 120
```

Required rebuild:

```text
Sketch001
   ↓
Face001
   ↓
Extrude001
```

Unrelated features should not rebuild.

Example:

```text
Cylinder005
```

should remain untouched.

---

## 13. Dirty state

A useful future mechanism is:

```text
Clean
Dirty
Error
```

Example:

```text
Sketch001       Clean
Face001         Dirty
Extrude001      Dirty
```

After rebuilding:

```text
Sketch001       Clean
Face001         Clean
Extrude001      Clean
```

If rebuild fails:

```text
Face001         Error
Extrude001      Error / blocked
```

The first implementation may be simpler, but new code should not prevent introducing this mechanism later.

---

## 14. Avoid full viewer rebuilds

UI selection must not trigger complete scene reconstruction.

When a user clicks a feature in the tree:

```text
Tree selection
     ↓
select existing AIS object
```

It should NOT do:

```text
viewer.clear()
redisplay every feature
fitAll()
```

Selection must preserve:

* current zoom;
* camera orientation;
* pan;
* displayed objects.

---

## 15. Viewer responsibilities

`CadViewer` should handle visualization and selection.

It may manage mappings such as:

```text
FeatureId -> AIS_InteractiveObject
```

Possible responsibilities:

* display a feature;
* update a changed feature;
* remove a feature;
* select a feature;
* deselect a feature;
* map selected AIS object back to FeatureId.

It should not own the parametric feature model.

---

## 16. Updating geometry

When a feature is rebuilt, the viewer should update only that feature and affected dependents.

Preferred conceptual API:

```text
viewer.updateFeature(featureId, shape)
```

rather than:

```text
viewer.clear()
for every feature:
    viewer.display(...)
viewer.fitAll()
```

Full scene reconstruction may be used after loading a completely new document, but not for normal parameter editing or selection.

---

## 17. Tree model

The initial tree may remain flat:

```text
Sketch001
Face001
Extrude001
```

Dependency relationships are stored in the model itself.

Later the UI may show:

```text
Extrude001
 └── Face001
      └── Sketch001
```

or another dependency-oriented representation.

The tree hierarchy must not become the source of truth for feature dependencies.

`Document` remains the source of truth.

---

## 18. FeatureEditorPanel

The editor should expose parameters according to feature type.

### SketchFeature

```text
Width
Height
```

### FaceFeature

Normally no direct geometric parameters in the first version.

Display:

```text
Source: Sketch001
```

### ExtrudeFeature

```text
Length
```

Possibly later:

```text
Direction X
Direction Y
Direction Z
Symmetric
Reverse
```

---

## 19. Parameter editing

Editing a parameter should follow this flow:

```text
User changes value
       ↓
Feature parameter changes
       ↓
Feature marked dirty
       ↓
Dependent features marked dirty
       ↓
Rebuild affected graph
       ↓
Update affected AIS objects
```

It should not require rebuilding unrelated geometry.

---

## 20. Feature validation

Every feature validates its own parameters.

### SketchFeature

```text
width > 0
height > 0
```

### FaceFeature

```text
source exists
source produces closed wire
face creation succeeds
```

### ExtrudeFeature

```text
source exists
source produces face
length > 0
```

Failures should produce human-readable messages.

---

## 21. Feature ownership

`Document` should preferably own features through:

```cpp
std::unique_ptr<Feature>
```

Dependent features should store IDs, not ownership pointers.

Good:

```cpp
FeatureId sourceFeatureId_;
```

Avoid:

```cpp
Feature* source_;
```

as the persisted dependency representation.

Temporary resolved pointers/references may be used internally during rebuild but should not be the permanent relationship.

---

## 22. Serialization

Feature definitions and dependencies must be serializable.

Example:

```json
{
  "id": "sketch-001",
  "type": "sketch",
  "width": 100,
  "height": 60
}
```

```json
{
  "id": "face-001",
  "type": "face",
  "sourceFeatureId": "sketch-001"
}
```

```json
{
  "id": "extrude-001",
  "type": "extrude",
  "sourceFeatureId": "face-001",
  "length": 40
}
```

See:

```text
docs/PCAD_FORMAT.md
```

---

## 23. Existing primitive features

`BoxFeature` and `CylinderFeature` remain valid feature types.

They are independent primitive features.

Example:

```text
BoxFeature
    ↓
TopoDS_Solid
```

```text
CylinderFeature
    ↓
TopoDS_Solid
```

They do not have to be rewritten internally as Sketch + Extrude immediately.

That may be considered later if useful.

---

## 24. Future features

After Sketch/Face/Extrude works reliably, possible next features include:

```text
CircleSketch
PolylineSketch
RevolveFeature
BooleanCutFeature
BooleanFuseFeature
BooleanCommonFeature
FilletFeature
ChamferFeature
LoftFeature
SweepFeature
OffsetFeature
PatternFeature
MirrorFeature
```

---

## 25. Boolean example

A future nut-like model could become:

```text
HexagonSketch
      ↓
HexagonFace
      ↓
Extrude
      |
      +----------------+
                       |
CircleSketch           |
      ↓                |
CircleFace             |
      ↓                |
HoleExtrude            |
      |                |
      +-------> BooleanCut
                       |
                       v
                    NutSolid
```

This demonstrates why explicit dependency tracking is important.

---

## 26. Rebuild order

The document should rebuild features in dependency order.

For a simple sequence:

```text
Sketch
Face
Extrude
```

For a graph, use dependency traversal or topological ordering.

Cycles must be rejected.

Invalid example:

```text
FeatureA -> FeatureB
FeatureB -> FeatureA
```

A cyclic dependency is a document error.

---

## 27. Deleting features

Deleting a source feature must account for dependents.

Example:

```text
Delete Sketch001
```

while:

```text
Face001 -> Sketch001
Extrude001 -> Face001
```

exists.

Possible policies:

1. prohibit deletion until dependents are removed;
2. ask to delete dependents too;
3. keep dependents in an error state.

The first implementation should prefer the simplest predictable behavior.

---

## 28. Naming

Suggested default names:

```text
Box001
Cylinder001
Sketch001
Face001
Extrude001
```

Names are user-facing.

Feature IDs are internal stable identifiers.

Do not use the display name itself as the dependency key.

The user may later rename:

```text
Sketch001 -> BaseProfile
```

without breaking dependencies.

---

## 29. Error isolation

A failed feature rebuild should not crash the application.

Example:

```text
Sketch001      valid
Face001        invalid
Extrude001     blocked
Cylinder001    valid
```

Unrelated geometry should remain displayed and editable.

---

## 30. Initial implementation scope

The first milestone should contain only:

```text
Rectangle Sketch
      ↓
Face
      ↓
Extrude
```

plus integration with:

* Document;
* tree;
* FeatureEditorPanel;
* CadViewer;
* `.pcad`;
* save/load;
* selection.

Do not add Loft, Sweep, NURBS or advanced constraints until this workflow is stable.

---

## 31. Acceptance scenario

The basic scenario should work as follows.

### Step 1

Create:

```text
Sketch001
width = 100
height = 60
```

The viewer shows a rectangular wire.

### Step 2

Select `Sketch001`.

Run:

```text
Create Face
```

Result:

```text
Face001
source = Sketch001
```

### Step 3

Select `Face001`.

Run:

```text
Extrude
```

with:

```text
length = 40
```

Result:

```text
Extrude001
```

showing a rectangular solid.

### Step 4

Change:

```text
Sketch001 width
100 -> 150
```

Expected result:

```text
Sketch001 rebuilt
Face001 rebuilt
Extrude001 rebuilt
```

The solid becomes wider.

Current camera and zoom remain unchanged.

### Step 5

Change:

```text
Extrude001 length
40 -> 80
```

Only the extrusion and necessary dependent presentation should update.

### Step 6

Save:

```text
test.pcad
```

Close ParametricCAD.

Open:

```text
test.pcad
```

Expected restored model:

```text
Sketch001
Face001
Extrude001
```

with all parameters and dependencies intact.
