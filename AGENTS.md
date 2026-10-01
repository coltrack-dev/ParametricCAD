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
- Follow the existing code style and naming conventions.
- Prefer fixing the existing design over introducing parallel implementations of the same functionality.

## Architecture
- `Document` owns the parametric model.
- `Feature` and its subclasses represent editable parametric objects.
- Features are responsible for creating and rebuilding their OpenCASCADE geometry.
- `CadViewer` is responsible for OpenCASCADE visualization and selection.
- `FeatureEditorPanel` edits feature parameters.
- `MainWindow` coordinates UI actions and document/viewer interaction.
- Keep model logic separate from visualization and Qt UI code where practical.
- Do not put feature geometry construction into `CadViewer`.
- Avoid putting model serialization logic directly into `CadViewer`.

## Model changes
- Changes to feature parameters must rebuild the affected geometry correctly.
- After model geometry changes, ensure the viewer is updated appropriately.
- Do not introduce model mutations that bypass existing document-level mechanisms without a good reason.
- When implementing Undo/Redo-related functionality, prefer document-level or command-level changes rather than storing undo state in UI widgets.

## OpenCASCADE
- Check OCCT operation results for failure where applicable.
- For boolean operations, verify `IsDone()` when supported.
- Validate resulting `TopoDS_Shape` objects with `IsNull()` where appropriate.
- Preserve the semantic order of boolean operands.
- For a cut operation, treat it as:

  `result = base - tool`

- Do not silently swap operands to make an operation succeed.
- Report invalid geometry or failed operations clearly instead of silently producing an incorrect model.

## Persistence
- Save the parametric model, not only `TopoDS_Shape`.
- File format extension: `.pcad`.
- Persistence code should be isolated from UI code where practical.
- Loading must validate data before replacing the current document.
- Preserve editability of loaded features.
- Preserve backward compatibility with existing `.pcad` files where practical.
- Before changing serialization or the `.pcad` format, inspect:
    - `PCAD_FORMAT.md`
    - `PARAMETRIC_FEATURES.md`
      if those files are present.

## Existing functionality
Do not break:
- shape/face/edge/vertex selection;
- FeatureEditorPanel;
- BoxFeature;
- CylinderFeature;
- existing parametric features;
- OpenCASCADE viewer interaction;
- save/load functionality;
- existing keyboard and mouse viewer controls.

## Build

If the build directory does not exist, configure with:

`cmake -S . -B build -G Ninja`

Build with:

`cmake --build build -j`

Do not introduce another build system.

## Build and verification
After changing C++ code:

1. Run:
   `cmake --build build -j`

2. Fix compilation errors caused by the changes.

3. Run:
   `git diff --check`

4. Run:
   `git status --short`

5. Inspect:
   `git diff`

Do not claim that something was built or tested unless the corresponding command was actually run successfully.

If tests relevant to the changed code exist, run them.

## Git policy
- NEVER create Git commits automatically.
- Do not run `git commit` unless the user explicitly asks to perform the commit in the current task.
- Do not amend, squash, rebase, reset, push, or create tags unless explicitly requested.
- It is allowed to run read-only Git commands such as:
    - `git status`
    - `git diff`
    - `git log`
    - `git show`
    - `git ls-tree`
- Leave all code changes uncommitted so the user can review them first.
- After completing a meaningful code change, suggest a concise Git commit message for the user to run manually.
- The suggested commit message should describe only the changes made in the current task.

## Scope
Before editing:
- inspect the relevant existing files;
- understand the current implementation before replacing it;
- prefer modifying the smallest number of files necessary;
- do not inspect or modify unrelated modules unless required;
- reuse existing abstractions when they already solve the problem.

## Final response
After completing a code task, report:
- what was changed;
- which files were modified;
- build result;
- tests or verification commands that were actually run;
- any known limitation or follow-up work;
- a suggested Git commit message.

Keep the final report concise.
