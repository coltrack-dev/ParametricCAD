# Architecture

ParametricCAD is a C++20 desktop CAD application built with Qt 6 and Open
CASCADE Technology (OCCT). The application separates the editable parametric
model from OCCT visualization and from the Qt user interface.

## Layers

```text
MainWindow
    ├── FeatureEditorPanel ──> Body / ParametricFeature
    └── CadViewer            ──> OCCT presentation and selection

ProjectFile ──> Document + Body

Body ──> ParametricFeature ──> operations / BasicFeatures ──> TopoDS_Shape
```

### Geometry

`src/geometry` contains small application-level mathematical types such as
`Point3D` and `Vector3D`. OCCT types are used for B-Rep geometry and geometric
operations.

### Model

The model layer is in `src/model`:

- `ParametricFeature` is the base class for the canonical parametric history.
  It owns a stable ID, display name, parameters in derived classes, generated
  `TopoDS_Shape`, dependencies, and recompute state.
- `Body` owns an ordered collection of shared parametric features. It validates
  feature insertion/removal, propagates dirty state through the history,
  recomputes the body, and exposes the resulting shape and error message.
- `Feature` and `Document` are the older ownership API. They remain available
  for compatibility with existing callers and legacy `.pcad` input, but new
  parametric modeling uses `Body` and `ParametricFeature`.

The application currently keeps both containers in `MainWindow`: the
canonical parametric history is in `parametricBody_`, while `document_` holds
legacy document features when needed. New saves serialize the canonical Body.

The model-facing code works through `ParametricFeature` interfaces rather than
testing concrete C++ classes. Feature-specific behavior is implemented by the
feature itself.

## Polymorphic feature contract

`ParametricFeature` provides the extension points used by the rest of the
application:

- `typeId()` identifies a feature in the persistence registry;
- `role()` exposes semantic capabilities such as `Sketch` and `Face`;
- `properties()` returns editable or read-only `FeatureProperty` metadata;
- `setNumericProperty()` applies an edit without exposing the concrete class;
- `serialize()` delegates parameter encoding to the feature;
- `hiddenDependencyIds()` supplies presentation-specific visibility rules;
- `creationLabel()` supplies the undo command label.

Adding a feature should therefore add its own geometry builder, properties and
serialization parameters. It should not require another RTTI branch in
`MainWindow`, `FeatureEditorPanel`, `Body`, or the viewer.

`FeatureProperty` is intentionally small: it contains a stable key, display
label, a `double` or `string` value, optional numeric limits, and an editable
flag. The editor creates controls from this metadata and records changes with
the generic `ChangeParametricPropertyCommand`.

## Parametric features and recompute

Every `ParametricFeature` has one of these states:

```cpp
enum class FeatureState {
    Dirty,
    UpToDate,
    Failed
};
```

Feature dependencies are stored as weak pointers. Constructors register the
dependencies needed to build a feature; for example:

```text
Sketch -> Face -> Extrude
```

When a parameter changes, the feature is marked dirty. `recompute()` checks
that dependencies still exist and are not failed, calls the feature's
`build()` implementation, rejects a null result, and records either the new
shape or an error. `Body::recompute()` processes the ordered history and
reports the first model error through `lastError()`.

The current feature implementations include:

- primitives: Box, Cylinder, Cone, Sphere, Torus and Hexagon;
- profile features: Rectangle Sketch and Face;
- construction: Extrude and Revolve;
- operations: Boolean (Fuse, Cut and Common), Fillet, Chamfer, Shell and
  Offset.

Feature construction belongs in `src/operations`, not in `CadViewer`.
Operations validate their inputs and must preserve OCCT operand semantics; a
cut is always `base - tool`.

## Operations

`src/operations` adapts feature parameters to OCCT builders. Shared primitive
and profile construction is provided by `BasicFeatures`; parametric feature
classes provide the feature-specific parameters and call those builders or
OCCT algorithms. A failed OCCT operation must leave the feature in the
`Failed` state with a user-visible error rather than silently returning an
incorrect shape.

## Application and UI

`MainWindow` coordinates document actions, menus, the undo stack, persistence,
the feature editor, and viewer refreshes. It does not build feature geometry.

`FeatureEditorPanel` provides the model tree and property editors. It changes
feature parameters through model APIs and uses `QUndoStack` commands for
creation, deletion, parameter edits, and project clearing. Commands retain
feature ownership where necessary, but do not own or replace the `Document` or
`Body` containers.

`CadViewer` owns the OCCT viewer and presentation objects. It is responsible
for displaying and updating shapes, object/edge/face selection, hover
highlighting, camera controls, X-Ray mode, and the experimental direct-modeling
Push/Pull interaction. Viewer-only Push/Pull results are not part of the
parametric history or `.pcad` persistence.

The normal model-to-view flow is:

```text
edit command
    -> mark affected feature(s) dirty
    -> Body::recompute()
    -> MainWindow refreshes feature presentations
    -> CadViewer displays the resulting shapes
```

Selection flows in the opposite direction: `CadViewer` emits selected feature
IDs and `MainWindow` forwards them to `FeatureEditorPanel`.

## Persistence

`ProjectFile` is the persistence boundary. It saves editable parameters,
stable IDs, names, feature types, and supported dependency references as
version 1 JSON in `.pcad` files. It does not serialize `TopoDS_Shape` data or
undo history.

Loading builds a temporary `Body`, validates fields and references, recomputes
it, and only then replaces the active document and body. This prevents an
invalid file from partially replacing the current project. Legacy Box and
Cylinder records are converted to canonical parametric features during load
and save.

Deserialization uses a registry keyed by the stable serialized `type` value.
The registry is the deliberate type boundary required to construct a concrete
class from file data; serialization of an existing feature remains virtual and
does not inspect its C++ type. Dependencies are validated generically against
the preceding history entries, while source-role validation is handled by the
feature contract.

See [PCAD_FORMAT.md](PCAD_FORMAT.md) for the file-level schema and
[PARAMETRIC_FEATURES.md](PARAMETRIC_FEATURES.md) for feature behavior.

## Compatibility and planned work

The legacy `Document`/`Feature` API and the canonical `Body` history are not
yet unified. Direct Push/Pull is also separate from the parametric history.
Some feature classes already have geometry implementations but are not yet in
the version 1 persistence registry. Future work may unify these histories and
add a persisted parametric Push/Pull feature, but changes must preserve
existing selection, editing, save/load, and undo behavior.
