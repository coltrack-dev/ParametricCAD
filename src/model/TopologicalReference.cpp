#include "model/TopologicalReference.h"

#include "model/Body.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cad::topology {

namespace {

QJsonArray pointJson(const gp_Pnt& point)
{
    return {point.X(), point.Y(), point.Z()};
}

QJsonArray directionJson(const gp_Dir& direction)
{
    return {direction.X(), direction.Y(), direction.Z()};
}

gp_Dir directionFromJson(const QJsonValue& value)
{
    const auto values = value.toArray();
    if (values.size() != 3) throw std::invalid_argument("Invalid topological direction");
    return {values.at(0).toDouble(), values.at(1).toDouble(), values.at(2).toDouble()};
}

gp_Pnt pointFromJson(const QJsonValue& value)
{
    const auto values = value.toArray();
    if (values.size() != 3) throw std::invalid_argument("Invalid topological point");
    return {values.at(0).toDouble(), values.at(1).toDouble(), values.at(2).toDouble()};
}

std::string enumName(const SurfaceKind kind)
{
    switch (kind) {
    case SurfaceKind::Plane: return "Plane";
    case SurfaceKind::Cylinder: return "Cylinder";
    case SurfaceKind::Cone: return "Cone";
    case SurfaceKind::Sphere: return "Sphere";
    case SurfaceKind::Torus: return "Torus";
    case SurfaceKind::Other: return "Other";
    }
    return "Other";
}

SurfaceKind surfaceKind(const GeomAbs_SurfaceType type)
{
    switch (type) {
    case GeomAbs_Plane: return SurfaceKind::Plane;
    case GeomAbs_Cylinder: return SurfaceKind::Cylinder;
    case GeomAbs_Cone: return SurfaceKind::Cone;
    case GeomAbs_Sphere: return SurfaceKind::Sphere;
    case GeomAbs_Torus: return SurfaceKind::Torus;
    default: return SurfaceKind::Other;
    }
}

SurfaceKind surfaceKindFromJson(const QString& value)
{
    if (value == "Plane") return SurfaceKind::Plane;
    if (value == "Cylinder") return SurfaceKind::Cylinder;
    if (value == "Cone") return SurfaceKind::Cone;
    if (value == "Sphere") return SurfaceKind::Sphere;
    if (value == "Torus") return SurfaceKind::Torus;
    if (value == "Other") return SurfaceKind::Other;
    throw std::invalid_argument("Invalid topological surface kind");
}

std::string enumName(const CurveKind kind)
{
    switch (kind) {
    case CurveKind::Line: return "Line";
    case CurveKind::Circle: return "Circle";
    case CurveKind::Ellipse: return "Ellipse";
    case CurveKind::Other: return "Other";
    }
    return "Other";
}

CurveKind curveKind(const GeomAbs_CurveType type)
{
    switch (type) {
    case GeomAbs_Line: return CurveKind::Line;
    case GeomAbs_Circle: return CurveKind::Circle;
    case GeomAbs_Ellipse: return CurveKind::Ellipse;
    default: return CurveKind::Other;
    }
}

CurveKind curveKindFromJson(const QString& value)
{
    if (value == "Line") return CurveKind::Line;
    if (value == "Circle") return CurveKind::Circle;
    if (value == "Ellipse") return CurveKind::Ellipse;
    if (value == "Other") return CurveKind::Other;
    throw std::invalid_argument("Invalid topological curve kind");
}

std::string kindName(const TopologicalKind kind)
{
    switch (kind) {
    case TopologicalKind::Face: return "Face";
    case TopologicalKind::Edge: return "Edge";
    case TopologicalKind::Vertex: return "Vertex";
    }
    return "";
}

TopologicalKind kindFromJson(const QString& value)
{
    if (value == "Face") return TopologicalKind::Face;
    if (value == "Edge") return TopologicalKind::Edge;
    if (value == "Vertex") return TopologicalKind::Vertex;
    throw std::invalid_argument("Invalid topological kind");
}

TopAbs_ShapeEnum shapeType(const TopologicalKind kind)
{
    switch (kind) {
    case TopologicalKind::Face: return TopAbs_FACE;
    case TopologicalKind::Edge: return TopAbs_EDGE;
    case TopologicalKind::Vertex: return TopAbs_VERTEX;
    }
    return TopAbs_SHAPE;
}

bool close(const double a, const double b, const double tolerance)
{
    return std::abs(a - b) <= tolerance;
}

bool closeRelative(const double a, const double b, const double tolerance)
{
    return close(a, b, tolerance * std::max({1.0, std::abs(a), std::abs(b)}));
}

bool closePoint(const gp_Pnt& a, const gp_Pnt& b, const TopologicalMatchTolerance& tolerance)
{
    return a.Distance(b) <= tolerance.linear;
}

bool closeDirection(const std::optional<gp_Dir>& a, const std::optional<gp_Dir>& b,
                    const TopologicalMatchTolerance& tolerance)
{
    if (!a || !b) return !a && !b;
    return std::abs(a->Dot(*b)) >= std::cos(tolerance.angular);
}

bool closeOrientedDirection(const std::optional<gp_Dir>& a,
                            const std::optional<gp_Dir>& b,
                            const TopologicalMatchTolerance& tolerance)
{
    if (!a || !b) return !a && !b;
    return a->Dot(*b) >= std::cos(tolerance.angular);
}

FaceSignature makeFaceSignature(const TopoDS_Face& face)
{
    BRepAdaptor_Surface surface(face, Standard_True);
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(face, properties);
    FaceSignature result;
    result.surfaceKind = surfaceKind(surface.GetType());
    result.centroid = properties.CentreOfMass();
    result.area = properties.Mass();
    if (result.surfaceKind == SurfaceKind::Plane) {
        gp_Dir normal = surface.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
        result.normal = normal;
    } else if (result.surfaceKind == SurfaceKind::Cylinder) {
        result.radius = surface.Cylinder().Radius();
        result.normal = surface.Cylinder().Axis().Direction();
    } else if (result.surfaceKind == SurfaceKind::Sphere) {
        result.radius = surface.Sphere().Radius();
    } else if (result.surfaceKind == SurfaceKind::Torus) {
        result.radius = surface.Torus().MajorRadius();
    }
    return result;
}

EdgeSignature makeEdgeSignature(const TopoDS_Edge& edge)
{
    Standard_Real first = 0.0;
    Standard_Real last = 0.0;
    const auto curve = BRep_Tool::Curve(edge, first, last);
    if (curve.IsNull()) throw std::runtime_error("Edge has no curve");
    BRepAdaptor_Curve adaptor(edge);
    EdgeSignature result;
    result.curveKind = curveKind(adaptor.GetType());
    result.firstPoint = adaptor.Value(first);
    result.lastPoint = adaptor.Value(last);
    result.midpoint = adaptor.Value(first + (last - first) * 0.5);
    GProp_GProps properties;
    BRepGProp::LinearProperties(edge, properties);
    result.length = properties.Mass();
    if (result.curveKind == CurveKind::Line) {
        result.direction = adaptor.Line().Direction();
    } else if (result.curveKind == CurveKind::Circle) {
        result.radius = adaptor.Circle().Radius();
    } else if (result.curveKind == CurveKind::Ellipse) {
        result.radius = adaptor.Ellipse().MajorRadius();
    }
    return result;
}

TopologicalSignature makeSignature(const TopoDS_Shape& shape)
{
    switch (shape.ShapeType()) {
    case TopAbs_FACE: return makeFaceSignature(TopoDS::Face(shape));
    case TopAbs_EDGE: return makeEdgeSignature(TopoDS::Edge(shape));
    case TopAbs_VERTEX: return VertexSignature{BRep_Tool::Pnt(TopoDS::Vertex(shape))};
    default: throw std::invalid_argument("Unsupported topological shape type");
    }
}

bool faceMatches(const FaceSignature& reference, const FaceSignature& candidate,
                 const TopologicalMatchTolerance& tolerance)
{
    if (reference.surfaceKind != candidate.surfaceKind
        || !closeOrientedDirection(reference.normal, candidate.normal, tolerance)) return false;
    if (reference.surfaceKind == SurfaceKind::Plane) return true;
    return closePoint(reference.centroid, candidate.centroid, tolerance)
        && closeRelative(reference.area, candidate.area, tolerance.relativeArea)
        && (!reference.radius || (candidate.radius
            && closeRelative(*reference.radius, *candidate.radius, tolerance.radius)));
}

bool facePlaneMatchesIgnoringOrientation(const FaceSignature& reference,
                                         const FaceSignature& candidate,
                                         const TopologicalMatchTolerance& tolerance)
{
    return reference.surfaceKind == SurfaceKind::Plane
        && candidate.surfaceKind == SurfaceKind::Plane
        && closeDirection(reference.normal, candidate.normal, tolerance);
}

bool edgeMatches(const EdgeSignature& reference, const EdgeSignature& candidate,
                 const TopologicalMatchTolerance& tolerance)
{
    const bool endpoints = (closePoint(reference.firstPoint, candidate.firstPoint, tolerance)
        && closePoint(reference.lastPoint, candidate.lastPoint, tolerance))
        || (closePoint(reference.firstPoint, candidate.lastPoint, tolerance)
        && closePoint(reference.lastPoint, candidate.firstPoint, tolerance));
    return reference.curveKind == candidate.curveKind
        && endpoints
        && closePoint(reference.midpoint, candidate.midpoint, tolerance)
        && closeRelative(reference.length, candidate.length, tolerance.relativeLength)
        && closeDirection(reference.direction, candidate.direction, tolerance)
        && (!reference.radius || (candidate.radius
            && closeRelative(*reference.radius, *candidate.radius, tolerance.radius)));
}

bool signatureMatches(const TopologicalSignature& reference,
                      const TopologicalSignature& candidate,
                      const TopologicalMatchTolerance& tolerance)
{
    if (reference.index() != candidate.index()) return false;
    if (const auto* a = std::get_if<FaceSignature>(&reference))
        return faceMatches(*a, std::get<FaceSignature>(candidate), tolerance);
    if (const auto* a = std::get_if<EdgeSignature>(&reference))
        return edgeMatches(*a, std::get<EdgeSignature>(candidate), tolerance);
    return closePoint(std::get<VertexSignature>(reference).point,
                      std::get<VertexSignature>(candidate).point, tolerance);
}

TopologicalSignature signatureFromJson(const QJsonObject& object, const TopologicalKind kind)
{
    if (kind == TopologicalKind::Vertex)
        return VertexSignature{pointFromJson(object.value("point"))};
    if (kind == TopologicalKind::Edge) {
        EdgeSignature result;
        result.curveKind = curveKindFromJson(object.value("curveKind").toString());
        result.midpoint = pointFromJson(object.value("midpoint"));
        result.firstPoint = pointFromJson(object.value("firstPoint"));
        result.lastPoint = pointFromJson(object.value("lastPoint"));
        result.length = object.value("length").toDouble();
        if (object.contains("direction"))
            result.direction = directionFromJson(object.value("direction"));
        if (object.contains("radius")) result.radius = object.value("radius").toDouble();
        return result;
    }
    FaceSignature result;
    result.surfaceKind = surfaceKindFromJson(object.value("surfaceKind").toString());
    result.centroid = pointFromJson(object.value("centroid"));
    result.area = object.value("area").toDouble();
    if (object.contains("normal")) result.normal = directionFromJson(object.value("normal"));
    if (object.contains("radius")) result.radius = object.value("radius").toDouble();
    return result;
}

} // namespace

TopologicalReference TopologicalSignatureBuilder::createReference(
    const std::string& featureId, const TopoDS_Shape& ownerShape, const TopoDS_Shape& subshape)
{
    if (featureId.empty() || ownerShape.IsNull() || subshape.IsNull())
        throw std::invalid_argument("Cannot create a topological reference from a null shape");
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(ownerShape, subshape.ShapeType(), map);
    const int index = map.FindIndex(subshape);
    if (index <= 0) throw std::invalid_argument("Subshape does not belong to owner shape");

    TopologicalReference result;
    result.featureId = featureId;
    result.transientIndex = index;
    switch (subshape.ShapeType()) {
    case TopAbs_FACE: result.kind = TopologicalKind::Face; break;
    case TopAbs_EDGE: result.kind = TopologicalKind::Edge; break;
    case TopAbs_VERTEX: result.kind = TopologicalKind::Vertex; break;
    default: throw std::invalid_argument("Unsupported topological reference shape type");
    }
    result.signature = makeSignature(subshape);
    return result;
}

TopologicalReferenceResolver::TopologicalReferenceResolver(
    const cad::parametric::Body& body, TopologicalMatchTolerance tolerance) noexcept
    : body_(body), tolerance_(tolerance)
{
}

TopologicalResolveResult TopologicalReferenceResolver::resolveAgainstShape(
    const TopologicalReference& reference, const TopoDS_Shape& currentShape,
    const TopologicalMatchTolerance tolerance)
{
    if (currentShape.IsNull())
        return {ResolveStatus::FeatureMissing, std::nullopt, std::nullopt,
                "Current feature shape is missing"};
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(currentShape, shapeType(reference.kind), map);
    const auto candidateAt = [&](const int index) -> std::optional<TopoDS_Shape> {
        if (index <= 0 || index > map.Extent()) return std::nullopt;
        return map(index);
    };

    if (reference.transientIndex) {
        const auto candidate = candidateAt(*reference.transientIndex);
        if (candidate && (!reference.signature
            || signatureMatches(*reference.signature, makeSignature(*candidate), tolerance))) {
            return {ResolveStatus::Resolved, candidate, reference.transientIndex, {}};
        }
    }

    if (!reference.signature)
        return {ResolveStatus::Missing, std::nullopt, std::nullopt,
                "Legacy topological index is no longer valid"};

    std::vector<std::pair<int, TopoDS_Shape>> matches;
    for (int index = 1; index <= map.Extent(); ++index) {
        const auto candidate = map(index);
        if (signatureMatches(*reference.signature, makeSignature(candidate), tolerance))
            matches.emplace_back(index, candidate);
    }
    if (matches.empty()) {
        if (const auto* face = std::get_if<FaceSignature>(&*reference.signature)) {
            for (int index = 1; index <= map.Extent(); ++index) {
                const auto candidate = map(index);
                const auto candidateSignature = makeSignature(candidate);
                if (const auto* candidateFace = std::get_if<FaceSignature>(&candidateSignature);
                    candidateFace && facePlaneMatchesIgnoringOrientation(
                        *face, *candidateFace, tolerance)) {
                    matches.emplace_back(index, candidate);
                }
            }
        }
    }
    if (matches.empty())
        return {ResolveStatus::Missing, std::nullopt, std::nullopt,
                "Referenced topology is missing"};
    if (matches.size() != 1)
        return {ResolveStatus::Ambiguous, std::nullopt, std::nullopt,
                "Referenced topology is ambiguous"};
    return {ResolveStatus::Resolved, matches.front().second, matches.front().first, {}};
}

TopologicalResolveResult TopologicalReferenceResolver::resolve(
    const TopologicalReference& reference) const
{
    const auto feature = body_.findFeature(reference.featureId);
    if (!feature || feature->shape().IsNull())
        return {ResolveStatus::FeatureMissing, std::nullopt, std::nullopt,
                "Referenced feature is missing"};
    return resolveAgainstShape(reference, feature->shape(), tolerance_);
}

TopologicalResolveResult TopologicalReferenceResolver::resolve(
    const TopologicalReference& reference, const TopoDS_Shape& currentShape) const
{
    return resolveAgainstShape(reference, currentShape, tolerance_);
}

QJsonObject toJson(const TopologicalReference& reference)
{
    QJsonObject object{{"featureId", QString::fromStdString(reference.featureId)},
                       {"kind", QString::fromStdString(kindName(reference.kind))}};
    if (reference.transientIndex) object.insert("transientIndex", *reference.transientIndex);
    if (!reference.signature) return object;
    QJsonObject signature;
    if (const auto* face = std::get_if<FaceSignature>(&*reference.signature)) {
        signature.insert("surfaceKind", QString::fromStdString(enumName(face->surfaceKind)));
        signature.insert("centroid", pointJson(face->centroid));
        signature.insert("area", face->area);
        if (face->normal) signature.insert("normal", directionJson(*face->normal));
        if (face->radius) signature.insert("radius", *face->radius);
    } else if (const auto* edge = std::get_if<EdgeSignature>(&*reference.signature)) {
        signature.insert("curveKind", QString::fromStdString(enumName(edge->curveKind)));
        signature.insert("midpoint", pointJson(edge->midpoint));
        signature.insert("length", edge->length);
        signature.insert("firstPoint", pointJson(edge->firstPoint));
        signature.insert("lastPoint", pointJson(edge->lastPoint));
        if (edge->direction) signature.insert("direction", directionJson(*edge->direction));
        if (edge->radius) signature.insert("radius", *edge->radius);
    } else {
        signature.insert("point", pointJson(std::get<VertexSignature>(*reference.signature).point));
    }
    object.insert("signature", signature);
    return object;
}

TopologicalReference topologicalReferenceFromJson(const QJsonObject& object)
{
    if (!object.value("featureId").isString() || !object.value("kind").isString())
        throw std::invalid_argument("Invalid topological reference");
    TopologicalReference result;
    result.featureId = object.value("featureId").toString().toStdString();
    result.kind = kindFromJson(object.value("kind").toString());
    if (object.contains("transientIndex")) result.transientIndex = object.value("transientIndex").toInt();
    if (object.contains("signature"))
        result.signature = signatureFromJson(object.value("signature").toObject(), result.kind);
    return result;
}

} // namespace cad::topology
