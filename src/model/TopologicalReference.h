#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <QJsonObject>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cad::parametric {
class Body;
}

namespace cad::topology {

enum class TopologicalKind { Face, Edge, Vertex };
enum class SurfaceKind { Plane, Cylinder, Cone, Sphere, Torus, Other };
enum class CurveKind { Line, Circle, Ellipse, Other };

struct FaceSignature {
    SurfaceKind surfaceKind{SurfaceKind::Other};
    gp_Pnt centroid;
    std::optional<gp_Dir> normal;
    double area{0.0};
    std::optional<double> radius;
};

struct EdgeSignature {
    CurveKind curveKind{CurveKind::Other};
    gp_Pnt midpoint;
    double length{0.0};
    gp_Pnt firstPoint;
    gp_Pnt lastPoint;
    std::optional<gp_Dir> direction;
    std::optional<double> radius;
};

struct VertexSignature {
    gp_Pnt point;
};

using TopologicalSignature = std::variant<FaceSignature, EdgeSignature, VertexSignature>;

struct TopologicalReference {
    std::string featureId;
    TopologicalKind kind{TopologicalKind::Edge};
    std::optional<int> transientIndex;
    std::optional<TopologicalSignature> signature;

    bool isLegacy() const noexcept { return !signature.has_value(); }
};

struct TopologicalMatchTolerance {
    double linear{1.0e-6};
    double angular{1.0e-6};
    double relativeLength{1.0e-6};
    double relativeArea{1.0e-6};
    double radius{1.0e-6};
};

enum class ResolveStatus {
    Resolved,
    Missing,
    Ambiguous,
    FeatureMissing,
    TypeMismatch
};

struct TopologicalResolveResult {
    ResolveStatus status{ResolveStatus::Missing};
    std::optional<TopoDS_Shape> shape;
    std::optional<int> resolvedIndex;
    std::string error;
};

class TopologicalSignatureBuilder final
{
public:
    static TopologicalReference createReference(
        const std::string& featureId,
        const TopoDS_Shape& ownerShape,
        const TopoDS_Shape& subshape
    );
};

class TopologicalReferenceResolver final
{
public:
    explicit TopologicalReferenceResolver(
        const cad::parametric::Body& body,
        TopologicalMatchTolerance tolerance = {}
    ) noexcept;

    TopologicalResolveResult resolve(const TopologicalReference& reference) const;
    TopologicalResolveResult resolve(
        const TopologicalReference& reference,
        const TopoDS_Shape& currentShape
    ) const;

    static TopologicalResolveResult resolveAgainstShape(
        const TopologicalReference& reference,
        const TopoDS_Shape& currentShape,
        TopologicalMatchTolerance tolerance = {}
    );

private:
    const cad::parametric::Body& body_;
    TopologicalMatchTolerance tolerance_;
};

QJsonObject toJson(const TopologicalReference& reference);
TopologicalReference topologicalReferenceFromJson(const QJsonObject& object);

} // namespace cad::topology
