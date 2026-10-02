#include "viewer/PushPullDrag.h"
#include "viewer/SnapManager.h"
#include "viewer/TransformMath.h"

#include <cassert>
#include <cmath>

namespace
{
bool almostEqual(const double left, const double right)
{
    return std::abs(left - right) < 1.0e-9;
}
}

int main()
{
    using namespace cad::viewer;

    const auto state = makePushPullDragState(
        gp_Pnt(0.0, 0.0, 0.0),
        gp_Dir(0.0, 0.0, 1.0),
        gp_Dir(0.0, -1.0, -1.0),
        gp_Dir(0.0, 0.0, 1.0),
        gp_Dir(1.0, 0.0, 0.0)
    );
    assert(state);

    const auto positive = computePushPullDistance(
        *state,
        gp_Pnt(0.0, 2.0, 3.0),
        gp_Dir(0.0, -1.0, 0.0)
    );
    const auto negative = computePushPullDistance(
        *state,
        gp_Pnt(0.0, 2.0, -3.0),
        gp_Dir(0.0, -1.0, 0.0)
    );
    assert(positive && almostEqual(*positive, 3.0));
    assert(negative && almostEqual(*negative, -3.0));

    // The calculation is absolute from the drag start, not accumulated.
    const auto first = computePushPullDistance(
        *state,
        gp_Pnt(5.0, 2.0, 2.0),
        gp_Dir(0.0, -1.0, 0.0)
    );
    const auto later = computePushPullDistance(
        *state,
        gp_Pnt(-7.0, 2.0, 4.0),
        gp_Dir(0.0, -1.0, 0.0)
    );
    assert(first && later);
    assert(almostEqual(*first, 2.0));
    assert(almostEqual(*later, 4.0));

    // A different orthographic ray origin (zoom/viewport projection) still
    // gives the same world displacement when it hits the same drag plane.
    const auto otherRay = computePushPullDistance(
        *state,
        gp_Pnt(100.0, 20.0, 3.0),
        gp_Dir(0.0, -1.0, 0.0)
    );
    assert(otherRay && almostEqual(*otherRay, 3.0));

    // A head-on normal uses the stable camera-up depth-control fallback.
    const auto headOn = makePushPullDragState(
        gp_Pnt(0.0, 0.0, 0.0),
        gp_Dir(0.0, 0.0, 1.0),
        gp_Dir(0.0, 0.0, -1.0),
        gp_Dir(0.0, 1.0, 0.0),
        gp_Dir(1.0, 0.0, 0.0)
    );
    assert(headOn);
    const auto headOnDistance = computePushPullDistance(
        *headOn,
        gp_Pnt(0.0, 1.0, 2.0),
        gp_Dir(0.0, 0.0, -1.0)
    );
    assert(headOnDistance && std::isfinite(*headOnDistance));

    const ViewRay startRay{gp_Pnt(-1.0, -1.0, 1.0), gp_Dir(0.0, 1.0, 0.0)};
    const ViewRay currentRay{gp_Pnt(-1.0, -1.0, 3.5), gp_Dir(0.0, 1.0, 0.0)};
    const auto transformDistance = translationDelta(*state, startRay, currentRay);
    assert(transformDistance && almostEqual(*transformDistance, 2.5));

    const auto translation = translationTransform(gp_Dir(0.0, 0.0, 1.0), 2.5);
    gp_Pnt translated(0.0, 0.0, 0.0);
    translated.Transform(translation);
    assert(almostEqual(translated.Z(), 2.5));

    const auto angle = rotationDelta(
        gp_Pnt(0.0, 0.0, 0.0),
        gp_Dir(0.0, 0.0, 1.0),
        ViewRay{gp_Pnt(0.0, -1.0, 1.0), gp_Dir(0.0, 0.0, -1.0)},
        ViewRay{gp_Pnt(1.0, 0.0, 1.0), gp_Dir(0.0, 0.0, -1.0)}
    );
    assert(angle && almostEqual(*angle, 1.5707963267948966));

    SnapManager snapManager;
    const std::vector<SnapTarget> targets{
        {QStringLiteral("vertex-a"), SnapKind::VertexToVertex, gp_Pnt(), QPointF(100.0, 100.0)},
        {QStringLiteral("vertex-b"), SnapKind::VertexToVertex, gp_Pnt(), QPointF(130.0, 100.0)}
    };
    const auto snapped = snapManager.findCandidate(QPointF(108.0, 100.0), targets, std::nullopt);
    assert(snapped && snapped->id == QStringLiteral("vertex-a"));
    const auto held = snapManager.findCandidate(QPointF(115.0, 100.0), targets, snapped);
    assert(held && held->id == QStringLiteral("vertex-a"));
    const auto released = snapManager.findCandidate(QPointF(125.0, 100.0), targets, held);
    assert(released && released->id == QStringLiteral("vertex-b"));

    return 0;
}
