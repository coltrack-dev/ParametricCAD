# AGENTS.md

## Project
ParametricCAD is a C++20 / Qt6 / OpenCASCADE desktop CAD application.

## General rules
- Make minimal, focused changes.
- Do not rewrite unrelated code.
- Do not create patch files unless explicitly requested.
- Do not create scripts that modify source files.
- Do not commit changes unless explicitly requested.
- Preserve existing comments.
- Preserve existing file permissions unless a permission change is required.
- Do not run formatters across unrelated files.

## Architecture
- `Document` owns the parametric model.
- `Feature` and its subclasses represent editable parametric objects.
- `CadViewer` is responsible for OpenCASCADE visualization and selection.
- `FeatureEditorPanel` edits feature parameters.
- `MainWindow` coordinates UI actions and document/viewer interaction.
- Avoid putting model serialization logic directly into `CadViewer`.

## Persistence
- Save the parametric model, not only `TopoDS_Shape`.
- File format extension: `.pcad`.
- Persistence code should be isolated from UI code where practical.
- Loading must validate data before replacing the current document.
- Preserve editability of loaded features.

## Existing functionality
Do not break:
- shape/face/edge/vertex selection;
- FeatureEditorPanel;
- BoxFeature;
- CylinderFeature;
- OpenCASCADE viewer interaction.

## Build and verification
After changing C++ code:
1. Run:
   `cmake --build build -j`
2. Fix compilation errors caused by the changes.
3. Run:
   `git diff --check`
4. Run:
   `git status --short`

Do not commit automatically.

## Scope
Before editing:
- inspect the relevant existing files;
- prefer modifying the smallest number of files necessary;
- do not inspect or modify unrelated modules unless required.
