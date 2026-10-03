# Sketch Extrude and Pocket

`ExtrudeFeature` accepts both the legacy `FaceFeature` profile and a
`SketchFeature` profile. The latter is converted by `SketchProfileBuilder`
into one validated closed wire and planar face. Circle entities and unordered
connected line loops are supported; open, branching, mixed, or disconnected
profiles fail with a readable validation error.

Sketch extrusion follows the current `SketchFrame.normal`, with an optional
reverse flag. `PocketFeature` stores both the target solid and source sketch as
explicit dependencies. It resolves the sketch support face through its
`TopologicalReference`, classifies points on both sides of the face, and uses
the direction entering the target solid for the blind cut.

Both features are ordinary parametric features: their generated B-Rep is not
serialized, dependencies are hidden through the normal presenter rules, and
changes to a sketch, support face, or numeric property trigger recompute.
Legacy Face-to-Extrude JSON remains supported.

The first implementation intentionally supports one closed outer profile,
blind pockets, and solid targets only. Holes, through-all and other end
conditions remain follow-up work.
