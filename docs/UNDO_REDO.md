# Undo / Redo

One QUndoStack belongs to MainWindow's current project. Edit -> Undo uses Ctrl+Z;
Redo uses Ctrl+Y and the platform's standard Qt Redo bindings. Actions show the
command name and disable themselves when there is nothing to undo/redo.

## Commands and ownership

`src/commands/FeatureCommands.*` contains:

- AddFeatureCommand: retains a shared feature object and its insertion position.
- RemoveFeatureCommand: retains the selected feature and all transitive dependents,
  restoring the same objects, IDs, dependency links and original Body positions.
  Edit -> Delete (Delete key, also on the toolbar) confirms dependent removal;
  cancelling leaves the model unchanged. The entire cascade is one undo step.
- RemoveDocumentFeatureCommand: removes a legacy Box/Cylinder selected in the viewer
  and restores the same object at its original Document position.
- ChangeFeatureParameterCommand<T, Value>: stores typed old/new values and a setter,
  then marks/rebuilds only the affected dependency branch.
- AddDocumentFeatureCommand: handles the legacy Box/Cylinder creation path without
  moving their ownership into Body. It transfers the same unique_ptr on undo/redo.
- ClearProjectCommand: retains the two container contents in memory and restores
  them on Undo. It does not serialize files or reload the application.

Document and Body keep their existing roles. Their stable container instances
outlive the undo stack; feature references held by commands are owning shared_ptrs
(or unique_ptrs while a legacy feature is absent). History is cleared before model
replacement. Body insertion requires dependencies to precede their consumers.

Parameter edits create a command on editingFinished, not on valueChanged. Repeated
unchanged commits are ignored. Property refresh is deferred to avoid deleting an
editor during its signal, and guarded against creating commands during Undo.
Ctrl+Z/Ctrl+Y inside an editor commits the pending value before operating on the
project stack instead of using the line edit's private text history.

## Model, tree and viewer

Body::markDirtyFrom follows registered dependencies; unrelated features keep their
shapes. QUndoStack::indexChanged synchronizes the current model with the tree and
AIS objects. Existing presentations update only when geometry changes; absent
objects are individually removed. Undo/Redo never fits or clears the whole view.

Cut operand visibility is derived from the current Body by FeatureVisibility.
Undoing/removing a Cut shows its operands; redoing/restoring it hides them again.
Selection of surviving IDs is preserved; deleting a selected feature clears that
selection. Tree/viewer synchronization blocks recursive selection notifications.

## Save and project replacement

Successful Save calls setClean() without deleting history. The window's modified
marker follows QUndoStack::cleanChanged; returning to the saved index removes it.
New and successful Open clear the stack. Failed Open/Save preserve history; Save
rejects a Body that cannot rebuild. .pcad contains only the current model.

Extrude Face is available for a selected Face; its Length editor scales the existing
extrusion direction. The v1 serializer stores sourceFeatureId and vectorX/Y/Z, so
Sketch -> Face -> Extrude remains editable after loading. No undo commands are saved.

## Limits

- Experimental viewer-only Push/Pull is still outside the parametric model/history.
- Dependency order is validated on insertion; there is no arbitrary history reordering.
- Invalid parameter edits are undoable model error states. Undo restores a valid
  earlier value; saving a failed model is rejected.
- Headless tests cover model/commands; panel tests use Qt offscreen, without an OCCT
  display. Camera behavior still needs a manual viewport smoke test.

## Manual smoke test

1. Create Hexagon and Cylinder in the Model panel; set Cylinder radius to 5.
2. Zoom, pan and orbit; create Cut. Undo/Redo and verify operands/result visibility.
3. Edit a dimension, finish editing, then Undo/Redo from both menu and editor focus.
4. Create Rectangle Sketch -> Face -> Extrude Face. Change sketch width, then Undo;
   confirm Face and Extrude update while the camera stays fixed.
5. Delete Sketch with dependent Face/Extrude: cancel first, then confirm; Undo restores
   the entire chain in one step. Delete Cut and verify operands become visible;
   Undo hides them again. Check cleared selection and unchanged camera throughout.
6. Clear Project, Undo, Redo; verify both legacy and Body features return unchanged.
7. Save, edit, Undo; verify the modified marker disappears without losing history.
8. Open a project or choose New; verify Undo/Redo are disabled and old history is gone.
