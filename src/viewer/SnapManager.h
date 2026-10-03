#pragma once

#include <QPointF>
#include <QString>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <optional>
#include <functional>
#include <memory>
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

enum class SnapReferenceType
{
    Vertex,
    Edge,
    Face,
    CircleCenter,
    Axis
};

struct SnapReference
{
    QString ownerId;
    QString subshapeId;
    SnapReferenceType type{SnapReferenceType::Vertex};
    TopoDS_Shape shape;
    gp_Pnt point;
    std::optional<gp_Ax3> frame;
    std::optional<gp_Ax1> axis;
    QPointF screenPoint;
    // Shared geometry is used by candidate pairs so a large pattern does not
    // copy a TopoDS_Shape into every source/target combination.
    std::shared_ptr<const TopoDS_Shape> geometry;
};

struct SnapCandidate
{
    SnapReference source;
    SnapReference target;
    SnapKind kind{SnapKind::VertexToVertex};
    gp_Pnt targetPoint;
    gp_Trsf correction;
    double screenDistance{0.0};
    int specificity{0};
    std::size_t sourceIndex{0};

    QString id() const
    {
        return source.ownerId + ':' + source.subshapeId + "->"
            + target.ownerId + ':' + target.subshapeId;
    }
};

class SnapManager final
{
public:
    static std::vector<SnapReference> collectReferences(
        const QString& ownerId,
        const TopoDS_Shape& shape
    );

    static std::vector<SnapReference> transformReferences(
        const std::vector<SnapReference>& references,
        const gp_Trsf& transform
    );

    std::vector<SnapCandidate> buildCandidates(
        const std::vector<SnapReference>& sources,
        const std::vector<SnapReference>& targets
    ) const;

    std::optional<SnapCandidate> findCandidate(
        const std::vector<SnapCandidate>& candidates,
        const gp_Trsf& previewTransform,
        const std::function<QPointF(const gp_Pnt&)>& project,
        const std::optional<SnapCandidate>& active
    ) const;

    std::optional<SnapCandidate> findCandidate(
        const std::vector<SnapReference>& sources,
        const std::vector<SnapReference>& targets,
        const std::optional<SnapCandidate>& active,
        const std::function<QPointF(const gp_Pnt&)>& project = {}
    ) const;

    static constexpr double ActivationTolerancePixels = 10.0;
    static constexpr double HysteresisMultiplier = 1.6;
};

} // namespace cad::viewer
