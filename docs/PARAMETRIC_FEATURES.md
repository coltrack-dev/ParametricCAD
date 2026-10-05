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

## Placement and transforms

Placement is a `gp_Trsf` stored on `ParametricFeature`. The transform gizmo and
`TransformMath` create viewer previews by applying locations to AIS objects.
Preview is not a model mutation. On commit, `ModelingController` pushes a
placement command to `QUndoStack`; the command updates feature placement and
the resulting placement is serialized in `.pcad`.

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
