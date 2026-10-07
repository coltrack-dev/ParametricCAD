# Topological references

`SelectionDescriptor` is a runtime picking DTO. Its `subshapeIndex` is only
valid for the current shape and must not be stored as the identity of a model
dependency.

`TopologicalReference` is the persistence-oriented layer used by long-lived
operations such as Fillet, Chamfer, and face-attached Sketch. It stores the
owning `featureId`, Face/Edge/Vertex kind, an optional semantic key, a transient
index fast-path, and a geometric signature. The index is never sufficient for
a non-legacy reference.

## Resolution

Resolution is staged and restricted to the referenced feature shape:

1. semantic key (currently deterministic Box faces/edges and Cylinder faces);
2. signature compatibility and weighted geometric score;
3. explicit ambiguity rejection.

The transient index is only a fast path when its signature also matches. If
that check fails, the resolver scans only subshapes of the expected kind and
compares them with centralized tolerance values:

- faces: surface kind, centroid, area, normal where available, and radius;
- edges: curve kind, endpoints, midpoint, length, direction where available,
  and radius;
- vertices: point position and local edge/face degree;
- faces: boundary edge count and local adjacency;
- edges: adjacent face count.

Exactly one matching candidate is required. No candidates produce `Missing`;
multiple candidates produce `Ambiguous`. The first candidate is never selected
arbitrarily. A successful recovery returns the new transient index to the
caller, while the stored signature remains the identity check.

## Persistence and limitations

References are serialized as JSON and do not contain `TopoDS_Shape` data.
Older Fillet/Chamfer records containing only `edgeIndices` are accepted and
upgraded at runtime to a signature reference when the legacy index is still
valid. The original persistent intent is not rewritten to a random candidate.

This subsystem does not yet consume `BRepBuilderAPI_MakeShape::Modified()` /
`Generated()` provenance from operation builders, and arbitrary operation
features still use signature fallback. Topology-changing operations can make a
reference missing or ambiguous; in that case the dependent feature reports a
failed state rather than silently selecting a neighbour. Push/Pull and
SnapManager continue to use their existing runtime selection paths.
