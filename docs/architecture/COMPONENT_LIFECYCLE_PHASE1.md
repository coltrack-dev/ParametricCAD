# ParametricCAD — Component Lifecycle Phase 1: Diagnostics & Regression

**Дата:** 2026-10-31
**Scope:** read-only component integrity diagnostics and regression coverage.

## 1. Что реализовано

Добавлен `cad::parametric::inspectComponentIntegrity(const Body&, double)`:

- `src/model/ComponentIntegrity.h`
- `src/model/ComponentIntegrity.cpp`

Проверка не изменяет `Body`, `ModelComponent`, feature graph или resolver state. Она проверяет persisted metadata, replacement-граф, текущие solids и результат `ComponentResolver`.

Диагностические коды:

| Code | Severity | Проверка |
|---|---|---|
| `DUPLICATE_COMPONENT_ID` | Error | дубликаты persisted IDs |
| `MISSING_SOURCE_FEATURE` | Error | отсутствующий `sourceFeatureId` |
| `DANGLING_REPLACEMENT_REFERENCE` | Error | replacement ID отсутствует в metadata |
| `REPLACEMENT_CYCLE` | Error | цикл replacement-ссылок |
| `PARTIAL_COMPONENT_METADATA` | Warning | metadata покрывает только часть текущих solids |
| `AMBIGUOUS_GENERATED_COMPONENT_ID` | Error | несколько generated solids получили один ID |
| `UNRESOLVED_COMPONENT_GEOMETRY` | Error | persisted component не разрешается в текущей геометрии |
| `REPLACED_COMPONENT_ACTIVE` | Error | replacement metadata есть, но component не technical-hidden |
| `REPLACED_COMPONENT_RESOLVABLE` | Warning | текущий resolver всё ещё возвращает исторический replacement component |

Каждая запись содержит `code`, `featureId`, `componentId` (если применимо), описание и `severity`.

## 2. Регрессионный suite

Добавлен `tests/component_integrity_tests.cpp`, подключённый как CTest `component_integrity_tests`. Он использует только временные документы/файлы и не изменяет `examples/`.

Покрытие:

| Сценарий | Проверка | Результат |
|---|---|---|
| C-1 | удаление feature, stale metadata, Undo/Redo | PASS; stale metadata воспроизведена и диагностируется |
| C-2 | два одинаковых solid в compound | PASS; metric ID collision и unresolved ambiguity воспроизведены |
| C-3 | Transform → Trim → Split | PASS; replacement chain построена, historical IDs остаются resolver-resolvable |
| C-4 | metadata только одного из двух solids | PASS; `PARTIAL_COMPONENT_METADATA` обнаружен |
| C-5 | MCP `get_components` → `get_component` → visibility → global listing | добавлен integration test, запуск зависит от GUI display |
| A | 20 последовательных Transform | PASS; 21 feature/metadata row, historical replacement diagnostics присутствуют |
| B | 4 последовательных Trim | PASS; 5 metadata rows, replacement history диагностируется |
| C | Split → Undo → Redo | PASS; две branch metadata rows восстанавливаются |
| E | Save → Load | PASS; explicit component ID сохраняется |
| F | Sketch edit → recompute → resolution | PASS; downstream component остаётся разрешаемым |

Дополнительный fixture `diagnosticIssueCoverage()` проверяет dangling replacement, replacement cycle, incorrectly active replacement и unresolved geometry.

## 3. Фактические результаты

Выполнено:

```text
cmake --build build -j
ctest --test-dir build -R '^component_integrity_tests$' --output-on-failure
```

Обе команды завершились успешно после исправления тестового sketch profile. При сборке присутствуют существующие предупреждения OCCT о deprecated `std::iterator`; они не относятся к Phase 1.

MCP integration test `mcp_modeling_integration_tests` был собран, но запуск полного теста в текущем окружении завис на GUI automation/display и был остановлен. Это environment limitation, а не PASS/FAIL продукта. Его результат должен быть проверен в окружении с рабочим Qt display.

## 4. Подтверждённые нарушения инвариантов

### C-1 — stale metadata после удаления

`Body::removeFeature()` удаляет feature, но не component rows. После удаления диагностика выдаёт `MISSING_SOURCE_FEATURE`. Undo/Redo возвращает feature state, но не имеет отдельной component cleanup policy.

### C-2 — generated ID collision

Для одинаковых solids `ComponentResolver::enumerate()` создаёт одинаковые metric-based IDs. `resolve()` не может однозначно вернуть geometry; диагностика выдаёт `AMBIGUOUS_GENERATED_COMPONENT_ID`.

### C-3 — replacement resolver behavior

Replacement chain создаётся корректно на уровне metadata: parent получает replacement IDs и technical-hidden state. Однако текущий `ComponentResolver::resolve()` всё ещё возвращает исторический component ID. Диагностика отмечает это как `REPLACED_COMPONENT_RESOLVABLE`; production semantics в Phase 1 не менялись.

### C-4 — partial metadata

Когда persisted metadata содержит только один из двух текущих solids одной feature, диагностика выдаёт `PARTIAL_COMPONENT_METADATA`. Сама materialization policy не изменялась.

### C-5 — GUI/MCP

Добавлен MCP integration test, проверяющий, что component ID из `get_components` совпадает с `get_component` и сохраняется после visibility operation. Полный runtime result пока не получен из-за отсутствия рабочего GUI display в окружении.

## 5. Что осталось статически подтверждённым

- Удаление metadata при удалении feature не реализовывалось и не исправлялось.
- Правила генерации IDs и replacement semantics не изменялись.
- Нет отдельного production API для “active component”; диагностика использует существующие metadata, resolver и technical-hidden state.
- В Phase 1 не добавлялась интеграция диагностики в MCP API или UI.

Таким образом, диагностика является библиотечной read-only проверкой, а не новым источником истины.

## 6. Рекомендуемый порядок исправлений

1. Исправить C-1 на уровне undoable feature/component transaction: удаление feature должно атомарно сохранять и восстанавливать затронутые metadata.
2. Определить collision policy для generated IDs, сохранив backward compatibility; это отдельное изменение identity semantics и не должно входить в Phase 1.
3. Расширить resolver результатом `active/replaced/ambiguous`, не меняя сразу serialized format.
4. Унифицировать materialization controller/MCP/GUI.
5. Добавить recompute reconciliation для multi-solid metadata.

`ComponentRegistry` и Phase 2 не начинались.
