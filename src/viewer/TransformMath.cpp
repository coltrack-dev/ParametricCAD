#include "viewer/TransformMath.h"

#include <cmath>

namespace cad::viewer {

std::optional<double> translationDelta(
    const PushPullDragState& drag,
    const ViewRay& startRay,
    const ViewRay& currentRay
)
{
    const auto start = intersectRayWithPlane(
        startRay.origin, startRay.direction, drag.dragPlane);
    const auto current = intersectRayWithPlane(
        currentRay.origin, currentRay.direction, drag.dragPlane);
    if (!start || !current) return std::nullopt;
    return gp_Vec(*start, *current).Dot(gp_Vec(drag.normal));
}

std::optional<double> rotationDelta(
    const gp_Pnt& pivot,
    const gp_Dir& axis,
    const ViewRay& startRay,
    const ViewRay& currentRay
)
{
    const gp_Pln plane(pivot, axis);
    const auto start = intersectRayWithPlane(
        startRay.origin, startRay.direction, plane);
    const auto current = intersectRayWithPlane(
        currentRay.origin, currentRay.direction, plane);
    if (!start || !current) return std::nullopt;

    gp_Vec startVector(pivot, *start);
    gp_Vec currentVector(pivot, *current);
    if (startVector.Magnitude() <= 1.0e-9
        || currentVector.Magnitude() <= 1.0e-9) {
        return std::nullopt;
    }
    return startVector.AngleWithRef(currentVector, gp_Vec(axis));
}

gp_Trsf translationTransform(const gp_Dir& axis, const double distance)
{
    gp_Trsf result;
    result.SetTranslation(gp_Vec(axis) * distance);
    return result;
}

gp_Trsf rotationTransform(
    const gp_Pnt& pivot,
    const gp_Dir& axis,
    const double angle
)
{
    gp_Trsf result;
    result.SetRotation(gp_Ax1(pivot, axis), angle);
    return result;
}

} // namespace cad::viewer
