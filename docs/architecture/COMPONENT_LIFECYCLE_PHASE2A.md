# ParametricCAD — Component Lifecycle Phase 2A: Metadata Consistency

**Дата:** 2026-10-31
**Scope:** исправление C-1 и C-4 без ComponentRegistry, без изменения `.pcad` format, generated-ID rules или replacement semantics.

## 1. Первопричины

### C-1 — deletion

`Body::removeFeature()` удалял только feature из `features_` и очищал `resultShape_`; `components_` не изменялся. `RemoveFeatureCommand` каскадно удалял зависимые features, но не имел metadata snapshot. Поэтому после Delete оставались rows с отсутствующим `sourceFeatureId`, а Undo/Redo не могли восстановить компонентное состояние атомарно.

### C-4 — partial metadata

`ModelingController::componentMetadata()` раньше группировал rows по `sourceFeatureId`: наличие хотя бы одной persisted row полностью подавляло generated enumeration для той же feature. Для compound/multi-solid feature это теряло неперсистированный solid.

## 2. Изменённые классы и методы

| Файл | Изменение |
|---|---|
| `src/model/Body.cpp` | `Body::removeFeature()` удаляет rows удаляемой feature, очищает `parentComponentId` и replacement references, восстанавливает surviving parent, если его replacement chain исчезла |
| `src/commands/FeatureCommands.h/.cpp` | `RemoveFeatureCommand` сохраняет before/after component snapshots и применяет их в Undo/Redo |
| `src/model/ModelComponent.h/.cpp` | добавлен узкий `ComponentResolver::mergeFeatureMetadata()` для per-solid merge; `resolveFeature()` использует этот merge |
| `src/application/ModelingController.cpp` | `componentMetadata()` собирает metadata по каждой feature через resolver helper и возвращает ошибку ambiguity/conflict |
| `tests/component_integrity_tests.cpp` | C-1/C-4 переведены с expected defect на passing assertions; добавлены Delete-after-Transform/Trim/Split, multi-component delete и Save/Load |
| `docs/architecture/COMPONENT_LIFECYCLE_PHASE2A.md` | этот отчёт |

Формат persistence и публичный MCP API не менялись.

## 3. Новые правила metadata merge

`ComponentResolver::mergeFeatureMetadata()` действует только для одной существующей feature:

1. Если persisted rows отсутствуют, возвращаются generated rows текущих solids.
2. Если persisted rows есть, каждый current solid сопоставляется с persisted signature по существующим bounds/volume tolerance.
3. Однозначное совпадение сохраняет persisted component ID.
4. Несопоставленный current solid получает generated metadata.
5. Несколько совпадений или duplicate generated IDs возвращают ошибку, а не молчаливый выбор.
6. Исторические rows с `technicalHidden` или replacement IDs не сопоставляются с current active solids и не активируются.
7. Persisted active row, которая больше не соответствует геометрии, возвращает conflict error.
8. Rows отсутствующих features не возвращаются из controller-level metadata collection.

Таким образом, частичная metadata для одной feature дополняется на уровне solids, а существующие IDs сохраняются. C-2 (одинаковые generated metric IDs) не исправляется: он остаётся диагностируемым conflict.

## 4. Правила удаления metadata

При `Body::removeFeature(featureId)`:

- удаляются rows с `sourceFeatureId == featureId`;
- у surviving rows очищается `parentComponentId`, если parent удалён;
- из `replacementComponentIds` удаляются IDs удалённых rows;
- если replacement list surviving row стала пустой, row возвращается в active technical state (`visible=true`, `technicalHidden=false`);
- metadata сохранившихся features не удаляется.

`RemoveFeatureCommand` уже знает полный dependency closure в `removed_`. Его первый `redo()` применяет Body cleanup и сохраняет фактический after snapshot. `undo()` сначала восстанавливает features, затем точный before snapshot. Последующий `redo()` повторно удаляет closure и возвращает тот же after snapshot.

Это сохраняет исторические rows, принадлежащие существующим features, но не оставляет dangling references на rows удалённых features.

## 5. Diagnostics policy

`inspectComponentIntegrity()` остаётся read-only и не вызывается в горячем GUI refresh path. Его использование добавлено в regression tests и может применяться на границах операций/загрузки.

`PARTIAL_COMPONENT_METADATA` теперь не выдаётся для feature, у которой часть rows является законной replacement history. Для обычной persisted multi-solid feature без history partial state по-прежнему является warning до merge.

`REPLACED_COMPONENT_RESOLVABLE` остаётся ожидаемым предупреждением C-3: текущий resolver всё ещё способен разрешить historical replacement ID. Phase 2A не меняет replacement semantics.

## 6. Undo/Redo результаты

Проверены в `component_integrity_tests`:

- Delete → Undo → Redo для простой feature;
- Delete после Transform;
- Delete после Trim;
- Delete после Split;
- Delete feature с двумя component rows;
- восстановление metadata counts и IDs после Undo;
- отсутствие metadata после Redo;
- отсутствие `MISSING_SOURCE_FEATURE` после Delete.

Для component operation chains metadata snapshot восстанавливает parent/replacement IDs вместе с feature closure.

## 7. Save/Load результаты

Проверен временный `.pcad` после Delete:

- сохранение успешно;
- повторная загрузка успешна;
- `loadedBody.components()` пуст после удаления последней feature;
- stale metadata не появляется после load.

Существующие explicit component IDs и replacement rows сериализуются прежним кодом `ProjectFile`; формат не изменён. Общие existing persistence tests также продолжают проходить.

## 8. Regression results

Успешно выполнены:

```text
cmake --build build --target component_integrity_tests model_component_tests trim_body_tests project_file_tests -j
ctest --test-dir build -R '^(component_integrity_tests|model_component_tests|trim_body_tests|project_file_tests)$' --output-on-failure
```

Последний полный запуск выбранной группы: **4/4 PASS**.

Покрытие:

| Test | Result |
|---|---|
| C-1 deletion metadata | PASS |
| C-4 partial metadata merge | PASS |
| C-2 generated collision | PASS as diagnostic regression; behavior intentionally unchanged |
| C-3 replacement resolver | PASS as diagnostic regression; warning intentionally remains |
| Delete → Transform/Trim/Split → Undo/Redo | PASS |
| multi-component Delete | PASS |
| Save → Load after Delete | PASS |
| `model_component_tests` | PASS |
| `trim_body_tests` | PASS |
| `project_file_tests` | PASS |

Полная сборка `cmake --build build -j` после итоговых изменений выполнена успешно. MCP GUI integration test запускался с `DISPLAY=:0`, но завершился ожидаемой ошибкой окружения: Qt не смог подключиться к display и инициализировать `xcb`; функциональный MCP-результат не подтверждён.

## 9. Оставшиеся риски

### C-2 — generated ID collision

Metric-based IDs для одинаковых solids по-прежнему могут совпадать. Теперь merge возвращает ambiguity error вместо выбора, но самостоятельная collision policy не вводилась.

### C-3 — replaced IDs

Historical replacement component по-прежнему может быть возвращён существующим `ComponentResolver::resolve()`. Visibility/technicalHidden semantics не менялись. Полная active/replaced resolution остаётся задачей следующей фазы.

Дополнительный риск: сложные внутренние сценарии, создающие несколько отдельных `RemoveFeatureCommand` внутри одной macro transaction, требуют отдельного анализа command ordering. Обычный Delete с dependency closure покрыт regression tests.

## 10. Статус

Phase 2A завершена. ComponentRegistry, Phase 2B и исправление C-2/C-3 не начинались.
