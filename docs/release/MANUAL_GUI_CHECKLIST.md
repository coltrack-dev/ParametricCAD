# ParametricCAD v0.1.0-alpha manual GUI checklist

Run against a Release build. Record `PASS`, `FAIL`, `BLOCKED`, or `NOT RUN`,
with OS, GPU/driver, display scale and commit/build directory.

## Ubuntu 24.04

- [ ] Launch ParametricCAD with the normal desktop OpenGL backend.
- [ ] Create Sketch on XY; enter and exit Sketch mode.
- [ ] Draw a rectangle; finish the sketch; verify no synthetic rectangle is
      shown before user geometry exists.
- [ ] Select Object, Face, Edge and Vertex; verify selection highlighting.
- [ ] Undo and redo sketch creation/entity insertion.
- [ ] Create an extrusion, face-attached sketch and pocket.
- [ ] Save, close/reopen, recompute and verify the feature tree and geometry.

## Windows 11

- [ ] Repeat the Ubuntu workflow.
- [ ] Test 100%, 125% and 150% display scaling.
- [ ] Verify mouse selection at the upper-left, center and lower-right of the
      viewer; no systematic coordinate offset is allowed.
- [ ] Verify wheel zoom direction, magnitude and cursor-centered behavior.
- [ ] Test X-Ray Face selection and switching back to normal display.

## macOS Intel

- [ ] Launch from the packaged `.app` executable.
- [ ] Repeat the sketch, selection, extrusion, pocket and save/reopen workflow.
- [ ] Test X-Ray front/back Face selection and sketch editing orientation.
- [ ] Verify OCCT and Qt rendering after repeated recompute and undo/redo.
- [ ] Verify IFC import if the release bundle includes IfcOpenShell runtime
      libraries and plugins.

## Evidence to record

- OS version, CPU/GPU, display scale and Qt/OpenGL backend.
- Exact build and launch command.
- Screenshots or short recordings for failures.
- Whether each workflow was performed with a clean project and after reopening.
