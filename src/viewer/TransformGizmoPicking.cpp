#include "viewer/TransformGizmoPicking.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cad::viewer {

namespace
{
constexpr double TieTolerance = 1.0e-9;

double pointDistance(const QPointF& first, const QPointF& second)
{
    return std::hypot(first.x() - second.x(), first.y() - second.y());
}

double polylineDistance(
    const QPointF& point,
    const std::vector<QPointF>& points,
    const bool closed
)
{
    if (points.empty()) return std::numeric_limits<double>::infinity();
    if (points.size() == 1) return pointDistance(point, points.front());

    double best = std::numeric_limits<double>::infinity();
    for (std::size_t index = 1; index < points.size(); ++index) {
        best = std::min(best, pointToProjectedSegmentDistance(
            point, points[index - 1], points[index]));
    }
    if (closed) {
        best = std::min(best, pointToProjectedSegmentDistance(
            point, points.back(), points.front()));
    }
    return best;
}

double distanceToGeometry(
    const QPointF& point,
    const ScreenHandleGeometry& geometry
)
{
    switch (geometry.kind) {
    case ScreenHandleGeometryKind::Segment:
        return geometry.points.size() >= 2
            ? pointToProjectedSegmentDistance(
                point, geometry.points[0], geometry.points[1])
            : std::numeric_limits<double>::infinity();
    case ScreenHandleGeometryKind::Polyline:
        return polylineDistance(point, geometry.points, true);
    case ScreenHandleGeometryKind::Circle:
        return pointToProjectedCircleDistance(
            point, geometry.center, geometry.radius);
    case ScreenHandleGeometryKind::Point:
        return pointDistance(point, geometry.center);
    }
    return std::numeric_limits<double>::infinity();
}
}

double pointToProjectedSegmentDistance(
    const QPointF& point,
    const QPointF& first,
    const QPointF& second
)
{
    const QPointF direction = second - first;
    const double lengthSquared = QPointF::dotProduct(direction, direction);
    if (lengthSquared <= 1.0e-12) {
        return std::hypot(point.x() - first.x(), point.y() - first.y());
    }
    const double parameter = std::clamp(
        QPointF::dotProduct(point - first, direction) / lengthSquared,
        0.0,
        1.0
    );
    return std::hypot(
        point.x() - (first.x() + direction.x() * parameter),
        point.y() - (first.y() + direction.y() * parameter)
    );
}

double pointToProjectedCircleDistance(
    const QPointF& point,
    const QPointF& center,
    const double radius
)
{
    return std::abs(std::hypot(
        point.x() - center.x(), point.y() - center.y()) - radius);
}

std::optional<TransformHandle> pickClosestHandle(
    const QPointF& point,
    const std::vector<ScreenHandleGeometry>& geometries
)
{
    std::optional<TransformHandle> result;
    double bestDistance = std::numeric_limits<double>::infinity();
    bool bestIsCenter = false;
    for (const auto& geometry : geometries) {
        const double distance = distanceToGeometry(point, geometry);
        if (distance > geometry.tolerance) continue;
        const bool isCenter = geometry.handle == TransformHandle::Center;
        const bool closer = distance + TieTolerance < bestDistance;
        const bool equalCenter = isCenter && bestIsCenter == false
            && std::abs(distance - bestDistance) <= TieTolerance;
        if (!result || closer || equalCenter) {
            result = geometry.handle;
            bestDistance = distance;
            bestIsCenter = isCenter;
        }
    }
    return result;
}

} // namespace cad::viewer
