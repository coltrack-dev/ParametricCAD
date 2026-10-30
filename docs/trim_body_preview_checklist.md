# Trim Body preview checklist

Manual GUI regression checklist:

- Open `examples/house_facade_photo.pcad` and select a gable OSB component.
- Open `Trim Body by Plane`; confirm the cyan translucent plane and green/red
  kept/discarded preview are visible.
- Change `Keep positive` / `Keep negative` and offset; confirm the AIS preview
  changes immediately while the model tree and feature count stay unchanged.
- Select a planar face in the viewer, use `Pick planar face`, and confirm the
  preview plane follows that face.
- Press Cancel or Esc; confirm no preview plane or colored temporary solids
  remain.
- Apply once; confirm exactly one modeling command is added to Undo history.
- Undo and Redo; confirm the model updates and no old preview AIS objects
  remain.
- Repeat on both gable sides using the 60° and 25° roof planes.

The automated AIS test covers creation, replacement, explicit clearing, and
viewer-wide cleanup of transient Trim presentations. Model-side Trim tests
cover the actual BRep result and document invariants; the preview callback only
uses temporary `TrimBodyFeature` instances and never adds them to the Body.
