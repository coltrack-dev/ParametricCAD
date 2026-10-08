# Architecture

## Unified Sketch creation

The modeling UI exposes one `Create Sketch` command. It is enabled without a
selection for global `XY Plane`, `XZ Plane`, and `YZ Plane` sketches, and a
single selected planar face adds the `Selected Planar Face` placement choice.
All choices create a `SketchFeature` and enter the same edit lifecycle; the
Sketch tools create Lines, Rectangles, Circles, Arcs, and other entities with
stable `SketchEntityId` values. Placement is represented by the existing
`SketchSupportType` and `currentFrame()` model APIs, so downstream operations,
selection lifecycle, and persistence do not need a special Rectangle Sketch
path. The legacy `Add Rectangle Sketch` shortcut is intentionally absent from
the normal UI.

The `Arc` tool picks start, end, then a point on the arc. `Center Arc` keeps
the center, start, end sequence; both are converted to the same persistent
`SketchArc` representation.

## Toolbar architecture

`MainWindow::setupToolbars()` groups existing `QAction` instances into movable
and dockable File & History, Sketch, Constraints, Solid Modeling, Modify,
Transform, Selection, View, Visibility, and BIM toolbars. Menu and toolbar
commands therefore share handlers, shortcuts, enabled state, checked state,
and undo behavior. Stable toolbar object names are restored with
`QMainWindow::restoreState()` and `QSettings`, together with window geometry.
Toolbar icons are bundled SVG resources under `src/resources` and are selected
by command category, with native Qt style icons as a fallback.
Unsupported inventory items such as Loft, Offset, Boolean operations, standard
orthographic view buttons, and snap settings are not presented as fake actions.

ParametricCAD is a C++20 / Qt 6 / Open CASCADE desktop application. The
runtime separates the editable parametric model, application policy, and OCCT
presentation. The model is the source of geometry and dependencies; the
viewer is the source of interactive presentation state.

## Overview

```text
Qt UI
  MainWindow / FeatureEditorPanel / CadViewer
        |
Application
  ModelingController / ProjectController / VisibilityManager / ModelPresenter
        |
Commands and services
  QUndoStack / FeatureCommands / FeatureEditingService / SelectionResolver
        |
Parametric model
  Body -> ParametricFeature -> dependencies / placement / TopoDS_Shape
        |
OCCT geometry and AIS presentation
        |
ProjectFile -> .pcad container / legacy JSON
```

### UI and viewer

`MainWindow` is the Qt shell. It creates menus, docks and dialogs, routes
commands to application services, synchronizes selection, and coordinates
viewer-only state such as spatial boxes, section planes, and saved-view camera
restoration.

`FeatureEditorPanel` displays feature descriptors, properties, groups, filters,
and visibility presets. It stores UI descriptors and stable IDs; it does not
own `Body`, `Document`, commands, or geometry.

`CadViewer` owns `AIS_InteractiveContext`, `V3d_View`, feature AIS objects,
selection interaction, camera navigation, transform preview, snapping
integration, spatial-box presentation, section clipping, and viewer-side
presentation modes. It never constructs parametric feature geometry.

`ModelPresenter` is the synchronization adapter between `Body` and `CadViewer`.
`refreshModel()` recomputes the Body, updates/removes feature presentations,
refreshes world-space bounds, applies visibility deltas, and restores selection.
`refreshVisibility()` evaluates presentation policy without recomputing the
Body.

## Application layer

`ModelingController` owns the active `Document`, canonical `Body`, modeling
operations, action state, and `QUndoStack` commands. Geometry edits mark the
affected feature/dependents dirty, recompute, and refresh the model.

`ProjectController` owns project lifecycle coordination around
`ProjectFile`: validated load, save, and new-project replacement. Project
loading builds a temporary document/body/visibility state before committing it.

`IfcImporter` is the application-facing bulk import service. It consumes the
IFC adapter's application-neutral products and adds `ImportedFeature` objects
to the flat Body. `IfcOpenShellAdapter` is isolated under `src/import`; no IFC
dependency reaches CadViewer, SelectionAdapter, VisibilityManager, or core
feature builders.

The C1 GUI path runs `IfcImporter::prepare()` asynchronously. It transfers a
complete prepared result back to the GUI thread, constructs model features,
and commits them with the generic `Body::appendFeatures()` path through one
`ImportFeaturesCommand`. This keeps the current project transactional while
the worker runs and avoids per-product recompute/presentation updates.

`VisibilityManager` owns visibility policy and presentation metadata. It
calculates the effective `Visible`, `Ghosted`, or `Hidden` mode from persistent
feature state, dependencies, isolation, groups, filters, spatial rules, and
Ghost Others. It also owns visibility presets and saved-view metadata. It does
not depend on AIS or widgets.

`FeatureEditingService` is the application-facing interface used by the tree
and property editor. `SelectionResolver` maps stable selection IDs and
topological references to application operations.

## Commands and undo

`ModelingController` pushes model and visibility commands to `QUndoStack`.
Feature creation, deletion, property changes, placement changes, and persistent
visibility/group/filter/preset changes are command-driven. Temporary isolation,
Ghost Others, spatial visibility, section clipping, camera state, and saved-view
restore are presentation state and are not modeling features.

## Interactive parametric operations

Interactive modeling tools use the same four-phase lifecycle:

```text
begin -> updatePreview -> commit or cancel
```

`InteractiveOperationSession` owns the transient operation context: operation
kind, source IDs, optional edit target, original parameters, and temporary
parameters. It does not
own a `TopoDS_Shape`, `Body`, or AIS object. Viewer tools may keep operation-
specific transient presentation caches for performance, but those caches are
discarded by cancel and are never written to the parametric model.

Creation commits go through `ModelingController` and an add-feature command.
MainWindow and CadViewer share one session instance, so switching tools,
selection, Esc, and project replacement cancel the same transient operation.
Editing an existing feature goes through the same controller boundary and
`ChangeParametricPropertyCommand`, so one completed edit produces one undo
entry and marks only the affected dependency branch dirty. The
`FeatureEditorPanel` does not mutate feature geometry directly.

Input validation belongs to the modeling/operation layer and is reused by
action-state checks and commit paths. Persistent topology identity belongs to
feature parameters (`TopologicalReference`); transient OCCT subshapes are
valid only while building a preview or resolving the current result.

After commit or property edit, the normal path is:

```text
ModelingController -> Body dirty/recompute -> ModelPresenter -> CadViewer
```

Extrude, Pocket, Fillet, Chamfer, Shell, Revolve, and Sweep use the session for
staged operation input; Push/Pull additionally uses it for its lightweight AIS drag
preview. Their geometry construction remains in the corresponding feature and
controller code. This keeps preview responsive while making persistent results,
selection, and Undo/Redo use one model refresh pipeline.

Revolve's axis identity remains in the model layer: global axes are enum values,
Sketch axes use stable entity IDs, and model-edge axes use persistent topology
references. The UI stages the angle and global-axis choice but never stores a
transient OCCT axis as history.

Sketch-line axis picking is a dedicated viewport path. `SketchEntityPicker`
projects the source Sketch's local entities through its current frame and the
camera, resolves the nearest Line to a stable `SketchEntityId`, and consumes
the click before ordinary OCCT selection. Model Edge axes use the normal typed
OCCT Edge path; the two identities are intentionally not conflated.

Sweep paths use the same distinction. `SweepPathDefinition` stores either a
Model Edge owner plus `TopologicalReference`, or a Sketch ID plus stable
`SketchEntityId` values. `SketchPathBuilder` independently transforms selected
Line/Arc entities through the Sketch frame, orders one connected open chain,
and creates a `TopoDS_Wire`. Both path types then use the common Sweep feature
and `BasicFeatures::sweep` backend.

## Parametric model

`Body` is the canonical ordered history owner. It contains shared
`ParametricFeature` instances, validates insertion/dependency order, propagates
dirty state, recomputes the ordered history, and exposes errors.

`ParametricFeature` owns:

- stable string ID and name;
- feature parameters in derived classes;
- dependency pointers;
- placement (`gp_Trsf`);
- generated `TopoDS_Shape`;
- `Dirty`, `UpToDate`, or `Failed` state;
- persistent `userVisible()` state.

Recompute calls the feature's `build()` implementation and then applies its
placement to the result. A feature's generated shape is a result, not the
editable definition. Dependencies remain part of the parametric model even if
their presentations are hidden.

Current feature classes include primitives (Box, Cylinder, Cone, Sphere,
Torus, Hexagon), Sketch, Face, Extrude, Pocket, PushPull, Revolve, Boolean,
Fillet, Chamfer, Shell, Offset, Loft, Sweep, LinearPattern, and PathPattern.
The exact persistence/UI support of individual advanced features is described
in [PARAMETRIC_FEATURES.md](PARAMETRIC_FEATURES.md) and
[PCAD_FORMAT.md](PCAD_FORMAT.md).

`ImportedFeature` is a read-only feature for native imported geometry. Its
world-normalized B-Rep is stored with identity placement and its IFC building,
storey, class, and GlobalId are metadata; IFC hierarchy does not become model
history.

The read-only BIM Inspector consumes the structured IFC metadata retained by
`ImportedFeature` (identity, type, materials, property sets, and quantities).
It is a presentation projection and never reparses the IFC source.

`Document` and legacy `Feature` remain for compatibility with old callers and
legacy Box/Cylinder input. They are not the canonical presentation or feature
history container.

## Geometry and presentation

Feature builders in `src/operations` use OCCT algorithms and return
`TopoDS_Shape`. `CadViewer` wraps those shapes in `AIS_Shape` presentations.
`SetShape()`/`Redisplay()` update geometry after a model refresh; visibility
operations use only the existing `Display()`/`Erase()`/transparency paths.

The viewer keeps an authoritative per-feature presentation mode for interaction
eligibility. Ghosted objects remain displayed but are excluded from selection,
snap, transforms, and Push/Pull. Hidden objects are erased.

## Selection flow

```text
OCCT AIS detection/selection
        |
SelectionAdapter / OcctSelectionAdapter
        |
CadViewer::SelectionState
        | stable feature IDs + topology references
        v
MainWindow / application selection projection
        |
FeatureEditorPanel tree selection
```

The reverse tree flow is:

```text
FeatureEditorPanel IDs -> MainWindow -> CadViewer::selectFeatures()
```

OCCT selection is the picking mechanism, while `CadViewer::SelectionState` is
the normalized interaction state. `SelectionState::primary` is the sole
transform source and is also the source feature used to build snap references.
Topology selection is restored through feature IDs and geometry-signature
`TopologicalReference` values; raw `TopoDS_Shape` identity is transient.

While a Sketch is being edited, its persistent model presentation is excluded
from normal model selection management. Finish Sketch clears edit-only state,
re-registers the presentation, and restores the Sketch as an object selection.
Selection activation is object-scoped and incremental; `SetShape()` releases
the old managed selector before replacing geometry so OCCT cannot retain stale
selection structures. Diagnostic lifecycle output is disabled by default and
can be enabled with `PARAMETRICCAD_TRACE_SELECTION=1`.

## Visibility architecture

The effective policy precedence is:

1. technical, persistent, or dependency-hidden;
2. isolation;
3. group restrictions;
4. type, role, and category filters;
5. spatial restriction;
6. Ghost Others;
7. visible.

`Hidden` is stronger than `Ghosted`, which is stronger than `Visible`.

`VisibilityManager` evaluates policy over the current Body and caches the
previous effective mode per feature. It emits `VisibilityChange` deltas.
Policy evaluation is currently O(N), while `CadViewer` applies only changed
feature presentations in O(K), where K is the number of changed features.

Visibility-only operations do not call `Body::recompute()`, replace shapes, or
rebuild geometry. Snap invalidation remains conservative/global after a
visibility change so hidden and Ghosted references cannot remain candidates.

## Spatial visibility

`SpatialVisibilityRule` is temporary policy state owned by
`VisibilityManager`. The current relation is `Intersects`; features are tested
against cached world-space `Bnd_Box` values. `ParametricFeature::shape()` has
already received placement after recompute, so the cached bounds match the
world-space presentation. Outside features are either Hidden or Ghosted.

The interactive spatial box has six extent handles. It is a dedicated viewer
presentation, not a Body node, feature, snap target, or modeling command. Box
movement reevaluates visibility and applies deltas without recompute. The
active spatial rule is not persisted.

## Section clipping

Section clipping is separate from spatial feature filtering. `CadViewer` owns a
temporary `Graphic3d_ClipPlane` attached to `V3d_View`. It supports Section X,
Y, Z, Flip, and Clear, plus a wireframe plane grid, center handle, normal arrow,
and normal-constrained dragging with offset feedback.

Visibility decides whether an AIS presentation exists and whether it is normal
or Ghosted. Clipping decides which part of an already displayed presentation is
visible. Section clipping applies to Ghosted presentations too, does not
resurrect Hidden features, does not modify B-Rep geometry, and does not call
recompute. The plane/grid/handle/arrow are non-selectable and do not participate
in snap or transforms.

## Saved views

`SavedView` is presentation metadata owned by `VisibilityManager`, not a
`ParametricFeature`. It is persisted in the root `views` array and captures:

- camera eye, center, up vector, and scale;
- persistent visibility configuration;
- isolation and Ghost Others state;
- spatial rule;
- section axis, origin, and flip state.

Selection, hover, active transform drag, snap candidates, and AIS handles are
not saved. Restoring a view applies visibility and temporary presentation state,
restores camera/section state through `CadViewer`, clears transient selection,
and never recomputes the Body.

## Project persistence

`ProjectFile` is the serialization boundary. The logical version-1 manifest
stores the canonical Body history, parameters, stable IDs, dependency IDs,
placement, persistent feature visibility, and optional visibility metadata. New
files are ZIP-compatible `.pcad` containers containing `manifest.json` and
individual compressed raw B-Rep entries under `geometry/`. Legacy version-1
plain JSON files, including embedded Base64 B-Rep payloads, remain readable.
Geometry, AIS handles, camera state outside saved views, selection, hover, snap
caches, and undo history are not serialized.

Archive handling is isolated in `ProjectArchive`; `ShapePayload` provides raw
and legacy embedded B-Rep encoding. `ImportedFeature`, `Body`, and modeling
code do not know about ZIP entries. Saves are written to a temporary archive,
closed and then replaced as a completed project file. Archive paths are
validated to reject absolute and traversal entries.

Load validates and assembles temporary state first. The active model and
visibility manager are replaced only after successful parsing and validation.
See [PCAD_FORMAT.md](PCAD_FORMAT.md).

## Project Open and bulk synchronization

The current Open path is single-threaded for recompute and presentation, but
uses batching and event-loop yielding:

1. a worker reads/parses/validates the file and builds a temporary Body without
   AIS access;
2. the GUI thread incrementally recomputes the temporary Body with a time
   budget per continuation;
3. the active document/body/visibility state is replaced atomically;
4. CadViewer presents features in bulk chunks using deferred viewer updates;
5. stale presentations are removed, visibility is projected, selection and
   snap state are synchronized, and FitAll is performed once.

OCCT viewer objects and Qt widgets remain on the GUI thread. The worker does
not construct AIS objects or touch the active viewer.

## Transform and Snap

`TransformGizmo` handles viewer-side Move/Rotate interaction. `TransformMath`
converts screen movement into world transforms. `SnapManager` builds cached
endpoint, midpoint, and bounded intersection references for the selected
primary feature and uses screen-space projection/hysteresis during preview.

Transform preview updates AIS locations (`SetLocation`) and does not mutate the
model. On commit, `ModelingController` creates a placement command; the command
updates `ParametricFeature::placement()` and participates in undo/redo. Placement
is persisted in `.pcad`.

## Recompute and presentation separation

```text
ParametricFeature::recompute()
    -> changes canonical model geometry

ModelPresenter::refreshModel()
    -> recompute, update shapes, cache bounds, project presentation

ModelPresenter::refreshVisibility()
    -> policy evaluation and AIS visibility delta only

CadViewer section movement / saved-view restore
    -> viewer state only
```

Visibility, spatial-box movement, section movement, and saved-view restoration
must not trigger Body recompute.

## Related documents

- [Project format](PCAD_FORMAT.md)
- [Performance](PERFORMANCE.md)
- [Parametric features](PARAMETRIC_FEATURES.md)
- [Undo/Redo](UNDO_REDO.md)
- [Roadmap](ROADMAP.md)
