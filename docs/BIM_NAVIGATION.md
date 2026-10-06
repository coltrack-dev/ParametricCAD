# BIM navigation

Imported IFC products remain ordinary `ImportedFeature` objects in the flat
`Body` history. The BIM view is a presentation/indexing projection built by
`BimNavigationModel`:

```text
ImportedFeature metadata
        -> BimNavigationModel
        -> BIM navigation panel
        -> existing feature IDs
        -> Selection / VisibilityManager
```

The projection groups products by building, storey, and friendly IFC entity
category. Missing metadata is represented as `<No Building>` or
`<Unassigned Storey>`. It does not create Body dependencies or duplicate
parametric features.

Storey and category actions resolve their members to feature IDs and use the
generic visibility and selection paths. Search matches IFC name, GlobalId,
entity type, and storey without changing model visibility.
