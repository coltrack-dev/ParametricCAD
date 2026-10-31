# ParametricCAD — Component Lifecycle Phase 2C: Canonical Component Resolution

**Дата:** 2026-10-31  
**Scope:** устранение C-3/C-5 через расширение существующего `ComponentResolver`; без ComponentRegistry и без изменения `.pcad`.

## 1. Canonical resolver contract

`ComponentResolution` теперь содержит:

- `ModelComponent component`;
- текущий `TopoDS_Shape shape`, если он однозначно найден;
- `ComponentLifecycleStatus lifecycleStatus`;
- `diagnosticCode`;
- `diagnosticMessage`.

Lifecycle и пользовательская visibility разделены. Статусы:

| Status | Значение |
|---|---|
| `CURRENT` | компонент является допустимым текущим результатом; он может быть user-hidden |
| `REPLACED` | у компонента есть replacement results; он доступен для истории, но не для новых modeling operations |
| `MISSING_SOURCE` | source Feature отсутствует |
| `AMBIGUOUS` | current geometry или metadata не разрешаются однозначно |
| `INVALID_REPLACEMENT_CHAIN` | replacement/parent links повреждены или образуют цикл |

Добавлены методы:

- `ComponentResolver::resolveAll()` — canonical список persisted и generated components;
- `ComponentResolver::resolveCanonical()` — canonical lookup одного ID;
- `componentLifecycleStatusName()` — стабильное API-представление статуса.

Старый `ComponentResolver::resolve()` сохранён. Он делегирует canonical resolver и продолжает возвращать доступную historical geometry для inspection/legacy callers. Новые моделирующие операции используют только canonical path и требуют `CURRENT`.

## 2. Replacement traversal

Canonical resolver выполняет read-only проверки:

- replacement ID существует;
- replacement graph не содержит циклов;
- каждый replacement child ссылается на своего parent;
- каждый `parentComponentId` имеет reciprocal replacement link;
- source Feature существует;
- geometry разрешается ровно для одного current solid.

Для цепочки `A → B → C → D+E` результатом является:

- A, B, C — `REPLACED`;
- D, E — `CURRENT`;
- D/E остаются доступны через `resolveCanonical()` и могут быть целями modeling operations;
- исторические rows не удаляются и не исправляются автоматически.

`inspectComponentIntegrity()` дополнительно сообщает `INVALID_PARENT_COMPONENT_REFERENCE` и `INVALID_REPLACEMENT_CHAIN`.

## 3. Operation safety

`ModelingController::transformComponent()`, `splitComponent()` и `trimBody()` используют общий helper разрешения current component. Они отклоняют:

- `REPLACED` IDs;
- `MISSING_SOURCE`;
- `AMBIGUOUS` results;
- `INVALID_REPLACEMENT_CHAIN`;
- unresolved current geometry.

Ошибка содержит canonical diagnostic code, например `COMPONENT_REPLACED`. Legacy `resolve()` при этом не ломает исторический просмотр и диагностику.

## 4. GUI и MCP

`ModelPresenter` строит component AIS только из `resolveAll()` с `CURRENT` status. Historical/replaced rows больше не передаются в presentation reconciliation.

MCP `cad.get_components` и `cad.get_component` используют тот же canonical список и возвращают:

- `lifecycleStatus`;
- `diagnosticCode`;
- `diagnosticMessage`;
- component metadata и replacement IDs.

MCP `cad.set_component_visibility` разрешён только для `CURRENT`. Visibility не меняет lifecycle status. Generated metadata больше не materialize-ится этим read/visibility path: для generated component используется policy `VisibilityManager`, а `Body` не изменяется.

MCP aggregate inspection также использует только canonical `CURRENT` components, отфильтрованные отдельно по visibility. Preview trim перешёл с raw `enumerate()` на current canonical results и не изменяет Document.

## 5. Viewer/AIS consistency

`ModelPresenter` передаёт в `CadViewer` только current, non-null shapes. `CadViewer` и его существующий reconciliation/AIS cache не изменялись. При каждом refresh:

- replaced components не добавляются повторно;
- stale AIS удаляются существующим `retainComponents()`;
- user Hide/Show применяется после lifecycle filtering;
- Undo/Redo и Save/Load повторно проходят через canonical resolution.

Новый AIS-кэш не вводился.

## 6. Persistence and compatibility

Сериализация `.pcad` не менялась. Persisted IDs, `parentComponentId`, `replacementComponentIds`, visibility и historical rows сохраняются прежним `ProjectFile` кодом. Загрузка не выполняет миграцию или автоматическое исправление повреждённых chains.

Повреждённые legacy documents получают structured diagnostics через canonical resolver и `inspectComponentIntegrity()`.

## 7. Regression coverage

В `tests/component_integrity_tests.cpp` добавлены проверки:

| Сценарий | Результат |
|---|---|
| Transform → Trim → Split | PASS |
| Canonical A/B/C/D+E lifecycle | PASS |
| Modeling operation on replaced ID | PASS: rejected with `COMPONENT_REPLACED` |
| Hide current component | PASS: lifecycle remains `CURRENT` |
| Replacement Save/Load | PASS |
| Dangling replacement/cycle | PASS via integrity diagnostics |
| Invalid parent link | PASS via integrity diagnostics |
| Ambiguous generated component | PASS via integrity diagnostics |
| Read-only resolution | PASS: `resolveAll()` and diagnostics do not alter metadata |
| Stable IDs after recompute | PASS |
| No duplicate current branch geometry | PASS |

Проверены команды:

```text
cmake --build build -j
ctest --test-dir build -R '^(component_integrity_tests|model_component_tests|trim_body_tests|project_file_tests|undo_tests|undo_panel_tests|controller_tests)$' --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^main_window_tests$' --output-on-failure
```

Сборка и все семь выбранных regression tests прошли; `main_window_tests` также прошёл в offscreen режиме. Отдельный MCP functional runtime test не выполнялся: для него требуется рабочий Qt display и доступный MCP host, а текущая среда headless.

## 8. Известные ограничения

1. Legacy `resolve()` по-прежнему может вернуть historical geometry для совместимости; modeling code обязан использовать `resolveCanonical()`.
2. `VisibilityManager` остаётся отдельным владельцем пользовательской visibility policy; canonical resolver не изменяет её.
3. Полностью одинаковые solids остаются `AMBIGUOUS` согласно Phase 2B.
4. Replacement metadata не ремонтируется автоматически.
5. Полная provenance-модель и ComponentRegistry не вводились.

## 9. Статус

Phase 2C завершена. ComponentRegistry и дальнейшие архитектурные изменения не начинались.
