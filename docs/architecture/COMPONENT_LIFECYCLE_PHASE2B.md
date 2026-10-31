# ParametricCAD — Component Lifecycle Phase 2B: Stable Component Identity

**Дата:** 2026-10-31  
**Scope:** исправление C-2 без ComponentRegistry, без изменения формата `.pcad` и без изменения replacement semantics.

## 1. Выбранная стратегия

В этой фазе не вводится отдельная таблица идентичности. Уже существующее поле `ModelComponent::id` используется как persisted identity:

- explicit component IDs, созданные Transform/Trim/Split, сохраняются без изменений;
- legacy IDs, сохранённые в `.pcad`, также сохраняются без миграции;
- геометрические signatures используются только для поиска текущего solid и восстановления persisted ID;
- signature не считается уникальным identity key;
- если два текущих solids имеют одну signature, resolver возвращает `AMBIGUOUS_GENERATED_COMPONENT_ID` и не создаёт два активных компонента с одним ID.

Это безопаснее, чем назначать suffix по порядку `TopExp_Explorer`: для совпадающих solids такой порядок не является доказательством стабильной семантической идентичности.

## 2. Где создаются и сохраняются IDs

`ComponentResolver::idFor()` по-прежнему создаёт legacy generated ID из `sourceFeatureId` и geometric signature. Этот режим сохранён для обратной совместимости, но теперь он применяется только при однозначном результате (`src/model/ModelComponent.cpp`).

Компонентные операции создают UUID feature ID в `ModelingController::id()`, а component ID получают как `featureId + ":component"`. Эти IDs сохраняются в `Body::components()` через undoable `ReplaceComponentsCommand` и сериализуются `ProjectFile`.

`ProjectFile::encodeComponents()` и `decodeComponents()` уже сохраняют `id`, parent/replacement links и geometric metadata. Схема `.pcad` не менялась.

## 3. Правила legacy IDs

Старые файлы поддерживаются следующим образом:

1. Сохранённый `component.id` не переписывается при load/save.
2. Legacy generated ID разрешается по прежней signature, если она соответствует ровно одному current solid.
3. При одном persisted component и одном current solid `Body::refreshComponentGeometry()` обновляет bounds/volume, но сохраняет ID.
4. При неоднозначном результате resolver не выбирает solid автоматически и возвращает diagnostic error.
5. Автоматической массовой материализации или миграции всех проектов нет.

Поэтому старый проект может продолжать содержать legacy generated IDs, а новые explicit IDs появляются только в рамках существующих операций и их undoable metadata snapshots.

## 4. Сопоставление после recompute

`ComponentResolver::mergeFeatureMetadata()` выполняет one-to-one matching для одной Feature:

- source Feature остаётся обязательной частью области поиска;
- active persisted rows сопоставляются с текущими solids по bounds/volume tolerance;
- один persisted row нельзя использовать повторно;
- несколько возможных persisted rows дают `AMBIGUOUS_PERSISTED_COMPONENT_MATCH`;
- повторное использование одного persisted row даёт `AMBIGUOUS_PERSISTED_COMPONENT_REUSE`;
- persisted active row без current match даёт `UNRESOLVED_PERSISTED_COMPONENT_GEOMETRY`;
- generated IDs с одинаковой signature дают `AMBIGUOUS_GENERATED_COMPONENT_ID`;
- historical replacement rows не сопоставляются с active solids и не реактивируются.

`resolveFeature()` и `resolve()` используют этот путь. `resolve()` также передаёт ambiguity-код для legacy/generated lookup вместо общего `Component not found`.

`Body::refreshComponentGeometry()` сохраняет persisted ID для однозначного 1→1 результата и обновляет только geometric metadata. Для неоднозначных multi-solid изменений оно не назначает ID по порядку обхода; такой случай остаётся диагностируемым, а не скрыто исправляется.

## 5. Одинаковые solids

Для двух полностью одинаковых solids одного Feature bounds и volume совпадают. У них нет различимого признака в текущей модели metadata. Поэтому Phase 2B не создаёт искусственную identity из ordinal traversal:

- `enumerate(..., error)` возвращает пустой результат и `AMBIGUOUS_GENERATED_COMPONENT_ID`;
- `inspectComponentIntegrity()` сообщает ошибку с feature ID;
- `resolve()` прекращает lookup с тем же кодом;
- активный список не получает duplicate component IDs.

Различные solids с различными signatures продолжают получать разные legacy generated IDs. Если продукт в будущем должен адресовать геометрически полностью идентичные solids, потребуется отдельное persisted provenance/identity metadata или семантические labels; это не вводится в Phase 2B.

## 6. Transform, Trim и Split

Компонентные операции не изменяли правила replacement:

- Transform создаёт новый UUID-based feature/component ID;
- Trim создаёт новый UUID-based result component;
- Split создаёт два UUID-based result components;
- parent и replacement links остаются прежними;
- после recompute операции используют persisted IDs через resolver, а не заново генерируют их из geometry signature.

Regression test проверяет Transform → recompute → Trim → recompute → Split → recompute и отсутствие duplicate IDs. Поведение historical/replaced IDs относится к C-3 и не изменялось.

## 7. Save/Load

Проверены два последовательных цикла Save → Load:

- legacy-like persisted ID остаётся тем же;
- повторный Save → Load не меняет ID;
- текущая геометрия после load разрешается по сохранённым metadata;
- `.pcad` format и `ProjectFile` schema не изменялись.

## 8. Regression results

Добавлены проверки в `tests/component_integrity_tests.cpp`:

| Сценарий | Результат |
|---|---|
| Два одинаковых solids | PASS: duplicate ID не создаётся, ambiguity диагностируется |
| Различные solids | PASS: generated IDs различаются |
| Неизменный recompute | PASS: persisted ID сохраняется |
| Изменение размера однозначного результата | PASS: ID сохраняется, signature обновляется |
| Legacy-like ID Save/Load | PASS |
| Повторный Save/Load | PASS |
| Transform после recompute | PASS |
| Trim после recompute | PASS |
| Split после recompute | PASS |
| Проверка duplicate active IDs | PASS |

Проверенная команда:

```text
cmake --build build --target component_integrity_tests -j
ctest --test-dir build -R '^component_integrity_tests$' --output-on-failure
```

Полная сборка `cmake --build build -j` и целевой regression set выполнены успешно после изменений. Сборка выдаёт существующие предупреждения OCCT/Qt о deprecated API, ошибок компиляции нет.

## 9. Известные ограничения

1. Legacy generated ID всё ещё зависит от старой signature, если metadata не была материализована.
2. При изменении количества solids или неразличимом multi-solid результате resolver отказывает, а не пытается угадать соответствие.
3. Полностью одинаковые solids нельзя семантически различить без дополнительного persisted provenance.
4. Replacement-chain semantics C-3 не исправлялись.
5. ComponentRegistry и изменение `.pcad` schema не требуются для этой фазы, но могут стать необходимыми для будущей полноценной provenance-модели.

## 10. Статус

Phase 2B ограничена stable identity и ambiguity handling. Phase 2C не начиналась.
