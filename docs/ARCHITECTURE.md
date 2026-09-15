# Architecture

## Главный принцип

ParametricCAD разделяет четыре ответственности:

1. **Geometry** — собственные базовые математические типы и алгоритмы.
2. **Model** — документ и параметрические features.
3. **Operations** — построение OCCT shapes.
4. **Viewer/UI** — отображение и взаимодействие с пользователем.

UI не должен напрямую становиться моделью документа.

## Feature

Базовый `Feature` содержит имя и результирующий `TopoDS_Shape`.

Сейчас:

```text
BoxFeature.recompute()
    -> BRepPrimAPI_MakeBox
    -> TopoDS_Shape
```

В будущем:

```text
SketchFeature
      │
      ▼
ExtrudeFeature
      │
      ▼
FilletFeature
```

`ExtrudeFeature` будет зависеть от `SketchFeature`, а `FilletFeature` —
от `ExtrudeFeature`.

## Следующая архитектурная итерация

Добавить:

```cpp
using FeatureId = std::uint64_t;

enum class FeatureState {
    Clean,
    Dirty,
    Failed
};
```

и зависимости:

```cpp
std::vector<FeatureId> dependencies;
```

`Document::recompute()` должен:

1. построить dependency graph;
2. выполнить topological sort;
3. пересчитать только dirty features;
4. остановить зависимые features при ошибке upstream feature.

Это превращает учебный viewer в основу параметрической CAD-системы.
