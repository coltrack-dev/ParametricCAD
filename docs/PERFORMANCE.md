# ParametricCAD Performance

## Large Project Loading

### Problem

Small projects did not make the loading cost noticeable. A large project with
approximately 500 features caused a long GUI stall during Open, and Ubuntu could
show its “application is not responding” warning while the Qt window was busy.

The problem was primarily the amount of repeated GUI/viewer work, rather than the
JSON file size alone.

### Test Model

The main manual fixture is:

```text
examples/house.pcad
```

It is a one-and-a-half-storey timber-frame house demo with hundreds
of independent framing features, many placements, arbitrary rotations, roof
framing and dormer framing. The exact feature count is allowed to change with the
demo. In the current checkout the automated load check reports 214 body features;
the dedicated extended regression fixture contains 489 Box features.

The model is useful because hundreds of simple boxes exercise the same persistence,
recompute, AIS and selection paths as a more complex model without making the
stress test depend on one difficult Boolean operation.

### Original Bottleneck

Before the current loading path, `ProjectFile::load()` recomputed the temporary
Body and the subsequent normal `ModelPresenter::refresh()` recomputed it again.
The presentation loop then updated each feature through `CadViewer`.

The confirmed expensive repeated operations were:

- AIS `Display()`/`Redisplay()` work for each feature;
- selection-mode activation from `display()` for each newly created AIS object;
- `UpdateCurrentViewer()` reached through that repeated selection activation;
- snap-reference/cache invalidation during individual presentation changes;
- one coarse full presentation synchronization on the GUI thread.

`FitAll()` was not executed once per feature in the old path; it was performed as a
final refresh action. It is nevertheless measured separately now because final
viewer work must not be assumed to be cheap.

### Current Loading Pipeline

```text
MainWindow
    |
    v
ProjectController::loadProject
    |
    v
ProjectFile::load(recompute = false)
    |  worker: read / parse / validate / build temporary Body
    v
QFutureWatcher completion
    |
    v
Body incremental recompute
    |  GUI: one feature at a time, ~30 ms time budget
    v
atomic active Document/Body replacement
    |
    v
old presentation cleanup
    |
    v
CadViewer bulk presentation chunks
    |  Display/Redisplay without immediate viewer update
    v
final visibility, selection and snap-cache synchronization
    |
    v
one FitAll()
```

The recompute and presentation continuations use `QTimer::singleShot(0, ...)` to
return to the Qt event loop between chunks. They are not fixed-size batches: the
current loop continues until approximately the 30 ms time budget is reached,
while always processing at least one feature when work remains.

### Bulk Update Rules

During bulk model operations:

Do not:

- call `UpdateCurrentViewer()` per feature;
- call `FitAll()` per feature;
- rebuild snap references per feature;
- rebuild global selection structures unnecessarily;
- trigger full presentation synchronization for every insertion.

Prefer:

- modify or build the temporary model;
- mark required state dirty;
- recompute and present in time-budgeted continuations;
- use `Display(..., Standard_False)` and deferred `Redisplay()` updates;
- activate selection and synchronize caches at batch boundaries;
- perform final visibility/selection synchronization and one `FitAll()`.

These rules apply beyond Open Project. They are relevant to future Pattern and
assembly features, STEP import, duplication, scripted geometry generation and any
operation that creates many model objects.

### Threading

The current implementation has a mixed threading model:

| Work | Thread |
| --- | --- |
| File read | Worker thread |
| JSON parsing and validation | Worker thread |
| Parametric feature construction and temporary Body assembly | Worker thread |
| Recompute and OCCT shape construction | GUI thread |
| AIS object construction | GUI thread |
| Selection activation/restoration | GUI thread |
| Snap-reference invalidation/cache work | GUI thread |
| `AIS_InteractiveContext`, `V3d_View`, `UpdateCurrentViewer()` and `FitAll()` | GUI thread |
| Qt widgets and progress indicator | GUI thread |

The worker path is limited to `ProjectFile::load(..., recompute = false)`. It does
not use `AIS_InteractiveContext`, `V3d_View` or QWidget APIs. The code does not
claim that arbitrary OCCT geometry/model operations are thread-safe. In
particular, recompute remains on the GUI thread, where it is time-budgeted rather
than moved to an arbitrary worker.

### Performance Testing

Use `examples/house.pcad` for manual Open/viewport testing. The automated
`project_file_tests` test covers:

- a generated 489-Box large-project load with ordered progress and feature-count
  checks;
- loading the repository `examples/house.pcad` when run from `build/tests`;
- a 35-degree rotated placement round-trip;
- rounded placement coefficients and subsequent recompute;
- failed-load preservation of the existing model.

The loader emits stage timings through `qInfo()`. During GUI Open it also emits
per-chunk diagnostics containing feature count, recompute or presentation time,
AIS update time, selection activation time and chunk total. The final diagnostic
includes parse, deserialize, recompute, cleanup/replacement, presentation, final
synchronization, `FitAll`, maximum event-loop heartbeat interval, continuation
count and total load time.

Important invariants are:

- invalid input or failed recompute does not replace the active project;
- successful Open preserves feature definitions, placements and visibility;
- successful Open clears the undo stack according to existing Open semantics;
- selection and snapping remain viewer-side operations after presentation is
  synchronized;
- no full viewer refresh is requested for every feature.

The timing diagnostics are the source of truth for future regressions. A total load
time improvement is useful, but the primary responsiveness criterion is that the
GUI event loop continues to receive control between time-budgeted chunks.
