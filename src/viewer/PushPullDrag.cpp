#include "viewer/PushPullDrag.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace cad::viewer {

namespace
{
constexpr double DirectionTolerance = 1.0e-9;
}

std::optional<gp_Pnt> intersectRayWithPlane(
    const gp_Pnt& rayOrigin,
    const gp_Dir& rayDirection,
    const gp_Pln& plane
)
{
    const gp_Dir planeNormal = plane.Axis().Direction();
    const gp_Vec direction(rayDirection);
    const double denominator = direction.Dot(gp_Vec(planeNormal));
    if (std::abs(denominator) <= DirectionTolerance) {
        return std::nullopt;
    }

    const double parameter =
        gp_Vec(rayOrigin, plane.Location()).Dot(gp_Vec(planeNormal))
        / denominator;
    if (parameter < 0.0) {
        return std::nullopt;
    }

    return rayOrigin.Translated(direction * parameter);
}

std::optional<PushPullDragState> makePushPullDragState(
    const gp_Pnt& anchor,
    const gp_Dir& normal,
    const gp_Dir& viewDirection,
    const gp_Dir& viewUp,
    const gp_Dir& viewSide
)
{
    const gp_Vec normalVector(normal);
    const gp_Vec viewVector(viewDirection);

    // The primary plane contains the face normal and the camera direction
    // projected onto the face tangent plane. Its normal is therefore as
    // close as possible to the viewing direction.
    const gp_Vec tangent = viewVector.Crossed(normalVector);
    gp_Vec planeNormal = normalVector.Crossed(tangent);

    // Near a head-on view the projected normal vanishes. Try screen axes to
    // keep the choice deterministic. If the view is exactly parallel to the
    // normal, use a camera-up depth-control plane: a plane containing the
    // normal would be parallel to the rays and could not be intersected.
    if (planeNormal.Magnitude() <= DirectionTolerance) {
        const gp_Vec candidates[] = {
            normalVector.Crossed(gp_Vec(viewUp)),
            normalVector.Crossed(gp_Vec(viewSide))
        };
        const auto best = std::max_element(
            std::begin(candidates),
            std::end(candidates),
            [&viewVector](const gp_Vec& left, const gp_Vec& right) {
                return std::abs(left.Dot(viewVector))
                    < std::abs(right.Dot(viewVector));
            }
        );
        if (best != std::end(candidates)
            && best->Magnitude() > DirectionTolerance
            && std::abs(best->Dot(viewVector)) > DirectionTolerance) {
            planeNormal = *best;
        } else {
            planeNormal = viewVector + gp_Vec(viewUp);
            if (planeNormal.Magnitude() <= DirectionTolerance) {
                planeNormal = viewVector + gp_Vec(viewSide);
            }
            if (planeNormal.Magnitude() <= DirectionTolerance) {
                return std::nullopt;
            }
        }
    }

    return PushPullDragState{
        anchor,
        normal,
        gp_Pln(anchor, gp_Dir(planeNormal)),
        0.0
    };
}

std::optional<double> computePushPullDistance(
    const PushPullDragState& state,
    const gp_Pnt& rayOrigin,
    const gp_Dir& rayDirection
)
{
    const auto point = intersectRayWithPlane(
        rayOrigin,
        rayDirection,
        state.dragPlane
    );
    if (!point) {
        return std::nullopt;
    }

    return gp_Vec(state.anchor, *point).Dot(gp_Vec(state.normal))
        + state.initialDistance;
}

} // namespace cad::viewer
