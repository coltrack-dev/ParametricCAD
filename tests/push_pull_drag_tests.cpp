#include "viewer/PushPullDrag.h"
#include "viewer/SnapManager.h"
#include "viewer/TransformMath.h"
#include "viewer/TransformGizmoPicking.h"

#include <cassert>
#include <cmath>
#include <algorithm>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>

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
    const std::vector<SnapReference> sources{
        {QStringLiteral("moving"), QStringLiteral("vertex:1"),
         SnapReferenceType::Vertex, {}, gp_Pnt(), std::nullopt, std::nullopt,
         QPointF(100.0, 100.0)}
    };
    const std::vector<SnapReference> targets{
        {QStringLiteral("target"), QStringLiteral("vertex-a"),
         SnapReferenceType::Vertex, {}, gp_Pnt(), std::nullopt, std::nullopt,
         QPointF(100.0, 100.0)},
        {QStringLiteral("target"), QStringLiteral("vertex-b"),
         SnapReferenceType::Vertex, {}, gp_Pnt(), std::nullopt, std::nullopt,
         QPointF(130.0, 100.0)}
    };
    auto moving = sources;
    moving[0].screenPoint = QPointF(108.0, 100.0);
    const auto snapped = snapManager.findCandidate(moving, targets, std::nullopt);
    assert(snapped && snapped->target.subshapeId == QStringLiteral("vertex-a"));
    moving[0].screenPoint = QPointF(115.0, 100.0);
    const auto held = snapManager.findCandidate(moving, targets, snapped);
    assert(held && held->target.subshapeId == QStringLiteral("vertex-a"));
    moving[0].screenPoint = QPointF(125.0, 100.0);
    const auto released = snapManager.findCandidate(moving, targets, held);
    assert(released && released->target.subshapeId == QStringLiteral("vertex-b"));

    assert(almostEqual(pointToProjectedSegmentDistance(
        QPointF(10.0, 5.0), QPointF(0.0, 0.0), QPointF(20.0, 0.0)), 5.0));
    assert(almostEqual(pointToProjectedCircleDistance(
        QPointF(13.0, 0.0), QPointF(0.0, 0.0), 10.0), 3.0));

    const std::vector<ScreenHandleGeometry> handles{
        {TransformHandle::TranslateX, ScreenHandleGeometryKind::Segment,
         {QPointF(0.0, 0.0), QPointF(100.0, 0.0)}, {}, 0.0, 14.0},
        {TransformHandle::RotateZ, ScreenHandleGeometryKind::Circle,
         {}, QPointF(50.0, 50.0), 30.0, 10.0},
        {TransformHandle::Center, ScreenHandleGeometryKind::Point,
         {}, QPointF(0.0, 0.0), 0.0, 18.0}
    };
    assert(pickClosestHandle(QPointF(72.0, 1.0), handles)
               == TransformHandle::TranslateX);
    assert(pickClosestHandle(QPointF(50.0, 20.0), handles)
               == TransformHandle::RotateZ);
    assert(pickClosestHandle(QPointF(0.0, 0.0), handles)
               == TransformHandle::Center);

    const auto targetBox = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    const auto references = snapManager.collectReferences(
        QStringLiteral("target-box"), targetBox);
    const auto edge = std::find_if(references.begin(), references.end(), [](const auto& ref) {
        return ref.type == SnapReferenceType::Edge;
    });
    const auto face = std::find_if(references.begin(), references.end(), [](const auto& ref) {
        return ref.type == SnapReferenceType::Face;
    });
    assert(edge != references.end() && face != references.end());
    SnapReference sourceVertex{
        QStringLiteral("moving"), QStringLiteral("vertex:1"),
        SnapReferenceType::Vertex, {}, gp_Pnt(5.0, 5.0, 20.0),
        std::nullopt, std::nullopt, QPointF(0.0, 0.0)};
    auto edgeTarget = *edge;
    edgeTarget.screenPoint = QPointF(0.0, 0.0);
    auto edgeCandidate = snapManager.findCandidate(
        {sourceVertex}, {edgeTarget}, std::nullopt);
    assert(edgeCandidate && edgeCandidate->kind == SnapKind::VertexToEdge);
    auto faceTarget = *face;
    faceTarget.screenPoint = QPointF(0.0, 0.0);
    auto faceCandidate = snapManager.findCandidate(
        {sourceVertex}, {faceTarget}, std::nullopt);
    assert(faceCandidate && faceCandidate->kind == SnapKind::VertexToFace);

    auto sourceFace = *face;
    sourceFace.ownerId = QStringLiteral("moving");
    sourceFace.screenPoint = QPointF(0.0, 0.0);
    auto faceToFace = snapManager.findCandidate(
        {sourceFace}, {faceTarget}, std::nullopt);
    assert(faceToFace && faceToFace->kind == SnapKind::FaceToFace);

    const auto cylinder = BRepPrimAPI_MakeCylinder(5.0, 10.0).Shape();
    const auto cylinderReferences = snapManager.collectReferences(
        QStringLiteral("cylinder"), cylinder);
    const auto axis = std::find_if(cylinderReferences.begin(), cylinderReferences.end(),
        [](const auto& ref) { return ref.type == SnapReferenceType::Axis; });
    const auto center = std::find_if(cylinderReferences.begin(), cylinderReferences.end(),
        [](const auto& ref) { return ref.type == SnapReferenceType::CircleCenter; });
    assert(axis != cylinderReferences.end() && center != cylinderReferences.end());
    auto sourceAxis = *axis;
    sourceAxis.ownerId = QStringLiteral("moving");
    sourceAxis.screenPoint = QPointF(0.0, 0.0);
    auto targetAxis = *axis;
    targetAxis.screenPoint = QPointF(0.0, 0.0);
    auto axisCandidate = snapManager.findCandidate(
        {sourceAxis}, {targetAxis}, std::nullopt);
    assert(axisCandidate && axisCandidate->kind == SnapKind::AxisToAxis);
    auto sourceCenter = *center;
    sourceCenter.ownerId = QStringLiteral("moving");
    sourceCenter.screenPoint = QPointF(0.0, 0.0);
    auto targetCenter = *center;
    targetCenter.screenPoint = QPointF(0.0, 0.0);
    auto centerCandidate = snapManager.findCandidate(
        {sourceCenter}, {targetCenter}, std::nullopt);
    assert(centerCandidate && centerCandidate->kind == SnapKind::CenterToCenter);

    return 0;
}
