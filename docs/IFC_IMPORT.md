# IFC import (Phases B, C1 and C3)

Phase B provides a non-UI import core:

```text
IFC -> IfcOpenShellAdapter -> IfcImportProduct -> ImportedFeature -> Body -> .pcad
```

## End-user import workflow

In an IFC-enabled build, `File -> Import IFC...` starts an application-level
asynchronous task. The worker performs file parsing, IfcGeom iteration, shape
conversion, filtering, and metadata extraction. It reports application-neutral
progress and observes a cooperative cancellation token; it never accesses
widgets, Body, CadViewer, AIS, or SelectionAdapter.

```text
MainWindow
  -> IfcImporter::prepare (worker thread)
  -> prepared IfcImportResult
  -> IfcImporter::makeFeatures (GUI thread)
  -> ModelingController bulk command
  -> one model/presentation refresh
  -> CadViewer
```

The active project remains unchanged while preparation is running. Cancellation
or a fatal error discards the prepared result. Product-level warnings do not
discard a partially successful import. A completed import is one
`ImportFeaturesCommand`; undo removes the imported feature batch and redo reuses
the prepared feature objects without reparsing IFC. The model is marked dirty
through the normal `QUndoStack` path. The final viewer update performs one
`FitAll`; no viewer or geometry operation runs in the worker.

The DTO boundary transfers native OCCT `TopoDS_Shape` handles produced by the
validated IfcOpenShell build. No OCCT operations are performed on those shapes
in the worker after conversion, and model/AIS ownership remains on the GUI
thread. Application shutdown requests cancellation and waits for the worker.

When IFC support is disabled, the core still builds without IfcOpenShell and
the IFC menu action is omitted rather than failing at runtime.

`ImportedFeature` is a read-only `ParametricFeature` whose stored shape is
native OCCT B-Rep. IFC spatial hierarchy is retained as metadata (`building`
and `storey`); it is not converted into Body dependencies or synthetic
features. An IFC `GlobalId` is provenance identity and is distinct from the
ParametricCAD feature ID.

## Dependency strategy

The application uses OCCT 7.6.3. Phase B pins IfcOpenShell `v0.7.1`, commit
`ed8cbff3d253691ac81450eaf16cad46bf6149e5`, built separately against the same
OCCT headers and libraries. The complete IfcOpenShell source is not vendored.
The verified build used GCC 13.3.0, `/usr/include/opencascade`, and
`/usr/lib/x86_64-linux-gnu`; it produced `libIfcGeom.so`, `libIfcParse.so`,
`libIfcGeom_ifc2x3.a`, and `libIfcGeom_ifc4.a` against that same OCCT runtime.
IFC support is enabled by default for the canonical Linux development build.
Use the repository scripts so the configured and launched binary always come
from `build/`:

```bash
./configure.sh
./run.sh
```

For a manual configure, use:

```text
cmake -S . -B build -G Ninja \
  -DPARAMETRIC_CAD_ENABLE_IFC=ON \
  -DPARAMETRIC_CAD_IFCOPENSHELL_ROOT=/path/to/ifcopenshell-v0.7.1-install
```

`build-ifc/` is obsolete as a normal development directory. Keep special
builds under explicit names such as `build-asan` or `build-release`.

The external build must expose `IfcGeom`, `IfcParse`, `IfcGeom_ifc2x3`, and
`IfcGeom_ifc4`, and must use the same compiler ABI and OCCT 7.6.3 build. The
A build with IFC explicitly disabled has no IfcOpenShell or Python runtime
dependency and reports a structured diagnostic if IFC support is unavailable.

When IFC is explicitly enabled, CMake requires these headers and libraries and
fails during configuration with an actionable error if they are missing. The
verified developer commands are:

```text
cmake -S . -B build -G Ninja \
  -DPARAMETRIC_CAD_ENABLE_IFC=ON \
  -DPARAMETRIC_CAD_IFCOPENSHELL_ROOT=/tmp/ifcopenshell-v071-install
cmake --build build -j
LD_LIBRARY_PATH=/tmp/ifcopenshell-v071-install/lib \
  ./build/tests/ifc_import_tests
LD_LIBRARY_PATH=/tmp/ifcopenshell-v071-install/lib \
  ./build/tests/ifc_building_acceptance
```

## Geometry, placement, and units

The adapter uses `IfcGeom::Iterator` with native B-Rep output, world
coordinates, opening subtraction, and no independent per-product
`create_shape()` calls. Resulting geometry is normalized exactly once to
ParametricCAD's canonical millimetre unit. `ImportedFeature::placement()` is
identity; IFC placement chains and mapped-item transforms are resolved by
IfcOpenShell before the feature is created.

Physical products with body geometry are imported generally. Opening elements,
spaces, grids, distribution ports, annotations, and non-geometric spatial
objects are skipped as helper or presentation entities. Openings are expected
to be already subtracted from physical products by IfcOpenShell.

## Persistence

New `.pcad` files are ZIP-compatible containers. `manifest.json` contains
ImportedFeature metadata and a `shapePayload` reference; each imported shape is
stored as raw native B-Rep in a separate compressed
`geometry/<sha256(feature-id)>.brep` entry. `ProjectArchive` owns archive
handling and `ShapePayload` owns B-Rep encoding, so ImportedFeature and the IFC
adapter remain independent of storage details.

Legacy version-1 plain JSON files, including embedded Base64
`geometryPayload`, remain readable. The loader detects archives by their ZIP
signature rather than by filename extension. The original IFC is not needed to
reopen a saved `.pcad`.

## BIM Inspector

Selecting an `ImportedFeature` opens the read-only BIM Inspector. It reads
semantic metadata captured during the one-time import and stored with the
feature: identity and spatial IDs, predefined/type information, materials and
layers, property sets, simple element quantities, and source provenance. The
inspector never opens or reparses the IFC source file and remains usable after
the source has been moved or deleted.

The semantic boundary is:

```text
IfcOpenShellAdapter -> IfcMetadata DTO -> ImportedFeature -> .pcad -> BIM Inspector
```

No IfcOpenShell types or parsing logic are used by the Qt inspector.

Measured Phase B.2 sizes:

| Fixture | IFC | Old JSON/BRep | New container |
|---|---:|---:|---:|
| Duplex | 2,380,763 | 11,424,038 | 1,734,640 |
| BuildingBIMModel | 27,475,496 | 303,980,504 | 61,702,680 |

The new Building container is approximately 2.25x the IFC source and 5x
smaller than the transitional JSON/BRep file. It remains an implementation
storage layer; the logical Body and feature schema are unchanged.

## Diagnostics and tests

`IfcImportResult` reports schema, considered/geometry/imported/skipped/failed
counts, timings, per-entity statistics, and structured diagnostics. The regular
test target always covers B-Rep payload and ImportedFeature round trips.
Duplex is part of the IFC-enabled regular test target. Building acceptance is
an explicit slow executable and is not registered in the normal fast `ctest`
suite. The adapter records representation item names and placement depth for
diagnostics, including mapped representations and polygonal face sets. IFC
openings are excluded as Body features; their cuts are expected in the
physical product B-Rep returned by IfcOpenShell.
