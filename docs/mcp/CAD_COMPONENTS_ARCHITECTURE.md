# CAD Components Architecture Audit

## Current architecture

`Body` owns an ordered dependency list of `ParametricFeature` objects. Each
feature owns one `TopoDS_Shape`, a feature-level visibility flag, dirty/state
information and weak dependency links. `Body::recompute()` evaluates features
in dependency order and publishes the last non-null feature result as the body
result.

`TransformFeature` can consume several source features and filters nested
source selections with `effectiveSourceIds()`. `SplitSegmentFeature` creates a
clipped solid from one source, while `SplitAssemblyFeature` currently creates a
compound containing the rest of the whole source result plus replacement
segments. Therefore `SplitAssembly` is a whole-result feature, not a wall-only
feature.

`ModelPresenter` presents one AIS shape per feature. `VisibilityManager` also
stores and evaluates visibility by feature ID. `SelectionItem` identifies a
feature and, for subshapes, a transient current subshape index. That index is
explicitly not persistent across recompute. `TopologyReference` is the existing
stable topology mechanism for faces/edges, but it is attached to a feature
result, not yet to a compound component identity.

`ProjectFile` serializes the parametric feature history and feature visibility.
Undo/Redo is command based and `ModelTransaction` groups controller operations;
it does not make a solid ordinal into a persistent identifier.

## Implemented component layer

`ModelComponent` is a persistent metadata record containing:

- stable component ID;
- name and semantic role;
- source feature ID and optional parent component ID;
- geometric signature (bounds and volume);
- component visibility preference.

`ComponentResolver` enumerates solids from a feature and resolves a component
by its source feature plus geometric signature. It rejects missing and
ambiguous matches instead of silently selecting another solid. Projects without
component metadata use a deterministic legacy fallback generated from the
current feature result, so existing `.pcad` files remain readable.

Component metadata is stored in the optional root-level `components` array of
the existing version-1 project format. Legacy files omit this array and remain
compatible.

The read-only MCP discovery tools are exposed:

- `cad.get_components`;
- `cad.get_component`;
- `cad.get_component_topology`.

They return component identity, source feature, bounds, volume and OCCT shape
inspection/topology. Resolution is performed on the GUI/application side, not
by the stdio process.

## Migration risks

1. A geometric signature can still be ambiguous for two identical solids. The
   resolver returns an explicit ambiguity error; future metadata should use
   topology/signature continuity or a feature-provided semantic role.
2. Reordering or changing a compound can change its shape signature. Component
   continuity must be established before committing recompute, rather than by
   trusting a `TopExp_Explorer` ordinal.
3. The current presenter and OCCT selection adapter still present/select at
   feature granularity. Independent component AIS presentations require a
   component-aware presentation map and selection payload.
4. `VisibilityManager` currently has feature-level precedence rules only.
   Component visibility must be added without overwriting user feature
   preferences or technical visibility of source features.
5. Legacy `SplitAssembly` geometry is intentionally not rewritten. A future
   migration may expose its solids as components, but must preserve the old
   compound result and serialized feature history.

## Not yet enabled by this increment

The following mutation/presentation APIs remain future work and are deliberately
not advertised as complete: `cad.set_component_visibility`,
`cad.transform_component`, `cad.split_component`, component-level AIS
selection/isolation, and local replacement of `SplitAssembly` with independent
parametric output features. These require controller commands and undo state
for component metadata, plus component-aware ModelPresenter integration.

## Verification

The new `model_component_tests` covers a two-solid compound, deterministic
enumeration, successful resolution after metadata registration, and missing
component errors. The full existing suite was run with:

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Result: **100% tests passed, 29/29**.
