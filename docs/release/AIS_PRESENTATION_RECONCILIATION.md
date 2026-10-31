# AIS Presentation Reconciliation After Save/Load

**Дата:** 2026-10-31  
**Scope:** исправление presentation lifecycle после MCP Save/Load

## Executive summary

Исправлено реальное дублирование исторической Feature-level геометрии после
загрузки проекта с replacement metadata.

До исправления после `Transform → Split → Save → Load` наблюдалось:

```text
featurePresentationCount:          4
displayedFeaturePresentationCount: 3
componentPresentationCount:        2
displayedComponentPresentationCount: 2
```

Диагностика показала, что Feature AIS оставались для исторических Features,
которые уже имели persisted metadata и только `REPLACED` components. Их
геометрия отображалась одновременно с двумя CURRENT component AIS.

## Root cause

`ModelPresenter::refreshModel()` уже правильно выбирал только `CURRENT`
components через `ComponentResolver::resolveAll()` и вызывал
`CadViewer::retainComponents()`.

Однако при feature, имеющем persisted metadata, но не имеющем CURRENT
components, код продолжал выполнение до fallback:

```text
viewer_.updateFeature(feature->shape(), feature->id())
```

Так historical/replaced Feature снова попадал в Feature presentation cache.
`retainFeatures()` не удалял его, потому что текущий refresh добавлял этот ID в
`presentedIds`.

После Save/Load это было видно для исторических Features типов `Extrude`,
`ComponentTransform` и `ComponentSplit`.

## Исправление

В `ModelPresenter::refreshModel()` добавлено явное правило:

```text
if feature has persisted component metadata
and no CURRENT component is resolvable:
    do not create Feature AIS
```

Это сохраняет взаимоисключение:

```text
Feature AIS ⊻ Component AIS
```

для каждого Feature. `CadViewer`, IPC, MCP, ComponentResolver, Feature Graph,
serialization и BRep algorithms не переписывались.

## Диагностика

В `componentOperationsThroughMcp` добавлен диагностический вывод при нарушении
ожидания. Для каждого Feature выводятся:

- feature ID и type;
- наличие component metadata;
- число CURRENT и REPLACED components;
- visibility;
- component IDs и lifecycle states.

Зафиксированный проблемный снимок содержал:

| Feature type | Metadata | CURRENT | REPLACED |
|---|---:|---:|---:|
| Sketch | no | 0 | 0 |
| Extrude | yes | 0 | 1 |
| ComponentTransform | yes | 0 | 1 |
| ComponentSplit (negative) | yes | 0 | 1 |
| ComponentSplit (positive) | yes | 1 | 0 |
| ComponentTransform (moved) | yes | 1 | 0 |

Следовательно, три отображаемых Feature presentations соответствовали
историческим/replaced results, а не независимой актуальной геометрии.

## Lifecycle after fix

Проверяемая последовательность:

```text
Transform → Split → Undo/Redo → Save → Load → recompute/refresh
```

После Load и после повторного recompute проверяется:

- `displayedFeaturePresentationCount == 0`;
- число displayed component presentations сохраняется;
- CURRENT component IDs остаются разрешимыми;
- aggregate geometry остаётся valid;
- component visibility Hide → Show не меняет lifecycle status;
- повторный refresh не создаёт дополнительные AIS objects.

## Regression results

### MCP runtime

```text
ctest --test-dir build -R '^mcp_modeling_integration_tests$' --output-on-failure
```

Результат после исправления:

```text
1/1 Test #10: mcp_modeling_integration_tests ... Passed
20 passed, 0 failed
```

Тест выполнен в реальной X11/XWayland GUI-сессии с `QT_QPA_PLATFORM=xcb`,
реальным `ParametricCAD`, реальным stdio MCP executable и QLocalSocket IPC.

### Связанные тесты

```text
model_component_tests       PASS
component_integrity_tests   PASS
trim_body_tests             PASS
project_file_tests          PASS
undo_tests                  PASS
undo_panel_tests            PASS
main_window_tests           PASS
controller_tests            PASS
```

Сборка `cmake --build build -j` прошла успешно. Остались только обычные
deprecated warnings от системных OpenCASCADE/Qt headers.

## Ограничения

- Проверка интерактивного mouse selection не автоматизировалась через MCP.
- Новый AIS cache не вводился.
- Формат `.pcad` и файлы в `examples/` не изменялись.
- Полный `ctest` с IFC acceptance не является частью этого исправления;
  затронутый MCP integration test полностью зелёный.
