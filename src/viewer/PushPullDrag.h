#pragma once

#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include <optional>

namespace cad::viewer {

struct PushPullDragState
{
    gp_Pnt anchor;
    gp_Dir normal;
    gp_Pln dragPlane;
    double initialDistance{0.0};
};

std::optional<gp_Pnt> intersectRayWithPlane(
    const gp_Pnt& rayOrigin,
    const gp_Dir& rayDirection,
    const gp_Pln& plane
);

std::optional<PushPullDragState> makePushPullDragState(
    const gp_Pnt& anchor,
    const gp_Dir& normal,
    const gp_Dir& viewDirection,
    const gp_Dir& viewUp,
    const gp_Dir& viewSide
);

std::optional<double> computePushPullDistance(
    const PushPullDragState& state,
    const gp_Pnt& rayOrigin,
    const gp_Dir& rayDirection
);

} // namespace cad::viewer
