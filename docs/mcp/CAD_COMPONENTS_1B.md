# CAD-Components-1B Presentation and Selection

## Presentation ownership

`ModelPresenter` remains the owner of the decision whether a feature is shown
as one feature AIS object or as component AIS objects. If a resolved feature
contains more than one solid, the presenter updates one `AIS_Shape` per
component and omits the compound presentation. `CadViewer` owns the lifetime
of these objects in `componentObjects_` and reuses an existing AIS object when
the component ID remains present. `retainComponents()` removes obsolete
objects after recompute or project load.

Single-solid features continue to use the existing feature presentation and
selection path.

## Component resolution

Persisted component IDs are resolved through the source feature and stored
bounding-box/volume signature. If there is no metadata, the legacy resolver
generates deterministic IDs from the current solid signature. Missing or
ambiguous matches are rejected. No solid ordinal is treated as persistent.

## Selection

`SelectionItem` and `SelectionDescriptor` now optionally carry a
`componentId`. `OcctSelectionAdapter` maps component AIS owners back to both
component and source feature IDs. Face/edge/vertex subshape indexes remain
transient; topology restoration continues to use `TopologyReference`.

Component object selections are restored after refresh when the component can
still be resolved. Existing feature-only selection callers remain supported by
the compatibility constructor and default selection fields.

## Visibility precedence

The effective order is:

1. feature-level Hidden hides the feature and all child component AIS objects;
2. component-level Hidden hides only that component;
3. component-level Ghosted keeps the component displayed but disables it as a
   selectable object through the normal visibility/selectability gate;
4. component-level Visible restores the component unless its feature is hidden;
5. isolation is implemented by hiding non-isolated component objects.

Technical feature visibility is kept in the existing feature visibility map;
component preferences are stored separately in `ModelComponent::visible` and
`CadViewer::componentVisibility_`.

## MCP

Added `cad.set_component_visibility` with modes `visible`, `hidden`, and
`ghosted`, plus optional `isolate`. `cad.get_selection` and `cad.get_component`
return component identity and effective visibility where applicable.

No splitting or component transformation API was added in this phase.
