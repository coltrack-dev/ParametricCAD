#include "viewer/SnapManager.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp_Face.hxx>
#include <BRep_Tool.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Geom_Circle.hxx>
#include <GeomAPI_ExtremaCurveCurve.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <algorithm>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <iostream>

namespace cad::viewer {

namespace
{
constexpr double TieTolerance = 1.0e-6;
constexpr double ReferenceDedupTolerance = 1.0e-6;
constexpr double IntersectionTolerance = 1.0e-7;
constexpr double IntersectionScreenRadius = 96.0;

std::size_t gLastNearbyEdgePairCount = 0;
std::size_t gLastIntersectionCandidateCount = 0;
std::size_t gLastCollectedReferenceCount = 0;
std::size_t gLastUniqueSourceReferenceCount = 0;
std::size_t gLastUniqueTargetReferenceCount = 0;
std::size_t gLastCandidateCount = 0;

template <typename Hasher>
auto diagnosticShapeHash(const TopoDS_Shape& shape, int)
    -> decltype(Hasher{}(shape), std::size_t{})
{
    return static_cast<std::size_t>(Hasher{}(shape));
}

template <typename Hasher>
std::size_t diagnosticShapeHash(const TopoDS_Shape& shape, long)
{
    // OCCT versions predating the functor call expose the same hasher as a
    // static function. Keep Linux/macOS builds using those versions
    // compatible while preferring the OCCT 7.9 API above.
    return static_cast<std::size_t>(Hasher::HashCode(shape, 1000003));
}

std::size_t diagnosticShapeHash(const TopoDS_Shape& shape)
{
    return diagnosticShapeHash<TopTools_ShapeMapHasher>(shape, 0);
}

QString referenceKey(const SnapReference& reference)
{
    const auto quantize = [](const double value) {
        return static_cast<qlonglong>(std::llround(value / ReferenceDedupTolerance));
    };
    QString key = QString::number(static_cast<int>(reference.type));
    key += ':' + QString::number(quantize(reference.point.X()));
    key += ':' + QString::number(quantize(reference.point.Y()));
    key += ':' + QString::number(quantize(reference.point.Z()));
    if (reference.frame) {
        key += ":n:" + QString::number(quantize(reference.frame->Direction().X()));
        key += ':' + QString::number(quantize(reference.frame->Direction().Y()));
        key += ':' + QString::number(quantize(reference.frame->Direction().Z()));
    }
    if (reference.axis) {
        key += ":a:" + QString::number(quantize(reference.axis->Direction().X()));
        key += ':' + QString::number(quantize(reference.axis->Direction().Y()));
        key += ':' + QString::number(quantize(reference.axis->Direction().Z()));
    }
    // Synthetic references used by callers/tests may not carry geometry. Keep
    // those distinct; only references backed by actual model geometry are
    // eligible for world-space deduplication.
    const bool pointReference = reference.type == SnapReferenceType::Vertex
        || reference.type == SnapReferenceType::Endpoint
        || reference.type == SnapReferenceType::Midpoint
        || reference.type == SnapReferenceType::Intersection
        || reference.type == SnapReferenceType::CircleCenter;
    if (!pointReference || (!reference.geometry && reference.shape.IsNull())) {
        key += ":synthetic:" + reference.ownerId + ':' + reference.subshapeId;
    }
    return key;
}

gp_Dir perpendicular(const gp_Dir& normal)
{
    gp_Dir reference(1.0, 0.0, 0.0);
    if (std::abs(normal.Dot(reference)) > 0.9) reference = gp_Dir(0.0, 1.0, 0.0);
    return gp_Dir(gp_Vec(normal) ^ gp_Vec(reference));
}

} // namespace

std::int64_t SnapScreenIndex::key(const int x, const int y) noexcept
{
    return (static_cast<std::int64_t>(x) << 32)
        ^ static_cast<std::uint32_t>(y);
}

void SnapScreenIndex::rebuild(const std::vector<SnapCandidate>& candidates)
{
    buckets.clear();
    buckets.reserve(candidates.size());
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& point = candidates[index].target.screenPoint;
        const int x = static_cast<int>(std::floor(point.x() / CellSize));
        const int y = static_cast<int>(std::floor(point.y() / CellSize));
        buckets[key(x, y)].push_back(index);
    }
}

std::vector<std::size_t> SnapScreenIndex::nearby(
    const QPointF& point,
    const double radius
) const
{
    const int minX = static_cast<int>(std::floor((point.x() - radius) / CellSize));
    const int maxX = static_cast<int>(std::floor((point.x() + radius) / CellSize));
    const int minY = static_cast<int>(std::floor((point.y() - radius) / CellSize));
    const int maxY = static_cast<int>(std::floor((point.y() + radius) / CellSize));
    std::vector<std::size_t> result;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const auto found = buckets.find(key(x, y));
            if (found != buckets.end()) {
                result.insert(result.end(), found->second.begin(), found->second.end());
            }
        }
    }
    return result;
}

std::size_t SnapManager::lastNearbyEdgePairCount() noexcept
{
    return gLastNearbyEdgePairCount;
}

std::size_t SnapManager::lastIntersectionCandidateCount() noexcept
{
    return gLastIntersectionCandidateCount;
}

std::size_t SnapManager::lastCollectedReferenceCount() noexcept
{
    return gLastCollectedReferenceCount;
}

std::size_t SnapManager::lastUniqueSourceReferenceCount() noexcept
{
    return gLastUniqueSourceReferenceCount;
}

std::size_t SnapManager::lastUniqueTargetReferenceCount() noexcept
{
    return gLastUniqueTargetReferenceCount;
}

std::size_t SnapManager::lastCandidateCount() noexcept
{
    return gLastCandidateCount;
}

namespace
{

std::optional<gp_Ax3> faceFrame(const TopoDS_Face& face, const gp_Pnt& point)
{
    BRepAdaptor_Surface surface(face, Standard_True);
    if (surface.GetType() != GeomAbs_Plane) return std::nullopt;
    const gp_Ax3 plane = surface.Plane().Position();
    gp_Dir normal = plane.Direction();
    if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
    return gp_Ax3(point, normal, plane.XDirection());
}

std::optional<gp_Pnt> closestPoint(const gp_Pnt& source, const SnapReference& target)
{
    if (target.type == SnapReferenceType::Vertex
        || target.type == SnapReferenceType::Endpoint
        || target.type == SnapReferenceType::Midpoint
        || target.type == SnapReferenceType::Intersection
        || target.type == SnapReferenceType::CircleCenter) return target.point;
    const TopoDS_Shape* targetShape = nullptr;
    if (!target.shape.IsNull()) targetShape = &target.shape;
    else if (target.geometry) targetShape = target.geometry.get();
    if (!targetShape || targetShape->IsNull()) return std::nullopt;
    const auto vertex = BRepBuilderAPI_MakeVertex(source).Vertex();
    BRepExtrema_DistShapeShape distance(vertex, *targetShape);
    if (!distance.IsDone() || distance.NbSolution() == 0) return std::nullopt;
    return distance.PointOnShape2(1);
}

std::optional<SnapKind> compatible(const SnapReference& source, const SnapReference& target)
{
    const auto pointReference = [](const SnapReferenceType type) {
        return type == SnapReferenceType::Vertex
            || type == SnapReferenceType::Endpoint
            || type == SnapReferenceType::Midpoint
            || type == SnapReferenceType::Intersection
            || type == SnapReferenceType::CircleCenter;
    };
    if (pointReference(source.type) && pointReference(target.type)) {
        if (target.type == SnapReferenceType::Endpoint) return SnapKind::Endpoint;
        if (target.type == SnapReferenceType::Intersection) return SnapKind::Intersection;
        if (target.type == SnapReferenceType::Midpoint) return SnapKind::Midpoint;
        if (source.type == SnapReferenceType::CircleCenter
            && target.type == SnapReferenceType::CircleCenter)
            return SnapKind::CenterToCenter;
        return SnapKind::VertexToVertex;
    }
    if (source.type == SnapReferenceType::Vertex) {
        if (target.type == SnapReferenceType::Vertex) return SnapKind::VertexToVertex;
        if (target.type == SnapReferenceType::Edge) return SnapKind::VertexToEdge;
        if (target.type == SnapReferenceType::Face) return SnapKind::VertexToFace;
    }
    if (source.type == SnapReferenceType::Face && target.type == SnapReferenceType::Face
        && source.frame && target.frame)
        return SnapKind::FaceToFace;
    if (source.type == SnapReferenceType::CircleCenter
        && target.type == SnapReferenceType::CircleCenter)
        return SnapKind::CenterToCenter;
    if (source.type == SnapReferenceType::Axis && target.type == SnapReferenceType::Axis)
        return SnapKind::AxisToAxis;
    return std::nullopt;
}

int specificity(const SnapKind kind)
{
    switch (kind) {
    case SnapKind::Endpoint: return 6;
    case SnapKind::Intersection: return 5;
    case SnapKind::Midpoint: return 4;
    // Point snaps are the most precise result. Axis/edge snaps win over a
    // face-plane result only when screen distances are effectively equal.
    case SnapKind::VertexToVertex:
    case SnapKind::CenterToCenter: return 3;
    case SnapKind::VertexToEdge:
    case SnapKind::AxisToAxis: return 2;
    case SnapKind::VertexToFace:
    case SnapKind::FaceToFace: return 1;
    }
    return 0;
}

std::optional<TopoDS_Edge> edgeShape(const SnapReference& reference)
{
    const TopoDS_Shape* shape = !reference.shape.IsNull()
        ? &reference.shape
        : reference.geometry ? reference.geometry.get() : nullptr;
    if (!shape || shape->IsNull() || shape->ShapeType() != TopAbs_EDGE) return std::nullopt;
    std::cerr << "[SnapManager] before TopoDS::Edge cast hash="
              << diagnosticShapeHash(*shape) << '\n' << std::flush;
    const auto edge = TopoDS::Edge(*shape);
    std::cerr << "[SnapManager] after TopoDS::Edge cast hash="
              << diagnosticShapeHash(edge) << '\n' << std::flush;
    return edge;
}

void logEdgeDetails(const char* label, const TopoDS_Edge& edge,
                    const Handle(Geom_Curve)& curve, const Standard_Real first,
                    const Standard_Real last)
{
    std::cerr << "[SnapManager] " << label
              << " IsNull=" << edge.IsNull()
              << " ShapeType=" << static_cast<int>(edge.ShapeType())
              << " Orientation=" << static_cast<int>(edge.Orientation())
              << " Degenerated=" << BRep_Tool::Degenerated(edge)
              << " hash=" << diagnosticShapeHash(edge)
              << " curveNull=" << curve.IsNull()
              << " range=[" << first << ',' << last << ']';
    if (!curve.IsNull()) {
        std::cerr << " curveType=" << curve->DynamicType()->Name();
    }
    const auto firstVertex = TopExp::FirstVertex(edge, Standard_True);
    const auto lastVertex = TopExp::LastVertex(edge, Standard_True);
    if (!firstVertex.IsNull() && !lastVertex.IsNull()) {
        const auto firstPoint = BRep_Tool::Pnt(firstVertex);
        const auto lastPoint = BRep_Tool::Pnt(lastVertex);
        std::cerr << " endpoints=(" << firstPoint.X() << ',' << firstPoint.Y()
                  << ',' << firstPoint.Z() << ")->(" << lastPoint.X() << ','
                  << lastPoint.Y() << ',' << lastPoint.Z() << ')';
    }
    std::cerr << '\n' << std::flush;
}

std::optional<cad::topology::TopologicalReference> makeTopologyReference(
    const QString& ownerId, const TopoDS_Shape& ownerShape, const TopoDS_Shape& subshape)
{
    try {
        return cad::topology::TopologicalSignatureBuilder::createReference(
            ownerId.toStdString(), ownerShape, subshape);
    } catch (const Standard_Failure&) {
        return std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<SnapReference> buildIntersectionReferences(
    const std::vector<SnapReference>& edges)
{
    std::cerr << "[SnapManager] buildIntersectionReferences entry edgeCount="
              << edges.size() << '\n' << std::flush;
    std::vector<SnapReference> result;
    std::set<QString> points;
    gLastNearbyEdgePairCount = 0;
    gLastIntersectionCandidateCount = 0;
    for (std::size_t first = 0; first < edges.size(); ++first) {
        std::cerr << "[SnapManager] intersection edge loop first=" << first
                  << '/' << edges.size() << '\n' << std::flush;
        const auto edgeA = edgeShape(edges[first]);
        if (!edgeA) continue;
        Standard_Real firstA = 0.0;
        Standard_Real lastA = 0.0;
        std::cerr << "[SnapManager] before BRep_Tool::Curve first=" << first
                  << '\n' << std::flush;
        const auto curveA = BRep_Tool::Curve(*edgeA, firstA, lastA);
        std::cerr << "[SnapManager] after BRep_Tool::Curve first=" << first
                  << '\n' << std::flush;
        logEdgeDetails("edgeA", *edgeA, curveA, firstA, lastA);
        if (curveA.IsNull() || BRep_Tool::Degenerated(*edgeA)
            || !std::isfinite(firstA) || !std::isfinite(lastA) || firstA >= lastA) {
            std::cerr << "[SnapManager] skipping invalid first edge index=" << first
                      << '\n' << std::flush;
            continue;
        }
        for (std::size_t second = first + 1; second < edges.size(); ++second) {
            std::cerr << "[SnapManager] intersection pair first=" << first
                      << " second=" << second << '/' << edges.size()
                      << '\n' << std::flush;
            const auto edgeB = edgeShape(edges[second]);
            if (!edgeB) continue;
            if (std::hypot(edges[first].screenPoint.x() - edges[second].screenPoint.x(),
                          edges[first].screenPoint.y() - edges[second].screenPoint.y())
                > IntersectionScreenRadius) continue;
            ++gLastNearbyEdgePairCount;
            Standard_Real firstB = 0.0;
            Standard_Real lastB = 0.0;
            std::cerr << "[SnapManager] before BRep_Tool::Curve second=" << second
                      << '\n' << std::flush;
            const auto curveB = BRep_Tool::Curve(*edgeB, firstB, lastB);
            std::cerr << "[SnapManager] after BRep_Tool::Curve second=" << second
                      << '\n' << std::flush;
            logEdgeDetails("edgeB", *edgeB, curveB, firstB, lastB);
            if (curveB.IsNull() || BRep_Tool::Degenerated(*edgeB)
                || !std::isfinite(firstB) || !std::isfinite(lastB) || firstB >= lastB) {
                std::cerr << "[SnapManager] skipping invalid second edge index=" << second
                      << '\n' << std::flush;
                continue;
            }
            const bool sameTShape = edgeA->TShape() == edgeB->TShape();
            std::cerr << "[SnapManager] edge pair same underlying TShape="
                      << sameTShape << " first=" << first << " second=" << second
                      << '\n' << std::flush;
            if (sameTShape) {
                std::cerr << "[SnapManager] skipping same underlying TShape first="
                          << first << " second=" << second << '\n' << std::flush;
                continue;
            }
            try {
                std::cerr << "[SnapManager] extrema generation first=" << first
                          << " second=" << second << '\n' << std::flush;
                const GeomAPI_ExtremaCurveCurve extrema(
                    curveA, curveB, firstA, lastA, firstB, lastB);
                std::cerr << "[SnapManager] extrema construction complete first=" << first
                          << " second=" << second << '\n' << std::flush;
                const int extremaCount = extrema.NbExtrema();
                std::cerr << "[SnapManager] extrema NbExtrema=" << extremaCount
                          << " first=" << first << " second=" << second
                          << '\n' << std::flush;
                for (int index = 1; index <= extremaCount; ++index) {
                    std::cerr << "[SnapManager] extrema solution index=" << index
                              << '/' << extremaCount << '\n' << std::flush;
                    std::cerr << "[SnapManager] before extrema.Distance index=" << index
                              << '\n' << std::flush;
                    if (extrema.Distance(index) > IntersectionTolerance) continue;
                    std::cerr << "[SnapManager] before extrema.Points index=" << index
                              << '\n' << std::flush;
                    gp_Pnt pointA;
                    gp_Pnt pointB;
                    extrema.Points(index, pointA, pointB);
                    std::cerr << "[SnapManager] after extrema.Points index=" << index
                              << '\n' << std::flush;
                    SnapReference reference;
                    reference.ownerId = edges[first].ownerId == edges[second].ownerId
                        ? edges[first].ownerId
                        : edges[first].ownerId + "+" + edges[second].ownerId;
                    reference.subshapeId = QStringLiteral("intersection:%1:%2:%3")
                        .arg(edges[first].subshapeId, edges[second].subshapeId)
                        .arg(index);
                    reference.type = SnapReferenceType::Intersection;
                    reference.point = gp_Pnt(
                        (pointA.X() + pointB.X()) * 0.5,
                        (pointA.Y() + pointB.Y()) * 0.5,
                        (pointA.Z() + pointB.Z()) * 0.5);
                    if (edges[first].topology) reference.topology = edges[first].topology;
                    if (edges[second].topology)
                        reference.relatedTopology.push_back(*edges[second].topology);
                    const QString key = referenceKey(reference);
                    if (points.insert(key).second) {
                        std::cerr << "[SnapManager] intersection cache insertion index="
                                  << index << '\n' << std::flush;
                        result.push_back(std::move(reference));
                        ++gLastIntersectionCandidateCount;
                    }
                }
            } catch (const Standard_Failure&) {
                continue;
            }
        }
    }
    std::cerr << "[SnapManager] buildIntersectionReferences exit resultCount="
              << result.size() << '\n' << std::flush;
    return result;
}

gp_Trsf correctionFor(
    const SnapReference& source,
    const SnapReference& target,
    const gp_Pnt& sourcePoint,
    const gp_Pnt& targetPoint
)
{
    if (source.type == SnapReferenceType::CircleCenter
        && target.type == SnapReferenceType::CircleCenter
        && source.axis && target.axis) {
        gp_Ax3 sourceFrame(source.axis->Location(), source.axis->Direction(),
                           perpendicular(source.axis->Direction()));
        gp_Ax3 targetFrame(target.axis->Location(), target.axis->Direction(),
                           perpendicular(target.axis->Direction()));
        gp_Trsf result;
        result.SetTransformation(sourceFrame, targetFrame);
        return result;
    }
    if (source.type == SnapReferenceType::Face && target.type == SnapReferenceType::Face
        && source.frame && target.frame) {
        gp_Ax3 mating = *target.frame;
        gp_Dir normal = mating.Direction();
        normal.Reverse();
        mating.SetDirection(normal);
        gp_Trsf result;
        // SetDisplacement maps the source coordinate frame itself onto the
        // mating frame. SetTransformation expresses coordinates between
        // systems and therefore does not produce the placement correction
        // required by an interactive object transform.
        result.SetDisplacement(*source.frame, mating);
        return result;
    }
    if (source.type == SnapReferenceType::Axis && target.type == SnapReferenceType::Axis
        && source.axis && target.axis) {
        gp_Ax3 sourceFrame(source.axis->Location(), source.axis->Direction(),
                           perpendicular(source.axis->Direction()));
        gp_Ax3 targetFrame(target.axis->Location(), target.axis->Direction(),
                           perpendicular(target.axis->Direction()));
        gp_Trsf result;
        result.SetTransformation(sourceFrame, targetFrame);
        return result;
    }
    gp_Trsf result;
    result.SetTranslation(gp_Vec(sourcePoint, targetPoint));
    return result;
}
}

std::vector<SnapReference> SnapManager::collectReferences(
    const QString& ownerId,
    const TopoDS_Shape& shape
)
{
    std::vector<SnapReference> result;
    int index = 0;
    std::cerr << "[SnapManager] collectReferences entry owner="
              << ownerId.toStdString() << " shapeNull=" << shape.IsNull() << '\n'
              << std::flush;
    std::cerr << "[SnapManager] vertex loop begin\n" << std::flush;
    for (TopExp_Explorer explorer(shape, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        ++index;
        std::cerr << "[SnapManager] vertex index=" << index << '\n' << std::flush;
        if (explorer.Current().IsNull() || explorer.Current().ShapeType() != TopAbs_VERTEX) {
            std::cerr << "[SnapManager] invalid vertex topology at index=" << index
                      << '\n' << std::flush;
            continue;
        }
        const auto vertex = TopoDS::Vertex(explorer.Current());
        SnapReference reference;
        reference.ownerId = ownerId;
        reference.subshapeId = QStringLiteral("vertex:%1").arg(index);
        reference.type = SnapReferenceType::Vertex;
        reference.shape = vertex;
        reference.point = BRep_Tool::Pnt(vertex);
        reference.topology = makeTopologyReference(ownerId, shape, vertex);
        result.push_back(std::move(reference));
    }
    index = 0;
    std::cerr << "[SnapManager] edge loop begin\n" << std::flush;
    for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        ++index;
        std::cerr << "[SnapManager] edge index=" << index << '\n' << std::flush;
        if (explorer.Current().IsNull() || explorer.Current().ShapeType() != TopAbs_EDGE) {
            std::cerr << "[SnapManager] invalid edge topology at index=" << index
                      << '\n' << std::flush;
            continue;
        }
        const auto edge = TopoDS::Edge(explorer.Current());
        Standard_Real first = 0.0;
        Standard_Real last = 0.0;
        const auto curve = BRep_Tool::Curve(edge, first, last);
        const auto topology = makeTopologyReference(ownerId, shape, edge);
        SnapReference edgeReference;
        edgeReference.ownerId = ownerId;
        edgeReference.subshapeId = QStringLiteral("edge:%1").arg(index);
        edgeReference.type = SnapReferenceType::Edge;
        edgeReference.shape = edge;
        edgeReference.topology = topology;
        if (!curve.IsNull()) edgeReference.point = curve->Value(first + (last - first) * 0.5);
        result.push_back(edgeReference);

        const auto firstVertex = TopExp::FirstVertex(edge, Standard_True);
        const auto lastVertex = TopExp::LastVertex(edge, Standard_True);
        if (!firstVertex.IsNull() && !lastVertex.IsNull()) {
            std::cerr << "[SnapManager] endpoint generation edge index=" << index
                      << '\n' << std::flush;
            SnapReference start;
            start.ownerId = ownerId;
            start.subshapeId = QStringLiteral("edge:%1:start").arg(index);
            start.type = SnapReferenceType::Endpoint;
            start.shape = firstVertex;
            start.point = BRep_Tool::Pnt(firstVertex);
            start.topology = makeTopologyReference(ownerId, shape, firstVertex);
            result.push_back(std::move(start));
            SnapReference end;
            end.ownerId = ownerId;
            end.subshapeId = QStringLiteral("edge:%1:end").arg(index);
            end.type = SnapReferenceType::Endpoint;
            end.shape = lastVertex;
            end.point = BRep_Tool::Pnt(lastVertex);
            end.topology = makeTopologyReference(ownerId, shape, lastVertex);
            result.push_back(std::move(end));
        }
        if (!curve.IsNull()) {
            std::cerr << "[SnapManager] midpoint generation edge index=" << index
                      << '\n' << std::flush;
            SnapReference midpoint;
            midpoint.ownerId = ownerId;
            midpoint.subshapeId = QStringLiteral("edge:%1:midpoint").arg(index);
            midpoint.type = SnapReferenceType::Midpoint;
            midpoint.shape = edge;
            midpoint.point = curve->Value(first + (last - first) * 0.5);
            midpoint.topology = topology;
            result.push_back(std::move(midpoint));
        }
        const auto circle = Handle(Geom_Circle)::DownCast(curve);
        if (!circle.IsNull()) {
            std::cerr << "[SnapManager] axis generation edge index=" << index
                      << '\n' << std::flush;
            const gp_Circ data = circle->Circ();
            SnapReference center;
            center.ownerId = ownerId;
            center.subshapeId = QStringLiteral("edge:%1:center").arg(index);
            center.type = SnapReferenceType::CircleCenter;
            center.shape = edge;
            center.point = data.Location();
            center.axis = gp_Ax1(data.Location(), data.Axis().Direction());
            center.topology = topology;
            result.push_back(std::move(center));
        }
    }
    index = 0;
    std::cerr << "[SnapManager] face loop begin\n" << std::flush;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        ++index;
        std::cerr << "[SnapManager] face index=" << index << '\n' << std::flush;
        if (explorer.Current().IsNull() || explorer.Current().ShapeType() != TopAbs_FACE) {
            std::cerr << "[SnapManager] invalid face topology at index=" << index
                      << '\n' << std::flush;
            continue;
        }
        const auto face = TopoDS::Face(explorer.Current());
        BRepGProp_Face properties(face);
        gp_Pnt point;
        gp_Vec normal;
        properties.Normal(0.5, 0.5, point, normal);
        auto frame = faceFrame(face, point);
        // Face alignment is intentionally limited to planar faces. Keeping
        // non-planar faces out of the reference cache also prevents them from
        // entering the generic FaceToFace candidate pipeline and falling back
        // to an ambiguous point translation.
        if (frame) {
            std::cerr << "[SnapManager] face candidate generation index=" << index
                      << '\n' << std::flush;
            SnapReference reference;
            reference.ownerId = ownerId;
            reference.subshapeId = QStringLiteral("face:%1").arg(index);
            reference.type = SnapReferenceType::Face;
            reference.shape = face;
            reference.point = point;
            reference.frame = frame;
            reference.topology = makeTopologyReference(ownerId, shape, face);
            result.push_back(std::move(reference));
        }
        BRepAdaptor_Surface surface(face, Standard_True);
        if (surface.GetType() == GeomAbs_Cylinder) {
            std::cerr << "[SnapManager] face axis generation index=" << index
                      << '\n' << std::flush;
            const gp_Ax1 axis = surface.Cylinder().Axis();
            SnapReference center;
            center.ownerId = ownerId;
            center.subshapeId = QStringLiteral("face:%1:center").arg(index);
            center.type = SnapReferenceType::CircleCenter;
            center.shape = face;
            center.point = axis.Location();
            center.axis = axis;
            result.push_back(std::move(center));
            SnapReference axisReference;
            axisReference.ownerId = ownerId;
            axisReference.subshapeId = QStringLiteral("face:%1:axis").arg(index);
            axisReference.type = SnapReferenceType::Axis;
            axisReference.shape = face;
            axisReference.point = axis.Location();
            axisReference.axis = axis;
            result.push_back(std::move(axisReference));
        }
    }
    std::cerr << "[SnapManager] collectReferences exit count=" << result.size()
              << '\n' << std::flush;
    for (auto& reference : result) {
        if (!reference.shape.IsNull()) {
            reference.geometry = std::make_shared<TopoDS_Shape>(reference.shape);
        }
    }
    gLastCollectedReferenceCount = result.size();
    return result;
}

std::vector<SnapReference> SnapManager::transformReferences(
    const std::vector<SnapReference>& references,
    const gp_Trsf& transform
)
{
    auto result = references;
    for (auto& reference : result) {
        reference.point.Transform(transform);
        if (reference.frame) {
            gp_Pnt origin = reference.frame->Location();
            origin.Transform(transform);
            gp_Dir normal = reference.frame->Direction();
            gp_Dir xDirection = reference.frame->XDirection();
            normal.Transform(transform);
            xDirection.Transform(transform);
            reference.frame = gp_Ax3(origin, normal, xDirection);
        }
        if (reference.axis) {
            gp_Pnt origin = reference.axis->Location();
            origin.Transform(transform);
            gp_Dir direction = reference.axis->Direction();
            direction.Transform(transform);
            reference.axis = gp_Ax1(origin, direction);
        }
    }
    return result;
}

std::vector<SnapCandidate> SnapManager::buildCandidates(
    const std::vector<SnapReference>& sources,
    const std::vector<SnapReference>& targets
) const
{
    std::cerr << "[SnapManager] buildCandidates entry sources=" << sources.size()
              << " targets=" << targets.size() << '\n' << std::flush;
    std::vector<SnapCandidate> result;
    std::vector<SnapReference> uniqueSources;
    std::vector<SnapReference> uniqueTargets;
    std::set<QString> sourceKeys;
    std::set<QString> targetKeys;
    std::size_t sourceIndex = 0;
    for (const auto& source : sources) {
        std::cerr << "[SnapManager] source dedup index=" << sourceIndex++ << '/'
                  << sources.size() << '\n' << std::flush;
        if (sourceKeys.insert(referenceKey(source)).second) {
            uniqueSources.push_back(source);
        }
    }
    std::size_t targetIndex = 0;
    for (const auto& target : targets) {
        std::cerr << "[SnapManager] target dedup index=" << targetIndex++ << '/'
                  << targets.size() << '\n' << std::flush;
        if (targetKeys.insert(referenceKey(target)).second) {
            uniqueTargets.push_back(target);
        }
    }
    gLastUniqueSourceReferenceCount = uniqueSources.size();
    gLastUniqueTargetReferenceCount = uniqueTargets.size();
    std::vector<SnapReference> edgeTargets;
    std::size_t uniqueTargetIndex = 0;
    for (const auto& target : uniqueTargets) {
        std::cerr << "[SnapManager] edge-target scan index=" << uniqueTargetIndex++
                  << '/' << uniqueTargets.size() << '\n' << std::flush;
        if (target.type == SnapReferenceType::Edge) edgeTargets.push_back(target);
    }
    std::cerr << "[SnapManager] before intersection generation edgeTargets="
              << edgeTargets.size() << '\n' << std::flush;
    for (auto& intersection : buildIntersectionReferences(edgeTargets)) {
        std::cerr << "[SnapManager] intersection dedup/cache insertion\n" << std::flush;
        if (targetKeys.insert(referenceKey(intersection)).second)
            uniqueTargets.push_back(std::move(intersection));
    }
    std::cerr << "[SnapManager] after intersection generation uniqueTargets="
              << uniqueTargets.size() << '\n' << std::flush;
    sourceIndex = 0;
    for (const auto& source : uniqueSources) {
        std::cerr << "[SnapManager] candidate source loop index=" << sourceIndex
                  << '/' << uniqueSources.size() << '\n' << std::flush;
        std::size_t candidateTargetIndex = 0;
        for (const auto& target : uniqueTargets) {
            std::cerr << "[SnapManager] candidate target loop source=" << sourceIndex
                      << " target=" << candidateTargetIndex++ << '/'
                      << uniqueTargets.size() << '\n' << std::flush;
            // A feature must never snap to its own references. In the viewer
            // this is normally filtered while building the target cache, but
            // keeping the invariant here also protects direct/test callers.
            const auto referencesOwner = [&source](const auto& reference) {
                return reference && QString::fromStdString(reference->featureId) == source.ownerId;
            };
            if ((!source.ownerId.isEmpty() && source.ownerId == target.ownerId)
                || referencesOwner(target.topology)
                || std::any_of(target.relatedTopology.begin(), target.relatedTopology.end(),
                    [&source](const auto& reference) {
                        return QString::fromStdString(reference.featureId) == source.ownerId;
                    })) {
                continue;
            }
            const auto kind = compatible(source, target);
            if (!kind) continue;
            std::cerr << "[SnapManager] face/edge/vertex candidate generation source="
                      << sourceIndex << '\n' << std::flush;
            // Exact edge/face projection is deliberately deferred until a candidate
            // is actually close to the cursor. Doing it here made pattern features
            // turn source x target pairing into thousands of OCCT distance queries.
            auto cachedSource = source;
            auto cachedTarget = target;
            if (cachedSource.geometry) cachedSource.shape.Nullify();
            if (cachedTarget.geometry) cachedTarget.shape.Nullify();
            result.push_back({
                std::move(cachedSource),
                std::move(cachedTarget),
                *kind,
                target.point,
                gp_Trsf(),
                0.0,
                specificity(*kind),
                sourceIndex
            });
            std::cerr << "[SnapManager] candidate cache insertion resultCount="
                      << result.size() << '\n' << std::flush;
        }
        ++sourceIndex;
    }
    gLastCandidateCount = result.size();
    std::cerr << "[SnapManager] buildCandidates exit resultCount=" << result.size()
              << '\n' << std::flush;
    return result;
}

std::optional<SnapCandidate> SnapManager::findCandidate(
    const std::vector<SnapReference>& sources,
    const std::vector<SnapReference>& targets,
    const std::optional<SnapCandidate>& active,
    const std::function<QPointF(const gp_Pnt&)>& project
) const
{
    const auto candidates = buildCandidates(sources, targets);
    return findCandidate(candidates, gp_Trsf(), project, active);
}

std::optional<SnapCandidate> SnapManager::findCandidate(
    const std::vector<SnapCandidate>& candidates,
    const gp_Trsf& previewTransform,
    const std::function<QPointF(const gp_Pnt&)>& project,
    const std::optional<SnapCandidate>& active,
    const SnapScreenIndex* screenIndex
) const
{
    const double limit = active
        ? ActivationTolerancePixels * HysteresisMultiplier
        : ActivationTolerancePixels;
    std::size_t sourceCount = 0;
    for (const auto& candidate : candidates) {
        sourceCount = std::max(sourceCount, candidate.sourceIndex + 1);
    }
    std::vector<QPointF> sourceScreens(sourceCount);
    std::vector<bool> sourceProjected(sourceCount, false);
    for (const auto& candidate : candidates) {
        if (sourceProjected[candidate.sourceIndex]) continue;
        if (project) {
            gp_Pnt sourcePoint = candidate.source.point;
            sourcePoint.Transform(previewTransform);
            sourceScreens[candidate.sourceIndex] = project(sourcePoint);
        } else {
            sourceScreens[candidate.sourceIndex] = candidate.source.screenPoint;
        }
        sourceProjected[candidate.sourceIndex] = true;
    }
    std::optional<SnapCandidate> result;
    std::vector<std::size_t> candidateIndices;
    if (screenIndex) {
        std::vector<bool> visited(candidates.size(), false);
        for (const auto& sourceScreen : sourceScreens) {
            for (const auto index : screenIndex->nearby(sourceScreen, limit)) {
                if (index < visited.size() && !visited[index]) {
                    visited[index] = true;
                    candidateIndices.push_back(index);
                }
            }
        }
    } else {
        candidateIndices.resize(candidates.size());
        std::iota(candidateIndices.begin(), candidateIndices.end(), 0);
    }
    for (const auto candidateIndex : candidateIndices) {
            const auto& cached = candidates[candidateIndex];
            const QPointF sourceScreen = sourceScreens[cached.sourceIndex];
            const QPointF targetScreen = cached.target.screenPoint;
            const double distance = std::hypot(
                sourceScreen.x() - targetScreen.x(),
                sourceScreen.y() - targetScreen.y());
            if (distance > limit) continue;
            SnapCandidate candidate = cached;
            candidate.screenDistance = distance;
            if (active && candidate.id() == active->id()) {
                result = std::move(candidate);
                break;
            }
            if (!result || candidate.specificity > result->specificity
                || (candidate.specificity == result->specificity
                    && distance + TieTolerance < result->screenDistance)) {
                result = std::move(candidate);
            }
    }
    if (result) {
        auto source = result->source;
        source.point.Transform(previewTransform);
        if (source.frame) {
            gp_Pnt origin = source.frame->Location();
            origin.Transform(previewTransform);
            gp_Dir normal = source.frame->Direction();
            gp_Dir xDirection = source.frame->XDirection();
            normal.Transform(previewTransform);
            xDirection.Transform(previewTransform);
            source.frame = gp_Ax3(origin, normal, xDirection);
        }
        if (source.axis) {
            gp_Pnt origin = source.axis->Location();
            origin.Transform(previewTransform);
            gp_Dir direction = source.axis->Direction();
            direction.Transform(previewTransform);
            source.axis = gp_Ax1(origin, direction);
        }
        result->source = source;
        if (result->target.type == SnapReferenceType::Edge
            || result->target.type == SnapReferenceType::Face) {
            const auto exactTarget = closestPoint(source.point, result->target);
            if (!exactTarget) return std::nullopt;
            result->targetPoint = *exactTarget;
        }
        result->correction = correctionFor(
            source, result->target, source.point, result->targetPoint);
    }
    return result;
}

} // namespace cad::viewer
