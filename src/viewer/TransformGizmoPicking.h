#pragma once

#include "viewer/TransformGizmo.h"

#include <QPointF>

#include <optional>
#include <vector>

namespace cad::viewer {

enum class ScreenHandleGeometryKind
{
    Segment,
    Polyline,
    Circle,
    Point
};

struct ScreenHandleGeometry
{
    TransformHandle handle{TransformHandle::None};
    ScreenHandleGeometryKind kind{ScreenHandleGeometryKind::Point};
    std::vector<QPointF> points;
    QPointF center;
    double radius{0.0};
    double tolerance{0.0};
};

double pointToProjectedSegmentDistance(
    const QPointF& point,
    const QPointF& first,
    const QPointF& second
);

double pointToProjectedCircleDistance(
    const QPointF& point,
    const QPointF& center,
    double radius
);

std::optional<TransformHandle> pickClosestHandle(
    const QPointF& point,
    const std::vector<ScreenHandleGeometry>& geometries
);

} // namespace cad::viewer
