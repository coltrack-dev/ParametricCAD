#pragma once

#include "viewer/PushPullDrag.h"

#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>

#include <optional>

namespace cad::viewer {

struct ViewRay
{
    gp_Pnt origin;
    gp_Dir direction;
};

std::optional<double> translationDelta(
    const PushPullDragState& drag,
    const ViewRay& startRay,
    const ViewRay& currentRay
);

std::optional<double> rotationDelta(
    const gp_Pnt& pivot,
    const gp_Dir& axis,
    const ViewRay& startRay,
    const ViewRay& currentRay
);

gp_Trsf translationTransform(const gp_Dir& axis, double distance);
gp_Trsf rotationTransform(const gp_Pnt& pivot, const gp_Dir& axis, double angle);

// Camera translation that restores the pre-zoom world anchor at the cursor.
gp_Vec zoomAnchorCorrection(const gp_Pnt& pointBefore, const gp_Pnt& pointAfter);

} // namespace cad::viewer
