#pragma once

#include <QPointF>
#include <QString>
#include <gp_Pnt.hxx>

#include <optional>
#include <vector>

namespace cad::viewer {

enum class SnapKind
{
    VertexToVertex,
    VertexToEdge,
    VertexToFace,
    FaceToFace,
    CenterToCenter,
    AxisToAxis
};

struct SnapTarget
{
    QString id;
    SnapKind kind{SnapKind::VertexToVertex};
    gp_Pnt point;
    QPointF screenPoint;
};

struct SnapCandidate
{
    QString id;
    SnapKind kind{SnapKind::VertexToVertex};
    gp_Pnt targetPoint;
    double screenDistance{0.0};
};

class SnapManager final
{
public:
    std::optional<SnapCandidate> findCandidate(
        const QPointF& sourceScreenPoint,
        const std::vector<SnapTarget>& targets,
        const std::optional<SnapCandidate>& active
    ) const;

    static constexpr double ActivationTolerancePixels = 10.0;
    static constexpr double HysteresisMultiplier = 1.6;
};

} // namespace cad::viewer
