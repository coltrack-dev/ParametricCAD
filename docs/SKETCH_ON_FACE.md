# Sketch on Face

`SketchFeature` keeps its geometry in local two-dimensional coordinates. A
global sketch uses the existing XY rectangle behavior (with XY/XZ/YZ support
metadata); an attached sketch stores a `TopologicalReference` to exactly one
planar Face and a deterministic `SketchFrame`.

The frame uses the Face centroid as origin. Its normal follows the oriented
`TopoDS_Face`; the X axis is built from a fixed global axis that is least
parallel to the normal, and Y is `normal × X`. This makes the frame
right-handed and independent of OCCT subshape enumeration order.

During recompute the Face reference is resolved first and the current frame is
derived again. Lines, circles, and rectangles are then converted from local
coordinates to OCCT edges. A missing or ambiguous support fails the Sketch;
the sketch is never moved to an arbitrary Face.

The viewer's sketch mode aligns the camera to the frame and converts mouse
screen points through a camera ray/plane intersection into `(u, v)` coordinates.
Line, circle, and rectangle additions use the regular application `QUndoStack`.

This is intentionally a foundation: constraints, curved-face support,
projection geometry, trimming, and a full sketch solver are separate work.
