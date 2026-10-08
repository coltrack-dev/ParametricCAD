# Parametric Feature Architecture

This document describes the current feature/model boundary. The broader
application architecture is in [ARCHITECTURE.md](ARCHITECTURE.md).

## Canonical model

`Body` owns the ordered canonical history of `ParametricFeature` objects.
Features have stable IDs, names, dependencies, parameters, placement, generated
`TopoDS_Shape`, and `Dirty`/`UpToDate`/`Failed` state. `Document` and legacy
`Feature` remain compatibility APIs; they are not the canonical runtime feature
container.

`ParametricFeature::recompute()` builds the definition and applies placement.
`Body::recompute()` processes the ordered history and reports failures. A
feature's `TopoDS_Shape` is generated output, not the editable definition.

Current feature classes include primitives, Sketch, Face, Extrude, Pocket,
PushPull, Revolve, Boolean, Fillet, Chamfer, Shell, Offset, Loft, Sweep,
LinearPattern, and PathPattern. Persistence and UI support is feature-specific;
the registry in `ProjectFile.cpp` is authoritative.

`ImportedFeature` is the Phase B read-only imported-geometry type. It stores a
world-normalized native OCCT shape with identity ParametricCAD placement and
generic IFC provenance metadata. Its B-Rep payload is serialized by the
persistence layer, so reopening a `.pcad` does not require the source IFC.

## Sketch creation workflow

`Create Sketch` is the single user-facing Sketch creation command. With no
face selected it offers `XY Plane`, `XZ Plane`, and `YZ Plane`. With exactly
one selected planar face it also offers `Selected Planar Face`. Each choice
creates the same `SketchFeature` and immediately enters Sketch edit mode.

Rectangle is a normal Sketch drawing tool, alongside Line, Circle, Arc, Trim,
and Extend; there is no separate Rectangle Sketch modeling feature. Global
planes use `SketchSupportType::XY`, `XZ`, or `YZ`, while face-attached sketches
use `SketchSupportType::Face` and the persistent topological face reference.
`SketchFeature::currentFrame()` remains the common frame used by editing and
downstream operations. Geometry and constraints retain stable `SketchEntityId`
values through save/load. The former `Add Rectangle Sketch` UI action was only
a shortcut for creating the default XY Sketch and is no longer presented.

## Revolve

`RevolveFeature` consumes a validated closed `SketchFeature` profile and stores
an angle in degrees, with sign carrying direction. Partial angles and a full
360-degree revolution are supported. Profile faces are rebuilt from the
current Sketch frame during recompute, so global-plane and face-attached
Sketches use the same local-to-world transformation path as Extrude and
Pocket.

The typed persistent axis can be global X/Y/Z, a stable `SketchEntityId` for a
line in the source Sketch, or a linear model Edge represented by a
`TopologicalReference` and owning feature ID. Missing Sketch lines,
unresolved references, and non-linear model Edges fail deterministically rather
than selecting a replacement. The generated B-Rep is only a result.

Revolve creation and editing use `InteractiveOperationSession` and the
controller command boundary. The current UI stages global-axis and angle
choices before one persistent AddFeature command. Angle and global-axis edits
use the standard FeatureEditorPanel and one Undo/Redo command per edit.

### Revolve workflow

For a Sketch-line axis:

1. Create a Sketch and draw a valid closed profile.
2. Draw a separate Line for the rotation axis.
3. Mark that Line as Construction, then finish the Sketch.
4. Select the Sketch feature in the model tree and activate Revolve.
5. Set `Axis` to `Sketch Line` and press `Pick Axis`.
6. Click the desired construction Line in the viewport. Choosing `Sketch Line`
   does not choose an axis automatically.
7. Confirm that the UI displays the selected Sketch line/entity, set the angle
   (for example, `360` degrees), and create the Revolve.

The profile and axis may belong to the same Sketch. The picked line is stored
by stable `SketchEntityId`; it is not identified by a transient edge index.
Construction geometry remains visible and editable, participates in Sketch
snapping/constraints, and is excluded from profile-boundary extraction. Thus a
closed Rectangle plus a construction Line remains a valid profile, while a
normal open extra Line remains subject to the normal open-profile rules.

`Axis = Model Edge` followed by `Pick Axis` uses ordinary linear OCCT Edge
selection and stores a persistent topology reference. Global X, Y, and Z axes
do not require viewport picking.

### Construction control

The checkable `Construction Line` control has two explicit behaviors. With no
existing Sketch edge selected, it is the drawing default: newly created Lines
are construction geometry while it is enabled. When an existing Sketch Line
is selected, the same control toggles that entity's Construction property;
the separate `Toggle Construction` action remains available as an equivalent
explicit action. The selected entity keeps its `SketchEntityId`, geometry, and
undo/redo history.

## Placement and transforms

Placement is a `gp_Trsf` stored on `ParametricFeature`. The transform gizmo and
`TransformMath` create viewer previews by applying locations to AIS objects.
Preview is not a model mutation. On commit, `ModelingController` pushes a
placement command to `QUndoStack`; the command updates feature placement and
the resulting placement is serialized in `.pcad`.

## Interactive operation lifecycle

Extrude, Pocket, Push/Pull, Fillet, Chamfer, and Shell use the shared
`InteractiveOperationSession` for transient operation state. MainWindow and
CadViewer share the session, while feature-specific code retains ownership of
profile, edge, and face geometry construction.

Creation follows:

```text
begin -> updatePreview -> ModelingController add command -> commit
```

Property editing follows the same boundary through
`ChangeParametricPropertyCommand`. A staged edit records the original feature
ID and parameters; cancel leaves that feature untouched, while commit updates
the existing feature with one undoable command. Repeated preview updates never
create history entries.

Push/Pull has the current lightweight AIS preview. The other operation dialogs
stage and validate their numeric input before commit; they do not recompute the
Body for every keystroke. Shell creation accepts a selected solid and optional
current Face selections, and stores only canonical `TopologicalReference`
values for its openings.

## Selection and snapping boundary

OCCT selection is read through `SelectionAdapter` and normalized by
`CadViewer::SelectionState`. Stable feature IDs are the application identity;
subshape selection is represented with transient topology references and
geometry signatures rather than raw `TopoDS_Shape` identity.

The active primary selection is the only transform source and the source used
by `SnapManager`. `SnapManager` caches endpoint, midpoint, and bounded
intersection references and projects them into screen space during transform
preview. Tree selection is sent back to the viewer by feature ID. Viewer
preview and model commit are separate phases.

## Visibility and viewer state

`ParametricFeature::userVisible()` is persistent feature state. It does not
remove dependencies or suppress recompute. `VisibilityManager` combines it
with technical/dependency hiding, isolation, groups, type/role/category
filters, spatial visibility, and Ghost Others. Its final mode is `Visible`,
`Ghosted`, or `Hidden`.

`ModelPresenter` evaluates policy and sends `VisibilityChange` deltas to
`CadViewer`. `CadViewer` owns AIS presentation state and interaction
eligibility. Ghosted and Hidden features remain in Body and can remain
dependencies; they are simply excluded from selection/snap/transform as
appropriate.

Spatial visibility is temporary feature-level policy based on cached world-space
bounding boxes. Section planes are viewer-only `Graphic3d_ClipPlane` state and
do not modify feature shapes. Neither spatial-box movement nor section-plane
movement recomputes the Body.

Saved views are presentation metadata. They can capture visibility
configuration, isolation, Ghost Others, spatial state, section state, and
camera state, but never become Body nodes or feature dependencies.

## Persistence

`ProjectFile` stores the canonical Body definition, stable IDs, dependency IDs,
placement, and persistent feature visibility. Optional groups, filters,
visibility presets, and root-level saved views are presentation metadata.
OCCT geometry, AIS handles, selection, hover, snap caches, transform preview,
active spatial editing, and active section editing are not serialized.

Loading validates temporary model and presentation state before replacing the
active project. The current file format is version 1; see
[PCAD_FORMAT.md](PCAD_FORMAT.md).

## Recompute rule

Model edits change parameters or placement, mark the affected history dirty,
recompute through `Body`, and then use `ModelPresenter::refreshModel()`.
Visibility-only commands use `refreshVisibility()` and must not call
`Body::recompute()`. Viewer camera, clipping, and saved-view restoration are
also presentation-only operations.

## Sweep (MVP)

`SweepFeature` consumes exactly one valid closed Sketch profile and one model
Edge path. Profile extraction uses the canonical `SketchProfileBuilder`, so
construction geometry is ignored. The path is persisted as its owner feature
ID plus a geometry-signature `TopologicalReference`, then resolved again after
recompute and load. The current orientation is Frenet/tangent-following.

Workflow: select the valid Sketch in the model tree and activate Sweep. The
staged dialog shows the profile and `Path: <not selected>`. Click `Pick Path`,
select one model Edge in the viewport, then click `Commit`; no SweepFeature is
inserted before Commit. Cancel leaves the model unchanged.
Lofting, multiple rails, and a multi-edge Wire picker are outside this MVP.
