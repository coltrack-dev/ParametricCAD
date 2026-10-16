# ParametricCAD Startup Viewport Fix

Date: 2026-10-17

## Root cause

The OCCT view was created while the native Qt/X11 child still reported its initial placeholder size (`100x30`). The first `MustBeResized()` therefore synchronized OCCT with the wrong viewport. A later real Qt resize corrected the native `Xw_Window`, which is why manually resizing the window made the model appear.

There was a second diagnostic problem: `cad.screenshot` used `QWidget::grab()` on a `WA_PaintOnScreen` widget. That captures the Qt wrapper rather than the OCCT OpenGL view and produced a black image even when AIS presentations existed.

The MCP project-open path also refreshed without fitting the camera. Models whose coordinates were far from the origin could therefore be outside the initial camera frame.

## Changes

- Added idempotent OCCT viewport synchronization in `CadViewer`.
  - Rechecks native viewport size after `showEvent`.
  - Rechecks it on `paintEvent` and real `resizeEvent`.
  - Logs logical size, expected physical size, DPR, native readiness and OCCT size under `PARAMETRIC_CAD_PERF`.
  - Does not resize the Qt window, sleep, or schedule a recurring timer.
- Added final `UpdateCurrentViewer()` and `Redraw()` after bulk presentation, guarded for test viewers without a native OCCT window.
- Changed MCP `cad.open_project` to use the normal initial `FitAll` behavior after presentation.
- Replaced `cad.screenshot`'s QWidget grab with OCCT `V3d_View::ToPixMap`, converted to PNG through `QImage`.

Modified source files:

- [`src/viewer/CadViewer.cpp`](../../src/viewer/CadViewer.cpp)
- [`src/viewer/CadViewer.h`](../../src/viewer/CadViewer.h)
- [`src/viewer/MainWindow.cpp`](../../src/viewer/MainWindow.cpp)

## Live validation

Environment: Ubuntu 24.04.4 under VMware, X11/XCB, CMake Ninja Debug build, Qt 6.4.2, OCCT system packages.

Command used for the fresh application:

```bash
DISPLAY=:0 QT_QPA_PLATFORM=xcb \
PARAMETRICCAD_ENABLE_AUTOMATION=1 \
PARAMETRICCAD_AUTOMATION_SOCKET=/tmp/parametriccad-black-fix-live3 \
PARAMETRIC_CAD_PERF=1 \
build/src/ParametricCAD --enable-automation
```

The fixture was opened through the live MCP stdio server without resizing the window:

```bash
printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"cad.open_project","arguments":{"path":"/home/tatarino/projects3/ParametricCAD/examples/mcp_brick_house_split_test.pcad"}}}' \
| PARAMETRICCAD_AUTOMATION_SOCKET=/tmp/parametriccad-black-fix-live3 build/src/parametriccad-mcp
```

The request itself still reports the pre-existing `-32002 Timed out waiting for ParametricCAD` IPC timeout, but the GUI completes the load and renders the model. The post-load view state was fitted to center `[6379.25, -8591.37, 2542.40]`, with viewport `1445x1110`.

The native OCCT screenshot was obtained through `cad.screenshot` and was a nonempty `1445x1110` PNG. Visual inspection shows the complete house geometry on the gray background; no manual resize was performed.

Artifacts:

- Before: [`viewport-before-black.png`](artifacts/viewport-before-black.png) — old QWidget-grab capture, black viewport.
- After: [`viewport-after-native-occt.png`](artifacts/viewport-after-native-occt.png) — native OCCT `ToPixMap` capture with visible house geometry.

Startup diagnostics from the fixed run included:

```text
[VIEWPORT] showEvent widget=(1445,1110) physical=(1445,1110) dpr=1.000 nativeReady=1 occtWindow=(100,30) synchronized=1
[VIEWPORT] paintEvent widget=(1445,1110) physical=(1445,1110) dpr=1.000 nativeReady=1 occtWindow=(1445,1110) synchronized=1
CadViewer bulk refresh: features=182 visible=158 AIS=182 selectable=158
CadViewer performance FitAll samples=1 avgMs=133 maxMs=133
```

The second line confirms that the first paint synchronizes OCCT after the native surface has its real size. The final bulk refresh confirms AIS presentations were updated, and FitAll confirms the model was framed without user interaction.

## Verification

```bash
cmake --build build -j
build/tests/main_window_tests selectionOwnershipSurvivesShapeReplacementWithoutRendering
ctest --test-dir build --output-on-failure
git diff --check
```

Results:

- Build: PASS.
- Previously failing non-native viewer regression: PASS, 3 passed.
- Full CTest: PASS, 29/29 tests passed.
- Fresh GUI/MCP startup without manual resize: PASS, visible native OCCT geometry verified.

## Remaining issue

The live MCP `open_project` call can return a timeout after the GUI has already completed the operation. This is separate from viewport rendering and should be fixed in the IPC request/response timeout contract.

No dedicated CTest target was added for launching a separate GUI process; the no-resize startup check above was executed as a real live integration test against a fresh application process.

