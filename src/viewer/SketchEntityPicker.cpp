#include "viewer/SketchEntityPicker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cad::viewer {
namespace {

double distanceToSegment(const QPointF& point, const QPointF& start, const QPointF& end)
{
    const QPointF segment = end - start;
    const double lengthSquared = QPointF::dotProduct(segment, segment);
    if (lengthSquared <= 1.0e-12) return std::numeric_limits<double>::infinity();
    const double projection = std::clamp(
        QPointF::dotProduct(point - start, segment) / lengthSquared, 0.0, 1.0);
    const QPointF nearest = start + segment * projection;
    const QPointF delta = point - nearest;
    return std::hypot(delta.x(), delta.y());
}

} // namespace

std::optional<cad::parametric::SketchEntityId> pickSketchLine(
    const std::vector<SketchLineScreenCandidate>& candidates,
    const QPointF& position,
    const double tolerancePixels)
{
    if (tolerancePixels < 0.0) return std::nullopt;

    std::optional<cad::parametric::SketchEntityId> result;
    double bestDistance = std::numeric_limits<double>::infinity();
    bool bestConstruction = false;
    for (const auto& candidate : candidates) {
        const double distance = distanceToSegment(position, candidate.start, candidate.end);
        if (distance > tolerancePixels) continue;
        const bool closer = distance < bestDistance - 1.0e-9;
        const bool overlappingConstruction = candidate.construction && bestConstruction
            ? distance <= bestDistance + 1.0e-9
            : candidate.construction && !bestConstruction
                && distance <= bestDistance + 1.0e-9;
        if (!result || closer || overlappingConstruction) {
            result = candidate.id;
            bestDistance = distance;
            bestConstruction = candidate.construction;
        }
    }
    return result;
}

} // namespace cad::viewer
