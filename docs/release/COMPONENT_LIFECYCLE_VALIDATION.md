# ParametricCAD — Component Lifecycle Release Validation

**Дата:** 2026-10-31  
**Область:** Phase 1–2C, без изменения production-кода

## Итоговая оценка

**Релизная готовность: NOT READY / INCONCLUSIVE.**

Локальная автоматическая проверка прошла: 29 из 29 тестов, не требующих
внешнего MCP host и IFC acceptance harness. Однако обязательная E2E-проверка
через работающий MCP host и интерактивная GUI-проверка не выполнены: MCP tools
доступны в окружении, но ParametricCAD не подключён.

Ошибка подключения:

```text
ParametricCAD is not connected: QLocalSocket::connectToServer: Invalid name
```

До подключения рабочего host нельзя объявлять Component Lifecycle полностью
готовым к стабильному релизу.

## 1. Выполненные проверки

| Проверка | Результат | Основание |
|---|---|---|
| Сборка `cmake --build build -j` | PASS | Сборка завершилась успешно |
| ComponentResolver / lifecycle diagnostics | PASS | `component_integrity_tests` |
| Replacement lifecycle | PASS | Transform → Trim → Split в regression tests |
| Replaced component rejection | PASS | `component_integrity_tests`, `trim_body_tests` |
| Undo/Redo | PASS | `undo_tests`, `undo_panel_tests` |
| Save/Load и project files | PASS | `project_file_tests`, `project_archive_tests` |
| House facade file loading | PASS | `project_file_tests` загружает `examples/house_facade_photo.pcad` |
| GUI test harness | PASS (offscreen) | `main_window_tests` |
| MCP protocol serialization | PASS | `mcp_protocol_tests` |
| MCP E2E modeling | SKIPPED | Нет подключённого ParametricCAD host |
| Интерактивный GUI display | SKIPPED | Нет проверенной графической сессии |
| 100/50/20 stress sequences | SKIPPED | Нужен рабочий MCP/GUI runtime |
| IFC acceptance | SKIPPED | `ifc_building_acceptance` не завершился в текущем окружении |

Основная команда локальной проверки:

```text
ctest --test-dir build -E '^(mcp_modeling_integration_tests|ifc_building_acceptance)$' --output-on-failure
```

Результат: **29/29 passed**, суммарно около **8.64 s**.

## 2. MCP End-to-End

Сценарий не запускался частично: даже `cad.get_application_status` и
`cad.new_project` вернули ошибку отсутствующего подключения. Поэтому не
подтверждены runtime-последовательности:

```text
Sketch → Extrude → Transform → Trim → Split → Undo/Redo → Save/Load
```

Не подтверждены через MCP в работающем приложении:

- фактическое состояние `REPLACED` после Transform;
- два актуальных результата после Split;
- ошибка Trim по устаревшему ID;
- совпадение component IDs до и после Save/Load;
- совпадение MCP-ответа с GUI/controller runtime-состоянием.

Это ограничение инфраструктуры, а не PASS.

## 3. House Facade Regression

`project_file_tests` успешно загружает `examples/house_facade_photo.pcad` и
проверяет сохранность feature count и trim-related metadata. Исходный файл не
изменялся.

Следующие проверки требуют интерактивного viewer или MCP runtime и поэтому
остались `SKIPPED`:

- визуальное отсутствие дублирующихся OSB-панелей;
- корректное отображение крыши и фронтонов;
- соответствие AIS selection → component ID;
- интерактивный Trim Body Preview и Pick planar face;
- очистка временных AIS-объектов после Apply/Cancel и смены проекта;
- визуальная проверка Hide/Show.

## 4. Stress Regression и метрики

Стрессовые цепочки 100 Transform, 50 Trim и 20 Split не запускались: они
требуют работающего Document/viewer runtime. Поэтому следующие метрики не
получены:

- число Feature и metadata records после цепочек;
- число `CURRENT` и `REPLACED` компонентов;
- число AIS Presentations;
- время `resolveAll()` и ModelPresenter refresh;
- использование памяти;
- Undo/Redo и Save/Load после полной stress-цепочки.

Доступные ориентиры автоматического набора: полный исключённый от host набор
занял около 8.64 s; `project_file_tests` — около 0.55 s,
`component_integrity_tests` — около 0.46 s, `controller_tests` — около 0.95 s.
Это не является измерением stress-performance или AIS-performance.

## 5. Legacy Compatibility

Автоматически подтверждено:

- загрузка legacy/project fixtures в `project_file_tests`;
- round-trip project/archive tests;
- сохранение replacement metadata на house facade fixture;
- отсутствие необходимости в автоматической миграции в проверенных тестовых
  путях.

Не выполнено на отдельной матрице нескольких пользовательских legacy `.pcad`:

- сравнение всех component IDs до/после загрузки;
- проверка повреждённых replacement chains без автоматического исправления;
- интерактивная проверка сохранённой visibility.

## 6. Найденные проблемы и ограничения

1. **MCP host недоступен** — блокирует обязательную E2E-проверку.
2. **`mcp_modeling_integration_tests` не завершился** в окружении без host;
   он исключён из итогового локального набора и не считается PASS.
3. **`ifc_building_acceptance` не завершился** в текущем окружении; он также
   исключён из итогового набора.
4. **Offscreen GUI не заменяет интерактивную проверку** selection, AIS,
   preview и смены проекта.
5. **Stress metrics отсутствуют**, поэтому нельзя подтвердить отсутствие
   необъяснимого накопления активной или отображаемой геометрии под нагрузкой.

Production-код и существующие файлы в `examples/` в рамках валидации не
изменялись.

## 7. Что необходимо перед релизом

В рабочей графической сессии с подключённым MCP host повторить:

1. полный MCP E2E-сценарий из раздела 2;
2. house facade визуальную проверку во временной копии;
3. stress sequences с измерением Document/AIS/memory metrics;
4. legacy matrix с несколькими pre-Phase-2A `.pcad`;
5. интерактивные GUI Apply/Cancel, selection, Hide/Show и Undo/Redo.

До выполнения этих пунктов итоговый статус остаётся **NOT READY**.
