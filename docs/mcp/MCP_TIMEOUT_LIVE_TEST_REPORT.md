# MCP Long-Running Operation Validation

Date: 2026-10-17

## Root cause

The MCP stdio client used a fixed 5,000 ms `QLocalSocket::waitForReadyRead()` timeout for every tool. The GUI automation service executes `handleAutomationRequest()` synchronously on the Qt GUI thread. For `cad.open_project`, the project load/recompute was fast, but the subsequent model presentation refresh was not:

- Controller/project load and recompute: about 0.33 s for the house.
- Presentation refresh and final viewer synchronization: about 22.1 s.
- Total GUI request duration: about 22.5 s.

The client therefore returned `-32002` while the GUI continued and committed the operation. The response was not lost; it was produced later by the GUI service. The original timeout was insufficient for the synchronous GUI operation.

## Fix

- Short tools retain the 5-second timeout.
- `cad.open_project` and `cad.save_project` use a 120-second timeout.
- `PARAMETRICCAD_MCP_TIMEOUT_MS` provides a controlled client-side timeout override for diagnostics and integration tests.
- `PARAMETRICCAD_TRACE_MCP=1` logs request ID, tool, timeout, client elapsed time, GUI receipt, controller duration, presentation duration, dispatch duration, serialized response size, and flush state.
- Existing GUI-thread serialization is preserved: the automation service processes one request at a time, so a timed-out client cannot overlap a subsequent project mutation in the GUI.

No viewport rendering code was changed in this fix.

## Live setup

```bash
DISPLAY=:0 QT_QPA_PLATFORM=xcb \
PARAMETRICCAD_ENABLE_AUTOMATION=1 \
PARAMETRICCAD_AUTOMATION_SOCKET=/tmp/parametriccad-timeout-live \
PARAMETRICCAD_TRACE_MCP=1 \
build/src/ParametricCAD --enable-automation
```

The MCP client was the built `build/src/parametriccad-mcp` executable using the same local socket.

## Measured operations

### Small project: `box4.pcad`

Request: `cad.open_project`, JSON-RPC ID `101`.

Client response:

```text
[MCP] request id=101 tool=cad.open_project timeoutMs=120000
[MCP] response id=101 tool=cad.open_project elapsedMs=176 bytes=142
success=true featureCount=3
```

GUI trace:

```text
[MCP] project-open-start id=101 path=.../box4.pcad
Project load: parse: 0 ms, deserialize: 0 ms, recompute: 10 ms, total: 11 ms, features: 3
[MCP] project-open-controller id=101 elapsedMs=11 success=1
[MCP] project-open-finished id=101 elapsedMs=175
[MCP] response-written bytes=141 elapsedMs=175 flushed=1
```

Result: PASS.

### Large project: brick-house split fixture

Fixture: `examples/mcp_brick_house_split_test.pcad`, 245 features.

Request: `cad.open_project`, JSON-RPC ID `104`.

Client response:

```text
[MCP] request id=104 tool=cad.open_project timeoutMs=120000
[MCP] response id=104 tool=cad.open_project elapsedMs=22494 bytes=175
success=true featureCount=245
```

GUI trace:

```text
[MCP] project-open-controller id=104 elapsedMs=333 success=1
[MCP] project-open-finished id=104 elapsedMs=22493
[MCP] dispatch-complete id=104 tool=cad.open_project elapsedMs=22493
[MCP] response-written bytes=174 elapsedMs=22493 flushed=1
```

Result: PASS. The prior 5-second timeout would fail this operation; the 120-second operation-specific timeout completes it.

### Large project save

Request: `cad.save_project`, JSON-RPC ID `105`, destination `/tmp/parametriccad-timeout-live-copy.pcad`.

```text
[MCP] request id=105 tool=cad.save_project timeoutMs=120000
[MCP] response id=105 tool=cad.save_project elapsedMs=21971 bytes=137
success=true featureCount=245
```

Result: PASS.

### Forced client timeout

The same large-house open was run with `PARAMETRICCAD_MCP_TIMEOUT_MS=1000`, JSON-RPC ID `106`.

Client:

```text
[MCP] request id=106 tool=cad.open_project timeoutMs=1000
[MCP] timeout id=106 tool=cad.open_project elapsedMs=1001
error=-32002 Timed out waiting for ParametricCAD
```

GUI:

```text
[MCP] project-open-controller id=106 elapsedMs=334 success=1
[MCP] project-open-finished id=106 elapsedMs=22423
[MCP] response-written bytes=174 elapsedMs=22425 flushed=1
```

Result: PASS for failure-path detection. The client reports timeout, while the GUI completes the already-started operation. This is explicit in diagnostics and does not overlap with a subsequent mutation.

### Follow-up after timeout

`cad.get_application_status`, JSON-RPC ID `107`, was sent after the forced timeout. It returned `featureCount=245`, `dirty=false`, and the live application PID. Result: PASS; the application remained stable and the next request was accepted after the previous GUI request completed.

## Request lifecycle instrumentation

Instrumented stages:

1. MCP client request ID/tool/selected timeout.
2. GUI IPC receipt.
3. `ProjectController` operation duration.
4. GUI presentation and refresh duration (the difference between controller and finished timestamps).
5. Full GUI dispatch duration.
6. JSON serialization size and socket flush state.
7. MCP client response or timeout elapsed time.

## Concurrency and disconnect behavior

The service remains serialized on the GUI event loop. `CadAutomationService::readClient()` does not dispatch a second request while the current handler is executing. This prevents overlapping project mutations, including after a client disconnects during a long operation. A later client reconnects to the same QLocalServer and receives the next response after the active operation completes.

No cancellation tool was added: the current synchronous operation cannot be safely cancelled once `ProjectController` has started mutating/replacing the project. Reporting cancellation without rollback would be incorrect. A future asynchronous operation API should use operation IDs, explicit commit state, and cancellation before commit.

## Verification commands

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
git diff --check
```

Final result: **100% CTest pass, 29/29 tests passed**.

The live timeout scenarios above were executed against the real GUI process and real MCP stdio server, not mocks.

