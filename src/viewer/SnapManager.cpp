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
#include <QDebug>
#include <QElapsedTimer>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <algorithm>
#include <limits>
#include <map>
#include <numeric>
#include <set>

namespace cad::viewer {

namespace
{
constexpr double TieTolerance = 1.0e-6;
constexpr double ReferenceDedupTolerance = 1.0e-6;

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
    if (!reference.geometry && reference.shape.IsNull()) {
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
    QElapsedTimer vertexTimer;
    vertexTimer.start();
    int index = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        ++index;
        const auto vertex = TopoDS::Vertex(explorer.Current());
        result.push_back({ownerId, QStringLiteral("vertex:%1").arg(index),
                          SnapReferenceType::Vertex, vertex, BRep_Tool::Pnt(vertex),
                          std::nullopt, std::nullopt, {}, {}});
    }
    if (vertexTimer.elapsed() > 2) {
        qWarning() << "SnapManager vertex extraction took"
                   << vertexTimer.elapsed() << "ms";
    }
    QElapsedTimer edgeTimer;
    edgeTimer.start();
    index = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        ++index;
        const auto edge = TopoDS::Edge(explorer.Current());
        gp_Pnt midpoint;
        Standard_Real first = 0.0;
        Standard_Real last = 0.0;
        const auto curve = BRep_Tool::Curve(edge, first, last);
        if (!curve.IsNull()) {
            midpoint = curve->Value(first + (last - first) * 0.5);
        }
        result.push_back({ownerId, QStringLiteral("edge:%1").arg(index),
                          SnapReferenceType::Edge, edge, midpoint, std::nullopt,
                          std::nullopt, {}, {}});
        const auto circle = Handle(Geom_Circle)::DownCast(curve);
        if (!circle.IsNull()) {
            const gp_Circ data = circle->Circ();
            result.push_back({ownerId, QStringLiteral("edge:%1:center").arg(index),
                              SnapReferenceType::CircleCenter, edge, data.Location(),
                              std::nullopt,
                              gp_Ax1(data.Location(), data.Axis().Direction()), {}, {}});
        }
    }
    if (edgeTimer.elapsed() > 2) {
        qWarning() << "SnapManager edge extraction/OCCT geometry access took"
                   << edgeTimer.elapsed() << "ms";
    }
    QElapsedTimer faceTimer;
    faceTimer.start();
    index = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        ++index;
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
            result.push_back({ownerId, QStringLiteral("face:%1").arg(index),
                              SnapReferenceType::Face, face, point, frame,
                              std::nullopt, {}, {}});
        }
        BRepAdaptor_Surface surface(face, Standard_True);
        if (surface.GetType() == GeomAbs_Cylinder) {
            const gp_Ax1 axis = surface.Cylinder().Axis();
            result.push_back({ownerId, QStringLiteral("face:%1:center").arg(index),
                              SnapReferenceType::CircleCenter, face, axis.Location(),
                              std::nullopt, axis, {}, {}});
            result.push_back({ownerId, QStringLiteral("face:%1:axis").arg(index),
                              SnapReferenceType::Axis, face, axis.Location(),
                              std::nullopt, axis, {}, {}});
        }
    }
    if (faceTimer.elapsed() > 2) {
        qWarning() << "SnapManager face extraction/frame construction took"
                   << faceTimer.elapsed() << "ms";
    }
    for (auto& reference : result) {
        if (!reference.shape.IsNull()) {
            reference.geometry = std::make_shared<TopoDS_Shape>(reference.shape);
        }
    }
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
    QElapsedTimer timer;
    timer.start();
    std::vector<SnapCandidate> result;
    std::map<QString, std::size_t> candidatesBySource;
    std::map<QString, std::size_t> candidatesByTarget;
    std::vector<SnapReference> uniqueSources;
    std::vector<SnapReference> uniqueTargets;
    std::set<QString> sourceKeys;
    std::set<QString> targetKeys;
    for (const auto& source : sources) {
        if (sourceKeys.insert(referenceKey(source)).second) {
            uniqueSources.push_back(source);
        }
    }
    for (const auto& target : targets) {
        if (targetKeys.insert(referenceKey(target)).second) {
            uniqueTargets.push_back(target);
        }
    }
    std::size_t sourceIndex = 0;
    for (const auto& source : uniqueSources) {
        for (const auto& target : uniqueTargets) {
            // A feature must never snap to its own references. In the viewer
            // this is normally filtered while building the target cache, but
            // keeping the invariant here also protects direct/test callers.
            if (!source.ownerId.isEmpty() && source.ownerId == target.ownerId) {
                continue;
            }
            const auto kind = compatible(source, target);
            if (!kind) continue;
            // Exact edge/face projection is deliberately deferred until a candidate
            // is actually close to the cursor. Doing it here made pattern features
            // turn source x target pairing into thousands of OCCT distance queries.
            auto cachedSource = source;
            auto cachedTarget = target;
            if (cachedSource.geometry) cachedSource.shape.Nullify();
            if (cachedTarget.geometry) cachedTarget.shape.Nullify();
            candidatesBySource[source.ownerId]++;
            candidatesByTarget[target.ownerId]++;
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
        }
        ++sourceIndex;
    }
    if (timer.elapsed() > 2) {
        qWarning() << "SnapManager candidate collection/OCCT projection took"
                   << timer.elapsed() << "ms for" << result.size() << "candidates"
                   << "(sources" << sources.size() << "->" << uniqueSources.size()
                   << ", targets" << targets.size() << "->" << uniqueTargets.size() << ')';
        std::vector<std::pair<QString, std::size_t>> contributors(
            candidatesByTarget.begin(), candidatesByTarget.end());
        std::sort(contributors.begin(), contributors.end(),
            [](const auto& left, const auto& right) { return left.second > right.second; });
        const int limit = std::min<int>(5, contributors.size());
        for (int index = 0; index < limit; ++index) {
            qWarning() << "SnapManager target contributor"
                       << contributors[static_cast<std::size_t>(index)].first
                       << contributors[static_cast<std::size_t>(index)].second;
        }
        std::vector<std::pair<QString, std::size_t>> sourceContributors(
            candidatesBySource.begin(), candidatesBySource.end());
        std::sort(sourceContributors.begin(), sourceContributors.end(),
            [](const auto& left, const auto& right) { return left.second > right.second; });
        const int sourceLimit = std::min<int>(5, sourceContributors.size());
        for (int index = 0; index < sourceLimit; ++index) {
            qWarning() << "SnapManager source contributor"
                       << sourceContributors[static_cast<std::size_t>(index)].first
                       << sourceContributors[static_cast<std::size_t>(index)].second;
        }
    }
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
    QElapsedTimer totalTimer;
    totalTimer.start();
    const double limit = active
        ? ActivationTolerancePixels * HysteresisMultiplier
        : ActivationTolerancePixels;
    QElapsedTimer projectionTimer;
    projectionTimer.start();
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
    if (projectionTimer.elapsed() > 2) {
        qWarning() << "SnapManager source projection phase took"
                   << projectionTimer.elapsed() << "ms for" << sourceCount << "sources";
    }

    QElapsedTimer distanceTimer;
    distanceTimer.start();
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
            if (!result || distance + TieTolerance < result->screenDistance
                || (std::abs(distance - result->screenDistance) <= TieTolerance
                    && candidate.specificity > result->specificity)) {
                result = std::move(candidate);
            }
    }
    if (distanceTimer.elapsed() > 2) {
        qWarning() << "SnapManager nearest-distance phase took"
                   << distanceTimer.elapsed() << "ms for" << candidates.size() << "candidates";
    }
    if (result) {
        QElapsedTimer correctionTimer;
        correctionTimer.start();
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
        if (correctionTimer.elapsed() > 2) {
            qWarning() << "SnapManager candidate correction phase took"
                       << correctionTimer.elapsed() << "ms";
        }
    }
    if (totalTimer.elapsed() > 2) {
        qWarning() << "SnapManager::findCandidate total took"
                   << totalTimer.elapsed() << "ms";
    }
    return result;
}

} // namespace cad::viewer
