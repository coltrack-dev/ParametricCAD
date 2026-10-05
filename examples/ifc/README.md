# IFC Phase A fixtures

These files are developer/test fixtures for the IFC import investigation.

- `BuildingBIMModel.ifc` comes from [viktor-platform/ifc-sample-models](https://github.com/viktor-platform/ifc-sample-models), file `sample-models/BuildingBIMModel.ifc`.
- `Duplex_A_20110505.ifc` is the smaller public Duplex IFC fixture mirrored from [MadsHolten/BOT-Duplex-house](https://github.com/MadsHolten/BOT-Duplex-house), `Model files/IFC/Duplex.ifc`. The historical OpenIFC repository URL referenced by the original model documentation currently serves a web application instead of the ZIP archive.

The BuildingBIMModel repository does not provide a separate license file for
the sample model. The fixture is retained for development and acceptance
testing with source attribution; consult the upstream repository before
redistributing it.

Fixture inventory (Phase B):

| File | Size | SHA-256 | Schema |
| --- | ---: | --- | --- |
| `BuildingBIMModel.ifc` | 27,475,496 bytes | `050081f9f7a6d374f8feebe111897e1039ee4b62505b8caab129da906232bf17` | IFC4 |
| `Duplex_A_20110505.ifc` | 2,380,763 bytes | `b347a2c8aa8fff6db896a4417a9c50c22ac0ccd7c5cfc22b99b8d29336c606ed` | IFC2X3 |

The Duplex file is a public mirror, not a verified byte-identical copy of the
historical OpenIFC `Duplex_A_20110505.ifc`.
