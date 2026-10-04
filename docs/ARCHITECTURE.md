# Architecture

ParametricCAD is a C++20 desktop CAD application built with Qt 6 and Open
CASCADE Technology (OCCT). The application separates the editable parametric
model from OCCT visualization and from the Qt user interface.

## Layers

```text
MainWindow
    ├── ModelingController ──> Document / Body / commands
    ├── ProjectController  ──> ProjectFile and project lifecycle
  ├── FeatureEditorPanel ──> FeatureEditingService (DTOs and stable IDs)
    └── ModelPresenter     ──> CadViewer

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
- `setProperty()` applies a type-checked edit without exposing the concrete
  class; the current `PropertyValue` supports `bool`, `int`, `double` and
  `string`;
- `serialize()` delegates parameter encoding to the feature;
- `hiddenDependencyIds()` supplies presentation-specific visibility rules;
- `creationLabel()` supplies the undo command label.

Adding a feature should therefore add its own geometry builder, properties and
serialization parameters. It should not require another RTTI branch in
`MainWindow`, `FeatureEditorPanel`, `Body`, or the viewer.

`FeatureProperty` is intentionally small: it contains a stable key, display
label, a `PropertyValue`, optional numeric limits, and an editable flag. The
editor creates controls from this metadata and sends edits to the generic
`FeatureEditingService`; `ModelingController` validates the descriptor and
records changes with `ChangeParametricPropertyCommand`.

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

The application layer is in `src/application`:

- `ModelingController` owns the active `Document`, `Body` and `QUndoStack`.
  It creates features, validates selection-dependent operations, creates
  commands, performs deletion/clear operations, and exposes action state.
- `ProjectController` performs validated save/load/new-project operations. A
  loaded project is prepared by `ProjectFile` before replacing the active
  model, and successful saves update the undo-stack clean state.
- `FeatureEditingService` is the narrow application-facing contract used by
  `FeatureEditorPanel`. It exposes feature descriptors, property metadata,
  action state, modeling actions, and undo/redo without exposing `Body`,
  `Document`, concrete feature classes, or `QUndoStack` to the widget.

`MainWindow` is the Qt shell. It creates menus, dialogs and widgets, forwards
actions to the application controllers, displays errors/status, and routes
selection IDs between the viewer and editor. It does not construct feature
objects, inspect model dependencies, build commands, or serialize projects.

`FeatureEditorPanel` provides the model tree and property editors. It stores
only presentation descriptors and stable feature IDs. Button actions and
property edits call `FeatureEditingService`; the application layer creates and
pushes commands, recomputes the model, and supplies refreshed descriptors.
Consequently the panel has no direct dependency on `Body`, `Document`,
`QUndoStack`, commands, or concrete feature classes.

`ModelPresenter` is the model/view adapter. It recomputes the Body, updates
feature presentations, retains current IDs, and applies polymorphic visibility
rules. This keeps model-to-view synchronization out of `MainWindow`.

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
    -> application state supplies refreshed feature descriptors
    -> MainWindow refreshes feature presentations
    -> CadViewer displays the resulting shapes
```

Selection flows in the opposite direction: `CadViewer` emits selected feature
IDs and `MainWindow` forwards them to `FeatureEditorPanel`.

### Selection architecture

`OcctSelectionAdapter` is the viewer boundary for OCCT selection reads and
detection. It wraps `MoveTo()` and `SelectDetected()`, resolves the selected
AIS presentation to a feature ID, and exposes `SelectionHit` values for the
current object or subshape. Face and object validation are transient checks
against the current AIS presentation and shape.

`SelectionHit` contains immediate selection data: feature ID, selection kind,
current selected shape, parent presentation, and a current-presentation
subshape index when applicable. The index is not a persistent topology ID and
must not be reused after recompute or presentation replacement.

`CadViewer::SelectionState` is the normalized committed selection state after
OCCT selection/detection has completed. OCCT remains the picking mechanism, but
its selected-owner iteration order is not semantic identity and must not be
used as the source for interactive tools. `MainWindow` keeps
`selectedObjectIds_` as an application/UI compatibility projection used by
actions and model-operation inputs. The `FeatureEditorPanel` keeps only Qt
tree selection and uses signal blocking for programmatic viewer-to-tree
synchronization. Both tree selection and direct viewer selection converge on
the same `SelectionState`.

`TopoDS_Shape` identity is transient: feature rebuilds may replace every
subshape. Persistent active topology selection therefore uses
`featureId + TopologicalReference`, not a stored `TopoDS_Shape` or iteration
index. A reference records Face/Edge/Vertex kind and geometry signatures
(surface/curve type, measurements, points, normals where applicable, and
bounding box). During refresh, `ModelPresenter` captures references before
recompute; `CadViewer` resolves them only inside the original feature and
restores the resolved OCCT owners after AIS presentation updates. Missing or
ambiguous matches are dropped. Geometry signatures are an MVP fallback
persistent-naming strategy, not a guaranteed final persistent naming solution.

Interaction priority remains local to `CadViewer`: active transform capture
and independent `TransformGizmo` handle picking are processed before model
selection, followed by Push/Pull capture, camera navigation, and ordinary
selection. `TransformGizmo` does not use model selection picking. For
interactive transforms, `SelectionState::primary` is the sole source of the
transform target. The invariant is:

```text
SelectionState::primary
    == TransformGizmo target
    == beginTransform() feature
    == SnapManager source feature
```

When selection changes from A to B, the gizmo is detached from A, its pivot
and placement are rebuilt for B, and the next transform captures B. Clearing
selection hides the gizmo. A tree selection change during an active transform
cancels the drag before the new selection is applied, so a visible B cannot
retain A as its transform source. X-Ray detected-entity cycling remains a
`CadViewer` policy through `HilightNextDetected()`.

Viewer/OCCT selection order must never be used as semantic model identity.
Interactive tools that require one selected object must consume the normalized
application selection state and use `SelectionState::primary`; they must not
independently walk OCCT selected owners, reuse a stale selected-object helper,
or substitute hover/detected geometry for committed selection.

### Snap architecture

`SnapManager` does not choose the transform source. `beginTransform()` captures
source references from the feature selected through `SelectionState::primary`,
then passes those references to `SnapManager`, which builds candidate pairs at
transform start using the cached model reference set. Endpoint and midpoint references carry a
`TopologyReference`; intersection references carry both contributing edge
references. Endpoint points are deduplicated with tolerance, while distinct
edges remain distinct so their intersections are not lost. Midpoints use the
trimmed curve parameter domain rather than a bounding-box center.

Line/curve intersection discovery is performed only while building the cached
candidate set and is restricted by projected edge proximity. Mouse-move
processing uses the existing screen-space index, hysteresis, and cached
projections; it does not traverse model topology or rebuild AIS geometry.
Endpoint, intersection, and midpoint candidates have descending priority and
are displayed with distinct snap-marker colors. Transform correction remains
owned by the transform interaction and is composed with the raw preview delta,
so the committed command receives the same placement shown during preview.
The snap source, preview source, and committed feature are therefore always
the same selected feature; stale references from a previous selection are not
valid.

### Selection/transform regression coverage

The regression fixed on `HEAD c28170d` used the first object returned by OCCT
selected-object iteration (`validatedSelectedObjectHit()`) instead of
`SelectionState::primary`. This could leave the gizmo attached to the previous
feature and caused the next transform to build snap source references for that
feature. Snapping itself and its correction composition were correct; the
wrong source entered the pipeline before `SnapManager`.

The state/model regression tests cover:

- selection switching A -> B -> empty updates the primary feature state;
- a transform source is associated with A, then B, and never with stale A
  after switching to B.

At this revision, the project builds successfully, all 11 CTest tests pass,
and `git diff --check` passes.

Controller behavior is testable without starting the Qt GUI. The headless
controller tests cover stable-ID selection, Sketch → Face → Extrude creation,
Boolean Cut creation, property validation and property undo/redo, project
replacement, and invalid-load safety. Widget regression tests use the same
service contract rather than injecting model containers or an undo stack.

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
