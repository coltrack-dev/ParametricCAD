# ParametricCAD — MCP Runtime Validation

**Дата:** 2026-10-31  
**Область:** запуск GUI, QLocalSocket IPC и stdio MCP runtime

## Итог

IPC-связь восстановлена и воспроизводима при запуске в доступной X11/XWayland
сессии. Минимальный реальный MCP workflow прошёл:

```text
GUI startup → MCP connection → Sketch → Extrude → model inspection → shutdown
```

Полный `mcp_modeling_integration_tests` также подключается к приложению и
выполняет modeling scenarios, но имеет один отдельный функциональный FAIL в
проверке AIS/presentation после Save/Load. Это не ошибка QLocalSocket.

## 1. Архитектура IPC

`MainWindow` создаёт `CadAutomationService`, только если установлена переменная
`PARAMETRICCAD_ENABLE_AUTOMATION=1`. Имя сервера выбирается из
`PARAMETRICCAD_AUTOMATION_SOCKET`, с default `parametriccad-automation`.

`CadAutomationService::start()` вызывает `QLocalServer::listen()` и при
необходимости удаляет stale endpoint после неудачного первого listen.

`src/mcp/main.cpp` использует то же имя socket через
`PARAMETRICCAD_AUTOMATION_SOCKET`. Для каждого tool call MCP executable создаёт
`QLocalSocket` и отправляет внутренний JSON-RPC `automation/call`.

`tests/mcp_modeling_integration_tests.cpp` создаёт уникальный временный socket
path, передаёт его и GUI, и MCP executable, ждёт `waitForConnected()`, затем
отправляет MCP JSON-RPC через stdio. Разных hard-coded socket names в этих
путях не обнаружено.

## 2. Причина исходной ошибки

Исходная ошибка:

```text
QLocalSocket: Invalid name
```

не вызвана различием имён между `QLocalServer` и integration test. Фактическая
причина неработавшего запуска в sandbox была графической:

```text
qt.qpa.xcb: could not connect to display :0
qt.qpa.plugin: Could not load the Qt platform plugin "xcb"
```

GUI завершался до создания `QLocalServer`; MCP client не имел живого endpoint.
Сообщение `Invalid name` было следствием отсутствующего/невалидного runtime
endpoint на стороне client connector, а не дефектом регистрации MCP tools.

Проверка окружения показала: `DISPLAY=:0` задан, `XDG_RUNTIME_DIR` и Wayland
socket существуют, но sandbox не имел доступа к X11. `Xvfb` в окружении не
установлен. После запуска диагностик вне sandbox с корректным `XAUTHORITY`
`xdpyinfo` успешно подключился к X display `:0`.

## 3. Runtime-зависимости

Текущий OpenCASCADE viewer требует X11/XCB display-сессию. `QT_QPA_PLATFORM=offscreen`
запускает часть Qt, но ParametricCAD завершается при создании OCCT display
context с `Aspect_DisplayConnectionDefinitionError`.

Рабочий набор переменных:

```bash
export DISPLAY=:0
export XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.GEI8X3
export QT_QPA_PLATFORM=xcb
```

Для automation также необходимы:

```bash
export PARAMETRICCAD_ENABLE_AUTOMATION=1
export PARAMETRICCAD_AUTOMATION_SOCKET=/tmp/parametriccad-runtime-validation
```

## 4. Воспроизводимые команды

Проверка display:

```bash
DISPLAY=:0 \
XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.GEI8X3 \
xdpyinfo | head
```

Минимальный реальный MCP smoke test:

```bash
DISPLAY=:0 \
XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.GEI8X3 \
QT_QPA_PLATFORM=xcb \
build/tests/mcp_modeling_integration_tests modelingWorkflowThroughMcp
```

Результат: `initTestCase`, `modelingWorkflowThroughMcp` и `cleanupTestCase`
прошли. Тест запускает реальный GUI, реальный `parametriccad-mcp` и временный
QLocalSocket endpoint.

Полный integration test:

```bash
DISPLAY=:0 \
XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.GEI8X3 \
QT_QPA_PLATFORM=xcb \
ctest --test-dir build -R '^mcp_modeling_integration_tests$' --output-on-failure
```

## 5. Результаты

Smoke test: **PASS**. Реальный MCP host выполнил создание Sketch, добавление
прямоугольника, Extrude, inspection и завершение GUI-сессии.

Full integration: **20 passed, 1 failed**. Подключение, component identity,
spatial transforms, component operations, Save/Load, topology, Undo/Redo и
application reconnect были выполнены через реальный MCP runtime.

Обнаруженный отдельный FAIL:

```text
McpModelingIntegrationTests::componentOperationsThroughMcp()
Actual displayedFeaturePresentationCount: 3
Expected: 0
Location: tests/mcp_modeling_integration_tests.cpp:516
```

Это дефект reconciliation AIS/presentation после загрузки проекта, а не IPC.
Production-код в рамках этой задачи не изменялся.

## 6. Ограничения и следующие действия

1. QLocalSocket/QLocalServer работают; менять IPC на TCP или добавлять второй
   сервер не требуется.
2. Для CI нужна X11/XWayland-сессия либо совместимый с OCCT Xvfb; одного
   `QT_QPA_PLATFORM=offscreen` недостаточно.
3. Нужно отдельно исправить или согласовать ожидание
   `displayedFeaturePresentationCount == 0` после component Save/Load.
4. До устранения этого FAIL полный MCP integration suite нельзя объявлять
   полностью зелёным.

ComponentResolver, Feature Graph, сериализация и геометрические операции в
рамках runtime validation не изменялись.
