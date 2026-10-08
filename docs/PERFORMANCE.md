# ParametricCAD Performance

## Large-project problem

Hundreds of independent placed features make repeated AIS and viewer updates
more expensive than JSON parsing alone. The main stress fixture is:

```text
examples/house_1_5_storey_side_dormer.pcad
```

It is a detailed timber-frame model used for large-project Open, placement,
visibility, spatial, section, and saved-view checks.

## Current Open path

Project Open is currently single-threaded for recompute and presentation, but
uses a worker parse phase and GUI-thread bulk continuations:

```text
ProjectController / ProjectFile
    -> worker reads, parses, validates, assembles temporary Body
    -> GUI incrementally recomputes with a time budget
    -> active Document/Body/VisibilityManager replacement
    -> CadViewer bulk presentation chunks
    -> final visibility, selection, snap synchronization, FitAll
```

The worker path calls `ProjectFile::load(..., recompute = false)` and never
touches `AIS_InteractiveContext`, `V3d_View`, or Qt widgets. Recompute,
`AIS_Shape` construction, selection activation, cache synchronization, and
`FitAll()` stay on the GUI thread. `QTimer::singleShot(0, ...)` yields between
time-budgeted GUI continuations; this is batching, not multithreaded OCCT
recompute.

## Bulk-update invariants

Bulk model presentation should:

- avoid `UpdateCurrentViewer()` for every feature;
- avoid `FitAll()` for every feature;
- defer selection-mode activation;
- defer or coalesce snap-cache invalidation;
- update/remove stale AIS presentations once per synchronization phase;
- apply one final visibility projection and restore selection at the end.

`ModelPresenter::refreshModel()` is the full model path. It recomputes the Body,
updates feature shapes, refreshes the spatial bounding-box cache, retains
current presentations, applies visibility, and restores selection.

`ModelPresenter::refreshVisibility()` is presentation-only. It evaluates
`VisibilityManager` policy without calling `Body::recompute()` and forwards
only `VisibilityChange` deltas to `CadViewer`.

## Visibility and spatial cost

Visibility policy evaluation is currently O(N) over Body features. The manager
caches previous effective modes and emits only changed feature IDs. Viewer
presentation work is O(K), where K is the number of changed modes.

Spatial visibility uses cached world-space `Bnd_Box` values. A model refresh
rebuilds the cache; camera changes and visibility-only changes do not. Spatial
box movement evaluates cached boxes and sends visibility deltas without
rebuilding topology or geometry.

Snap invalidation after visibility changes remains conservative/global. This
preserves correctness for hidden and Ghosted candidates while the viewer update
itself remains incremental.

## Tests and diagnostics

The viewer has opt-in diagnostics for large-model investigations. Start the
IFC-enabled application with:

```bash
PARAMETRIC_CAD_PERF=1 ./build/src/ParametricCAD
```

This reports the OpenGL vendor/renderer/version when available, AIS and
selectable presentation counts, bulk refresh and selection activation timing,
tree rebuild timing, FitAll timing, aggregated hover `MoveTo` and navigation
timings, and snap reference/candidate counts when a transform is started.
The following controlled experiments are available without changing model
semantics:

```text
PARAMETRIC_CAD_DISABLE_SNAP=1       # skip transform snap reference/candidate work
PARAMETRIC_CAD_DISABLE_SELECTION=1  # keep geometry displayed, disable OCCT picking
PARAMETRIC_CAD_DISPLAY_MODE=shaded  # shaded faces without edge boundaries
PARAMETRIC_CAD_DISPLAY_MODE=wireframe
```

The display and interaction switches are diagnostic only; defaults are
unchanged. Camera navigation does not invoke model recompute, visibility
policy evaluation, tree rebuild, or snap topology generation. Hover picking is
already bypassed while orbiting/panning; the instrumentation measures the
remaining `MoveTo` cost when the mouse is stationary.

The model tests include deterministic spatial evaluation counts for 100, 500,
and 5000 synthetic features. They avoid wall-clock assertions. Relevant
invariants include:

- visibility-only operations never recompute the Body;
- unchanged effective modes are not sent as viewer changes;
- spatial bounds include feature placement;
- project replacement rebuilds current presentation state;
- stale features do not survive presentation retention.

Run:

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Current limits and future concerns

The current O(N) policy and bounding-box scans are appropriate for hundreds and
low thousands of features. Future work may add affected-set indexes, partial
snap invalidation, a spatial index, or more granular tree updates. True section
clipping is already viewer-level through OCCT `Graphic3d_ClipPlane`; it does not
alter model geometry. These are scaling concerns, not reasons to duplicate
model state or recompute hidden features.
