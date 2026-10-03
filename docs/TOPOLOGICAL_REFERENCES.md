# Topological references

`SelectionDescriptor` is a runtime picking DTO. Its `subshapeIndex` is only
valid for the current shape and must not be stored as the identity of a model
dependency.

`TopologicalReference` is the small persistence-oriented layer used by
long-lived operations such as Fillet and Chamfer. It stores the owning
`featureId`, Face/Edge/Vertex kind, a transient index fast-path, and a compact
geometric signature.

## Resolution

The resolver first checks the stored index and accepts it only when its shape
kind and signature match. If that check fails, it scans only subshapes of the
expected kind and compares them with centralized tolerance values:

- faces: surface kind, centroid, area, normal where available, and radius;
- edges: curve kind, endpoints, midpoint, length, direction where available,
  and radius;
- vertices: point position.

Exactly one matching candidate is required. No candidates produce `Missing`;
multiple candidates produce `Ambiguous`. The first candidate is never selected
arbitrarily. A successful recovery returns the new transient index to the
caller, while the stored signature remains the identity check.

## Persistence and limitations

References are serialized as JSON and do not contain `TopoDS_Shape` data.
Older Fillet/Chamfer records containing only `edgeIndices` are loaded as
legacy references without signatures. They retain backward compatibility but
cannot safely recover after an index changes.

This subsystem is not history-based persistent naming. Topology-changing
operations can still make a reference missing or ambiguous, and coincident
geometries may require a future naming/history layer. Push/Pull and SnapManager
continue to use their existing runtime selection paths.
