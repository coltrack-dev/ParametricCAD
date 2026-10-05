# IFC import (Phase B)

Phase B provides a non-UI import core:

```text
IFC -> IfcOpenShellAdapter -> IfcImportProduct -> ImportedFeature -> Body -> .pcad
```

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
IFC support is opt-in:

```text
cmake -S . -B build-ifc -G Ninja \
  -DPARAMETRIC_CAD_ENABLE_IFC=ON \
  -DPARAMETRIC_CAD_IFCOPENSHELL_ROOT=/path/to/ifcopenshell-v0.7.1-install
```

The external build must expose `IfcGeom`, `IfcParse`, `IfcGeom_ifc2x3`, and
`IfcGeom_ifc4`, and must use the same compiler ABI and OCCT 7.6.3 build. The
default build has no IfcOpenShell or Python runtime dependency and reports a
structured diagnostic if IFC support is unavailable.

When IFC is explicitly enabled, CMake requires these headers and libraries and
fails during configuration with an actionable error if they are missing. The
verified developer commands are:

```text
cmake -S . -B build-ifc -G Ninja \
  -DPARAMETRIC_CAD_ENABLE_IFC=ON \
  -DPARAMETRIC_CAD_IFCOPENSHELL_ROOT=/tmp/ifcopenshell-v071-install
cmake --build build-ifc -j
LD_LIBRARY_PATH=/tmp/ifcopenshell-v071-install/lib \
  ./build-ifc/tests/ifc_import_tests
LD_LIBRARY_PATH=/tmp/ifcopenshell-v071-install/lib \
  ./build-ifc/tests/ifc_building_acceptance
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

The existing version-1 JSON `.pcad` format remains backward compatible. For an
`ImportedFeature`, the generic persistence layer currently embeds an OCCT
B-Rep payload in the feature record. B-Rep serialization is isolated in
`src/model/ShapePayload.*`; `ImportedFeature` does not implement Base64 or
stream encoding. This transitional backend can later be replaced by archive
entries such as `geometry/<feature-id>.brep` without changing the feature or
importer API. The original IFC is not needed to reopen the saved `.pcad`.

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
