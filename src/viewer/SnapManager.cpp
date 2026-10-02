#include "viewer/SnapManager.h"

#include <algorithm>
#include <cmath>

namespace cad::viewer {

std::optional<SnapCandidate> SnapManager::findCandidate(
    const QPointF& sourceScreenPoint,
    const std::vector<SnapTarget>& targets,
    const std::optional<SnapCandidate>& active
) const
{
    const double limit = active
        ? ActivationTolerancePixels * HysteresisMultiplier
        : ActivationTolerancePixels;
    std::optional<SnapCandidate> result;
    double bestDistance = limit;
    for (const auto& target : targets) {
        const double distance = std::hypot(
            sourceScreenPoint.x() - target.screenPoint.x(),
            sourceScreenPoint.y() - target.screenPoint.y()
        );
        if (active && target.id == active->id && distance <= limit) {
            return SnapCandidate{
                target.id, target.kind, target.point, distance};
        }
        if (distance < bestDistance) {
            bestDistance = distance;
            result = SnapCandidate{
                target.id, target.kind, target.point, distance};
        }
    }
    return result;
}

} // namespace cad::viewer
