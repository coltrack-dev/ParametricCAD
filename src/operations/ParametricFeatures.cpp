#include "operations/ParametricFeatures.h"

#include "operations/BasicFeatures.h"
#include "operations/SketchProfileBuilder.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopAbs_ShapeEnum.hxx>

#include <gp_Pnt.hxx>
#include <gp_Circ.hxx>
#include <gp_Vec.hxx>
#include <QJsonArray>
#include <QUuid>
#include <Standard_Failure.hxx>

#include <array>
#include <algorithm>
#include <cmath>

#include <stdexcept>
#include <string>
#include <utility>

namespace cad::parametric {

namespace {

constexpr double sketchTwoPi = 6.283185307179586476925286766559;

SketchEntityId entityId(const SketchEntity& entity)
{
    return std::visit([](const auto& value) { return value.id; }, entity);
}

void ensureEntityId(SketchEntity& entity)
{
    if (!entityId(entity).empty()) return;
    const auto id = "entity-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    std::visit([&id](auto& value) { value.id = id; }, entity);
}

void ensureConstraintId(SketchConstraint& constraint)
{
    const auto makeId = [] {
        return "constraint-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    };
    std::visit([&](auto& value) {
        if (value.id.empty()) value.id = makeId();
    }, constraint);
}

std::string pointRoleName(const SketchPointRole role)
{
    switch (role) {
    case SketchPointRole::LineStart: return "LineStart";
    case SketchPointRole::LineEnd: return "LineEnd";
    case SketchPointRole::ArcStart: return "ArcStart";
    case SketchPointRole::ArcEnd: return "ArcEnd";
    case SketchPointRole::CircleCenter: return "CircleCenter";
    case SketchPointRole::ArcCenter: return "ArcCenter";
    }
    return "LineStart";
}

double normalizedArcSweep(const double start, const double end, const bool clockwise)
{
    double delta = std::fmod(end - start, sketchTwoPi);
    if (clockwise) {
        while (delta >= 0.0) delta -= sketchTwoPi;
    } else {
        while (delta <= 0.0) delta += sketchTwoPi;
    }
    return delta;
}

void requireFeature(
    const ParametricFeature::Ptr& feature,
    const char* parameterName
)
{
    if (!feature) {
        throw std::invalid_argument(
            std::string(parameterName)
            + " feature must not be null"
        );
    }
}

FeatureProperty numericProperty(
    const char* key,
    const char* label,
    const double value
)
{
    return {key, label, value, 0.001, 1'000'000.0, true};
}

FeatureProperty textProperty(
    const char* key,
    const char* label,
    std::string value
)
{
    return {key, label, std::move(value), std::nullopt, std::nullopt, false};
}

SketchFrame globalSketchFrame(const SketchSupportType support)
{
    switch (support) {
    case SketchSupportType::XY:
        return {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    case SketchSupportType::XZ:
        return {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}};
    case SketchSupportType::YZ:
        return {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}};
    case SketchSupportType::Face:
        break;
    }
    throw std::invalid_argument("Face support requires a planar face");
}

gp_Pnt worldPoint(const SketchFrame& frame, const gp_Pnt2d& point)
{
    gp_Pnt result = frame.origin;
    result.Translate(gp_Vec(frame.xDirection) * point.X()
        + gp_Vec(frame.yDirection) * point.Y());
    return result;
}

TopoDS_Edge makeArcEdge(const SketchFrame& frame, const SketchArc& arc)
{
    const gp_Circ circle(gp_Ax2(worldPoint(frame, arc.center), frame.normal), arc.radius);
    const double sweep = arc.signedSweep();
    if (sweep > 0.0) {
        return BRepBuilderAPI_MakeEdge(
            circle, arc.startAngle, arc.startAngle + sweep).Edge();
    }
    return TopoDS::Edge(BRepBuilderAPI_MakeEdge(
        circle, arc.startAngle + sweep, arc.startAngle).Edge().Reversed());
}

TopoDS_Shape compoundOf(const std::vector<TopoDS_Shape>& shapes)
{
    if (shapes.empty()) throw std::runtime_error("Operation produced no shapes");
    if (shapes.size() == 1) return shapes.front();
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (const auto& shape : shapes) {
        if (shape.IsNull()) throw std::runtime_error("Operation produced a null shape");
        builder.Add(compound, shape);
    }
    return compound;
}

void writePoint2d(QJsonArray& array, const gp_Pnt2d& point)
{
    array.append(point.X());
    array.append(point.Y());
}

std::string supportName(const SketchSupportType type)
{
    switch (type) {
    case SketchSupportType::XY: return "XY";
    case SketchSupportType::XZ: return "XZ";
    case SketchSupportType::YZ: return "YZ";
    case SketchSupportType::Face: return "Face";
    }
    return "XY";
}

std::vector<cad::topology::TopologicalReference> referencesFor(
    const ParametricFeature::Ptr& base,
    const std::vector<TopoDS_Edge>& edges
)
{
    requireFeature(base, "edge operation base");
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(base->shape(), TopAbs_EDGE, map);
    std::vector<cad::topology::TopologicalReference> result;
    result.reserve(edges.size());
    for (const auto& edge : edges) {
        if (edge.IsNull()) throw std::invalid_argument("Edge must not be null");
        if (map.FindIndex(edge) <= 0)
            throw std::invalid_argument("Selected edge does not belong to the source feature");
        result.push_back(cad::topology::TopologicalSignatureBuilder::createReference(
            base->id(), base->shape(), edge));
    }
    if (result.empty()) throw std::invalid_argument(
        "Edge operation requires at least one edge");
    return result;
}

std::vector<cad::topology::TopologicalReference> faceReferencesFor(
    const ParametricFeature::Ptr& base,
    const std::vector<TopoDS_Face>& faces)
{
    requireFeature(base, "face operation base");
    std::vector<cad::topology::TopologicalReference> result;
    result.reserve(faces.size());
    for (const auto& face : faces) {
        if (face.IsNull()) throw std::invalid_argument("Face must not be null");
        result.push_back(cad::topology::TopologicalSignatureBuilder::createReference(
            base->id(), base->shape(), face));
    }
    if (result.empty()) throw std::invalid_argument("Face operation requires at least one face");
    return result;
}

void validateFaceReferences(
    const std::vector<cad::topology::TopologicalReference>& references,
    const std::string& featureId)
{
    if (references.empty()) throw std::invalid_argument("Face operation requires at least one face reference");
    for (const auto& reference : references) {
        if (reference.featureId != featureId
            || reference.kind != cad::topology::TopologicalKind::Face)
            throw std::invalid_argument("Invalid face topological reference");
    }
}

std::vector<TopoDS_Face> resolveFaces(
    const ParametricFeature::Ptr& base,
    const std::vector<cad::topology::TopologicalReference>& references)
{
    requireFeature(base, "face operation base");
    std::vector<TopoDS_Face> result;
    result.reserve(references.size());
    for (const auto& reference : references) {
        const auto resolved = cad::topology::TopologicalReferenceResolver::resolveAgainstShape(
            reference, base->shape());
        if (resolved.status != cad::topology::ResolveStatus::Resolved || !resolved.shape)
            throw std::runtime_error(resolved.error.empty()
                ? "Stored face reference could not be resolved" : resolved.error);
        if (resolved.shape->ShapeType() != TopAbs_FACE)
            throw std::runtime_error("Stored face reference resolved to a non-face");
        result.push_back(TopoDS::Face(*resolved.shape));
    }
    return result;
}

std::vector<TopoDS_Edge> resolveEdges(
    const ParametricFeature::Ptr& base,
    const std::vector<cad::topology::TopologicalReference>& references
)
{
    requireFeature(base, "edge operation base");
    std::vector<TopoDS_Edge> result;
    result.reserve(references.size());
    for (const auto& reference : references) {
        const auto resolved = cad::topology::TopologicalReferenceResolver::resolveAgainstShape(
            reference, base->shape());
        if (resolved.status != cad::topology::ResolveStatus::Resolved || !resolved.shape)
            throw std::runtime_error(resolved.error.empty()
                ? "Stored edge reference could not be resolved" : resolved.error);
        if (resolved.shape->ShapeType() != TopAbs_EDGE)
            throw std::runtime_error("Stored edge reference resolved to a non-edge");
        result.push_back(TopoDS::Edge(*resolved.shape));
    }
    if (result.empty()) throw std::runtime_error(
        "Edge operation has no stored edge references");
    return result;
}

void validateReferences(const std::vector<cad::topology::TopologicalReference>& references,
                        const std::string& featureId)
{
    if (references.empty()) {
        throw std::invalid_argument(
            "Edge operation requires at least one edge reference");
    }
    for (const auto& reference : references) {
        if (reference.featureId != featureId || reference.kind != cad::topology::TopologicalKind::Edge
            || !reference.transientIndex || *reference.transientIndex <= 0) {
            throw std::invalid_argument(
                "Invalid edge topological reference");
        }
    }
}

void writeReferences(QJsonObject& object,
                     const std::vector<cad::topology::TopologicalReference>& references)
{
    QJsonArray values;
    for (const auto& reference : references) values.append(cad::topology::toJson(reference));
    object.insert("topologicalReferences", values);
}

TopoDS_Wire wireFromFeature(const ParametricFeature::Ptr& feature)
{
    requireFeature(feature, "wire source");
    TopTools_IndexedMapOfShape wires;
    TopExp::MapShapes(feature->shape(), TopAbs_WIRE, wires);
    if (wires.Extent() == 0) {
        throw std::runtime_error("Feature does not contain a wire");
    }
    return TopoDS::Wire(wires.FindKey(1));
}

std::vector<cad::topology::TopologicalReference> legacyReferences(
    const std::string& featureId, const std::vector<int>& indices)
{
    std::vector<cad::topology::TopologicalReference> result;
    result.reserve(indices.size());
    for (const int index : indices) {
        result.push_back({featureId, cad::topology::TopologicalKind::Edge, index, std::nullopt,
                          std::nullopt, std::nullopt});
    }
    return result;
}

void upgradeLegacyReferences(
    const ParametricFeature::Ptr& base,
    std::vector<cad::topology::TopologicalReference>& references)
{
    if (!base || base->shape().IsNull()) return;
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(base->shape(), TopAbs_EDGE, edges);
    for (auto& reference : references) {
        if (reference.signature || !reference.transientIndex
            || *reference.transientIndex <= 0 || *reference.transientIndex > edges.Extent()) continue;
        reference = cad::topology::TopologicalSignatureBuilder::createReference(
            base->id(), base->shape(), edges(*reference.transientIndex));
    }
}

} // namespace

gp_Pnt2d SketchArc::startPoint() const
{
    return {center.X() + radius * std::cos(startAngle),
        center.Y() + radius * std::sin(startAngle)};
}

gp_Pnt2d SketchArc::endPoint() const
{
    return {center.X() + radius * std::cos(endAngle),
        center.Y() + radius * std::sin(endAngle)};
}

double SketchArc::signedSweep() const
{
    return normalizedArcSweep(startAngle, endAngle, clockwise);
}

SketchFeature::SketchFeature(std::string id, double width, double height)
    : SketchFeature(std::move(id), SketchSupportType::XY, width, height)
{
}

SketchFeature::SketchFeature(
    std::string id, const SketchSupportType support, const double width, const double height,
    std::vector<SketchEntity> entities)
    : ParametricFeature(std::move(id), "Rectangle Sketch"),
      width_(width), height_(height), supportType_(support), frame_(globalSketchFrame(support)),
      entities_(std::move(entities))
{
    if (support == SketchSupportType::Face)
        throw std::invalid_argument("Global Sketch constructor cannot use Face support");
    for (auto& entity : entities_) ensureEntityId(entity);
}

SketchFeature::SketchFeature(
    std::string id,
    const Ptr& supportSource,
    cad::topology::TopologicalReference faceReference,
    std::vector<SketchEntity> entities
)
    : ParametricFeature(std::move(id), "Sketch on Face"),
      width_(0.0), height_(0.0), supportType_(SketchSupportType::Face),
      supportSource_(supportSource), faceReference_(std::move(faceReference)),
      entities_(std::move(entities))
{
    requireFeature(supportSource, "Sketch support");
    if (faceReference_->featureId != supportSource->id()
        || faceReference_->kind != cad::topology::TopologicalKind::Face) {
        throw std::invalid_argument("Sketch support reference must identify a Face of the source");
    }
    if (!supportSource->shape().IsNull()) {
        const auto resolved = cad::topology::TopologicalReferenceResolver::resolveAgainstShape(
            *faceReference_, supportSource->shape());
        if (resolved.status != cad::topology::ResolveStatus::Resolved || !resolved.shape
            || resolved.shape->ShapeType() != TopAbs_FACE) {
            throw std::invalid_argument("Sketch support face could not be resolved: " + resolved.error);
        }
        frame_ = frameForFace(TopoDS::Face(*resolved.shape));
    } else {
        frame_ = globalSketchFrame(SketchSupportType::XY);
    }
    addDependency(supportSource);
    for (auto& entity : entities_) ensureEntityId(entity);
}

std::vector<FeatureProperty> SketchFeature::properties() const
{
    if (supportType_ == SketchSupportType::Face) {
        return {textProperty("supportType", "Support", "Face"),
                textProperty("supportFeatureId", "Support feature",
                    supportSource_.lock() ? supportSource_.lock()->id() : ""),
                textProperty("entityCount", "Entity count", std::to_string(entities_.size())),
                textProperty("constraintCount", "Constraint count", std::to_string(constraints_.size()))};
    }
    return {numericProperty("width", "Width", width_),
            numericProperty("height", "Height", height_),
            textProperty("plane", "Plane", supportName(supportType_)),
            textProperty("constraintCount", "Constraint count", std::to_string(constraints_.size()))};
}

bool SketchFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "width") setSize(value, height_);
    else if (key == "height") setSize(width_, value);
    else return false;
    return true;
}

void SketchFeature::writeParameters(QJsonObject& object) const
{
    object.insert("supportType", QString::fromStdString(supportName(supportType_)));
    if (supportType_ != SketchSupportType::Face) {
        object.insert("plane", QString::fromStdString(supportName(supportType_)));
        object.insert("width", width_);
        object.insert("height", height_);
    } else {
        object.insert("supportReference", cad::topology::toJson(*faceReference_));
        object.insert("frame", QJsonObject{
            {"origin", QJsonArray{frame_.origin.X(), frame_.origin.Y(), frame_.origin.Z()}},
            {"xDirection", QJsonArray{frame_.xDirection.X(), frame_.xDirection.Y(), frame_.xDirection.Z()}},
            {"yDirection", QJsonArray{frame_.yDirection.X(), frame_.yDirection.Y(), frame_.yDirection.Z()}},
            {"normal", QJsonArray{frame_.normal.X(), frame_.normal.Y(), frame_.normal.Z()}}});
    }
    QJsonArray entities;
    for (const auto& entity : entities_) {
        QJsonObject value;
        if (const auto* line = std::get_if<SketchLine>(&entity)) {
            value.insert("type", "Line");
            value.insert("id", QString::fromStdString(line->id));
            QJsonArray start;
            writePoint2d(start, line->start);
            QJsonArray end;
            writePoint2d(end, line->end);
            value.insert("start", start);
            value.insert("end", end);
        } else if (const auto* circle = std::get_if<SketchCircle>(&entity)) {
            value.insert("type", "Circle");
            value.insert("id", QString::fromStdString(circle->id));
            QJsonArray center;
            writePoint2d(center, circle->center);
            value.insert("center", center);
            value.insert("radius", circle->radius);
        } else {
            const auto& arc = std::get<SketchArc>(entity);
            value.insert("type", "Arc");
            value.insert("id", QString::fromStdString(arc.id));
            QJsonArray center;
            writePoint2d(center, arc.center);
            value.insert("center", center);
            value.insert("radius", arc.radius);
            value.insert("startAngle", arc.startAngle);
            value.insert("endAngle", arc.endAngle);
            value.insert("clockwise", arc.clockwise);
        }
        entities.append(value);
    }
    object.insert("entities", entities);
    QJsonArray constraints;
    for (const auto& constraint : constraints_) {
        QJsonObject value;
        if (const auto* coincident = std::get_if<CoincidentConstraint>(&constraint)) {
            value.insert("type", "Coincident");
            value.insert("id", QString::fromStdString(coincident->id));
            value.insert("aEntityId", QString::fromStdString(coincident->a.entityId));
            value.insert("aRole", QString::fromStdString(pointRoleName(coincident->a.role)));
            value.insert("bEntityId", QString::fromStdString(coincident->b.entityId));
            value.insert("bRole", QString::fromStdString(pointRoleName(coincident->b.role)));
        } else if (const auto* horizontal = std::get_if<HorizontalConstraint>(&constraint)) {
            value.insert("type", "Horizontal");
            value.insert("id", QString::fromStdString(horizontal->id));
            value.insert("entityId", QString::fromStdString(horizontal->entityId));
            value.insert("anchorStart", horizontal->anchorStart);
        } else if (const auto* vertical = std::get_if<VerticalConstraint>(&constraint)) {
            value.insert("type", "Vertical");
            value.insert("id", QString::fromStdString(vertical->id));
            value.insert("entityId", QString::fromStdString(vertical->entityId));
            value.insert("anchorStart", vertical->anchorStart);
        } else if (const auto* distance = std::get_if<DistanceConstraint>(&constraint)) {
            value.insert("type", "Distance");
            value.insert("id", QString::fromStdString(distance->id));
            value.insert("entityId", QString::fromStdString(distance->entityId));
            value.insert("value", distance->value);
            value.insert("anchorStart", distance->anchorStart);
        } else if (const auto* radius = std::get_if<RadiusConstraint>(&constraint)) {
            value.insert("type", "Radius");
            value.insert("id", QString::fromStdString(radius->id));
            value.insert("entityId", QString::fromStdString(radius->entityId));
            value.insert("value", radius->value);
        } else if (const auto* horizontalDistance = std::get_if<HorizontalDistanceConstraint>(&constraint)) {
            value.insert("type", "HorizontalDistance");
            value.insert("id", QString::fromStdString(horizontalDistance->id));
            value.insert("firstEntityId", QString::fromStdString(horizontalDistance->first.entityId));
            value.insert("firstRole", QString::fromStdString(pointRoleName(horizontalDistance->first.role)));
            value.insert("secondEntityId", QString::fromStdString(horizontalDistance->second.entityId));
            value.insert("secondRole", QString::fromStdString(pointRoleName(horizontalDistance->second.role)));
            value.insert("value", horizontalDistance->value);
        } else if (const auto* verticalDistance = std::get_if<VerticalDistanceConstraint>(&constraint)) {
            value.insert("type", "VerticalDistance");
            value.insert("id", QString::fromStdString(verticalDistance->id));
            value.insert("firstEntityId", QString::fromStdString(verticalDistance->first.entityId));
            value.insert("firstRole", QString::fromStdString(pointRoleName(verticalDistance->first.role)));
            value.insert("secondEntityId", QString::fromStdString(verticalDistance->second.entityId));
            value.insert("secondRole", QString::fromStdString(pointRoleName(verticalDistance->second.role)));
            value.insert("value", verticalDistance->value);
        } else if (const auto* angle = std::get_if<AngleConstraint>(&constraint)) {
            value.insert("type", "Angle");
            value.insert("id", QString::fromStdString(angle->id));
            value.insert("entityId", QString::fromStdString(angle->entityId));
            value.insert("valueRadians", angle->radians);
            value.insert("anchorStart", angle->anchorStart);
        } else if (const auto* parallel = std::get_if<ParallelConstraint>(&constraint)) {
            value.insert("type", "Parallel");
            value.insert("id", QString::fromStdString(parallel->id));
            value.insert("firstLineId", QString::fromStdString(parallel->firstLineId));
            value.insert("secondLineId", QString::fromStdString(parallel->secondLineId));
            value.insert("anchorStart", parallel->anchorStart);
        } else if (const auto* perpendicular = std::get_if<PerpendicularConstraint>(&constraint)) {
            value.insert("type", "Perpendicular");
            value.insert("id", QString::fromStdString(perpendicular->id));
            value.insert("firstLineId", QString::fromStdString(perpendicular->firstLineId));
            value.insert("secondLineId", QString::fromStdString(perpendicular->secondLineId));
            value.insert("anchorStart", perpendicular->anchorStart);
        } else if (const auto* angleBetween = std::get_if<AngleBetweenLinesConstraint>(&constraint)) {
            value.insert("type", "AngleBetweenLines");
            value.insert("id", QString::fromStdString(angleBetween->id));
            value.insert("referenceLineId", QString::fromStdString(angleBetween->referenceLineId));
            value.insert("dependentLineId", QString::fromStdString(angleBetween->dependentLineId));
            value.insert("angleRadians", angleBetween->angleRadians);
            value.insert("anchorStart", angleBetween->anchorStart);
        } else if (const auto* tangent = std::get_if<TangentConstraint>(&constraint)) {
            value.insert("type", "Tangent");
            value.insert("id", QString::fromStdString(tangent->id));
            value.insert("firstEntityId", QString::fromStdString(tangent->firstEntityId));
            value.insert("secondEntityId", QString::fromStdString(tangent->secondEntityId));
        } else if (const auto* equal = std::get_if<EqualConstraint>(&constraint)) {
            value.insert("type", "Equal");
            value.insert("id", QString::fromStdString(equal->id));
            value.insert("referenceEntityId", QString::fromStdString(equal->referenceEntityId));
            value.insert("dependentEntityId", QString::fromStdString(equal->dependentEntityId));
        } else {
            throw std::logic_error("Unsupported Sketch constraint variant");
        }
        constraints.append(value);
    }
    object.insert("constraints", constraints);
}

void SketchFeature::setSize(double width, double height)
{
    width_ = width;
    height_ = height;
    markDirty();
}

double SketchFeature::width() const noexcept { return width_; }
double SketchFeature::height() const noexcept { return height_; }

SketchSupportType SketchFeature::supportType() const noexcept { return supportType_; }

const std::optional<cad::topology::TopologicalReference>&
SketchFeature::faceReference() const noexcept { return faceReference_; }

const SketchFrame& SketchFeature::frame() const noexcept { return frame_; }

SketchFrame SketchFeature::currentFrame() const
{
    if (supportType_ != SketchSupportType::Face) return frame_;
    const auto source = supportSource_.lock();
    if (!source) throw std::runtime_error("Sketch support feature no longer exists");
    const auto resolved = cad::topology::TopologicalReferenceResolver::resolveAgainstShape(
        *faceReference_, source->shape());
    if (resolved.status != cad::topology::ResolveStatus::Resolved || !resolved.shape)
        throw std::runtime_error("Sketch support face could not be resolved: " + resolved.error);
    return frameForFace(TopoDS::Face(*resolved.shape));
}

const std::vector<SketchEntity>& SketchFeature::entities() const noexcept { return entities_; }

const std::vector<SketchConstraint>& SketchFeature::constraints() const noexcept { return constraints_; }
std::size_t SketchFeature::constraintCount() const noexcept { return constraints_.size(); }

void SketchFeature::setConstraints(std::vector<SketchConstraint> constraints)
{
    for (auto& constraint : constraints) ensureConstraintId(constraint);
    constraints_ = std::move(constraints);
    markDirty();
}

void SketchFeature::replaceConstraints(std::vector<SketchConstraint> constraints)
{
    for (auto& constraint : constraints) ensureConstraintId(constraint);
    constraints_ = std::move(constraints);
    markDirty();
}

bool SketchFeature::hasConstraintsForEntity(const SketchEntityId& entityId) const noexcept
{
    for (const auto& constraint : constraints_) {
        if (const auto* coincident = std::get_if<CoincidentConstraint>(&constraint)) {
            if (coincident->a.entityId == entityId || coincident->b.entityId == entityId) return true;
        } else if (const auto* horizontal = std::get_if<HorizontalConstraint>(&constraint)) {
            if (horizontal->entityId == entityId) return true;
        } else if (const auto* vertical = std::get_if<VerticalConstraint>(&constraint)) {
            if (vertical->entityId == entityId) return true;
        } else if (const auto* distance = std::get_if<DistanceConstraint>(&constraint)) {
            if (distance->entityId == entityId) return true;
        } else if (const auto* radius = std::get_if<RadiusConstraint>(&constraint)) {
            if (radius->entityId == entityId) return true;
        } else if (const auto* horizontalDistance = std::get_if<HorizontalDistanceConstraint>(&constraint)) {
            if (horizontalDistance->first.entityId == entityId || horizontalDistance->second.entityId == entityId) return true;
        } else if (const auto* verticalDistance = std::get_if<VerticalDistanceConstraint>(&constraint)) {
            if (verticalDistance->first.entityId == entityId || verticalDistance->second.entityId == entityId) return true;
        } else if (const auto* angle = std::get_if<AngleConstraint>(&constraint)) {
            if (angle->entityId == entityId) return true;
        } else if (const auto* parallel = std::get_if<ParallelConstraint>(&constraint)) {
            if (parallel->firstLineId == entityId || parallel->secondLineId == entityId) return true;
        } else if (const auto* perpendicular = std::get_if<PerpendicularConstraint>(&constraint)) {
            if (perpendicular->firstLineId == entityId || perpendicular->secondLineId == entityId) return true;
        } else if (const auto* angleBetween = std::get_if<AngleBetweenLinesConstraint>(&constraint)) {
            if (angleBetween->referenceLineId == entityId || angleBetween->dependentLineId == entityId) return true;
        } else if (const auto* tangent = std::get_if<TangentConstraint>(&constraint)) {
            if (tangent->firstEntityId == entityId || tangent->secondEntityId == entityId) return true;
        } else if (const auto* equal = std::get_if<EqualConstraint>(&constraint)) {
            if (equal->referenceEntityId == entityId || equal->dependentEntityId == entityId) return true;
        }
    }
    return false;
}

std::size_t SketchFeature::entityCount() const noexcept { return entities_.size(); }

void SketchFeature::addEntity(SketchEntity entity)
{
    ensureEntityId(entity);
    if (const auto* circle = std::get_if<SketchCircle>(&entity); circle
        && (!std::isfinite(circle->radius) || circle->radius <= 0.0)) {
        throw std::invalid_argument("Sketch circle radius must be positive");
    }
    if (const auto* arc = std::get_if<SketchArc>(&entity)) {
        if (!std::isfinite(arc->center.X()) || !std::isfinite(arc->center.Y())
            || !std::isfinite(arc->radius) || arc->radius <= 1.0e-6
            || !std::isfinite(arc->startAngle) || !std::isfinite(arc->endAngle)
            || std::abs(arc->signedSweep()) <= 1.0e-6
            || std::abs(arc->signedSweep()) >= sketchTwoPi - 1.0e-6) {
            throw std::invalid_argument("Sketch arc has invalid radius or sweep");
        }
    }
    entities_.push_back(std::move(entity));
    markDirty();
}

void SketchFeature::removeLastEntity()
{
    if (entities_.empty()) throw std::runtime_error("Sketch has no entity to remove");
    entities_.pop_back();
    markDirty();
}

void SketchFeature::replaceEntities(const std::size_t index, const std::size_t count,
                                    std::vector<SketchEntity> replacements)
{
    if (index > entities_.size() || index + count > entities_.size()) {
        throw std::out_of_range("Sketch entity replacement range is invalid");
    }
    for (auto& replacement : replacements) ensureEntityId(replacement);
    entities_.erase(entities_.begin() + static_cast<std::ptrdiff_t>(index),
                    entities_.begin() + static_cast<std::ptrdiff_t>(index + count));
    entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(index),
                     std::make_move_iterator(replacements.begin()),
                     std::make_move_iterator(replacements.end()));
    markDirty();
}

bool SketchFeature::isPlanarFace(const TopoDS_Shape& shape) noexcept
{
    if (shape.IsNull() || shape.ShapeType() != TopAbs_FACE) return false;
    try {
        return BRepAdaptor_Surface(TopoDS::Face(shape), Standard_True).GetType()
            == GeomAbs_Plane;
    } catch (...) {
        return false;
    }
}

SketchFrame SketchFeature::frameForFace(const TopoDS_Face& face)
{
    BRepAdaptor_Surface surface(face, Standard_True);
    if (surface.GetType() != GeomAbs_Plane)
        throw std::invalid_argument("Sketch on Face currently supports planar faces only");

    GProp_GProps properties;
    BRepGProp::SurfaceProperties(face, properties);
    gp_Dir normal = surface.Plane().Axis().Direction();
    if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();

    const gp_Vec z(0, 0, 1);
    const gp_Vec x(1, 0, 0);
    const gp_Vec reference = std::abs(gp_Vec(normal).Dot(z)) < 0.95 ? z : x;
    gp_Dir xDirection(reference.Crossed(gp_Vec(normal)));
    gp_Dir yDirection(gp_Vec(normal).Crossed(gp_Vec(xDirection)));
    return {properties.CentreOfMass(), xDirection, yDirection, normal};
}

TopoDS_Shape SketchFeature::build() const
{
    SketchFrame frame = frame_;
    if (supportType_ == SketchSupportType::Face) {
        const auto source = supportSource_.lock();
        if (!source) throw std::runtime_error("Sketch support feature no longer exists");
        const auto resolved = cad::topology::TopologicalReferenceResolver::resolveAgainstShape(
            *faceReference_, source->shape());
        if (resolved.status != cad::topology::ResolveStatus::Resolved || !resolved.shape) {
            throw std::runtime_error("Sketch support face could not be resolved: " + resolved.error);
        }
        frame = frameForFace(TopoDS::Face(*resolved.shape));
    } else if (entities_.empty()) {
        return cad::modeling::BasicFeatures::rectangleWire(width_, height_);
    }

    if (entities_.empty()) {
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        return compound;
    }

    // A closed sketch profile is a modeling Face, while incomplete sketch
    // geometry remains an edge/wire presentation. Keep profile assembly in
    // the shared builder so modeling operations do not know entity types.
    try {
        const auto profile = cad::operations::SketchProfileBuilder::build(*this);
        std::vector<TopoDS_Shape> faces;
        faces.reserve(profile.faces.size());
        for (const auto& face : profile.faces) faces.push_back(face);
        return compoundOf(faces);
    } catch (const Standard_Failure&) {
        // Open or otherwise incomplete geometry is still useful while editing.
    } catch (const std::exception&) {
        // Open or otherwise incomplete geometry is still useful while editing.
    }

    std::vector<TopoDS_Edge> edges;
    for (const auto& entity : entities_) {
        if (const auto* line = std::get_if<SketchLine>(&entity)) {
            edges.push_back(BRepBuilderAPI_MakeEdge(
                worldPoint(frame, line->start), worldPoint(frame, line->end)).Edge());
        } else if (const auto* circle = std::get_if<SketchCircle>(&entity)) {
            const gp_Ax2 axis(worldPoint(frame, circle->center), frame.normal);
            edges.push_back(BRepBuilderAPI_MakeEdge(
                gp_Circ(axis, circle->radius)).Edge());
        } else {
            edges.push_back(makeArcEdge(frame, std::get<SketchArc>(entity)));
        }
    }
    if (edges.size() == 1 || std::all_of(entities_.begin(), entities_.end(),
        [](const SketchEntity& entity) { return std::holds_alternative<SketchLine>(entity); })) {
        BRepBuilderAPI_MakeWire wire;
        for (const auto& edge : edges) wire.Add(edge);
        if (wire.IsDone()) return wire.Wire();
    }
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (const auto& edge : edges) builder.Add(compound, edge);
    return compound;
}

FaceFeature::FaceFeature(std::string id, const Ptr& source)
    : ParametricFeature(std::move(id), "Face"), source_(source)
{
    requireFeature(source, "Sketch source");
    if (source->role() != FeatureRole::Sketch) {
        throw std::invalid_argument("Face source must be a Rectangle Sketch");
    }
    sourceFeatureId_ = source->id();
    addDependency(source);
}

std::vector<FeatureProperty> FaceFeature::properties() const
{
    return {textProperty("sourceFeatureId", "Source Sketch", sourceFeatureId_)};
}

void FaceFeature::writeParameters(QJsonObject& object) const
{
    object.insert("sourceFeatureId", QString::fromStdString(sourceFeatureId_));
}

const std::string& FaceFeature::sourceFeatureId() const noexcept
{
    return sourceFeatureId_;
}

ParametricFeature::Ptr FaceFeature::source() const
{
    return source_.lock();
}

TopoDS_Shape FaceFeature::build() const
{
    const auto sketch = source();
    if (!sketch) {
        throw std::runtime_error("Missing Sketch source '" + sourceFeatureId_ + "'");
    }
    if (sketch->state() != FeatureState::UpToDate || sketch->shape().IsNull()) {
        throw std::runtime_error("Sketch '" + sourceFeatureId_ + "' must be rebuilt into a valid profile");
    }
    TopoDS_Face face;
    if (sketch->shape().ShapeType() == TopAbs_FACE) {
        face = TopoDS::Face(sketch->shape());
    } else if (sketch->shape().ShapeType() == TopAbs_WIRE) {
        face = cad::modeling::BasicFeatures::face(TopoDS::Wire(sketch->shape()));
    } else {
        throw std::runtime_error("Sketch '" + sourceFeatureId_
            + "' does not contain a planar Face or closed wire");
    }
    if (!BRepCheck_Analyzer(face).IsValid()) {
        throw std::runtime_error("Sketch '" + sourceFeatureId_ + "' produced an invalid face");
    }
    return face;
}

BoxParametricFeature::BoxParametricFeature(
    std::string id,
    const double width,
    const double depth,
    const double height
)
    : ParametricFeature(std::move(id), "Box"),
      width_(width),
      depth_(depth),
      height_(height)
{
}

std::vector<FeatureProperty> BoxParametricFeature::properties() const
{
    return {numericProperty("width", "Width", width_),
            numericProperty("depth", "Depth", depth_),
            numericProperty("height", "Height", height_)};
}

bool BoxParametricFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "width") setSize(value, depth_, height_);
    else if (key == "depth") setSize(width_, value, height_);
    else if (key == "height") setSize(width_, depth_, value);
    else return false;
    return true;
}

void BoxParametricFeature::writeParameters(QJsonObject& object) const
{
    object.insert("width", width_);
    object.insert("depth", depth_);
    object.insert("height", height_);
}

void BoxParametricFeature::setSize(
    const double width,
    const double depth,
    const double height
)
{
    width_ = width;
    depth_ = depth;
    height_ = height;
    markDirty();
}

double BoxParametricFeature::width() const noexcept
{
    return width_;
}

double BoxParametricFeature::depth() const noexcept
{
    return depth_;
}

double BoxParametricFeature::height() const noexcept
{
    return height_;
}

TopoDS_Shape BoxParametricFeature::build() const
{
    return cad::modeling::BasicFeatures::box(
        width_,
        depth_,
        height_
    );
}

CylinderParametricFeature::CylinderParametricFeature(
    std::string id,
    const double radius,
    const double height
)
    : ParametricFeature(std::move(id), "Cylinder"),
      radius_(radius),
      height_(height)
{
}

std::vector<FeatureProperty> CylinderParametricFeature::properties() const
{
    return {numericProperty("radius", "Radius", radius_),
            numericProperty("height", "Height", height_)};
}

bool CylinderParametricFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "radius") setRadius(value);
    else if (key == "height") setHeight(value);
    else return false;
    return true;
}

void CylinderParametricFeature::writeParameters(QJsonObject& object) const
{
    object.insert("radius", radius_);
    object.insert("height", height_);
}

void CylinderParametricFeature::setRadius(const double radius)
{
    radius_ = radius;
    markDirty();
}

void CylinderParametricFeature::setHeight(const double height)
{
    height_ = height;
    markDirty();
}

double CylinderParametricFeature::radius() const noexcept
{
    return radius_;
}

double CylinderParametricFeature::height() const noexcept
{
    return height_;
}

TopoDS_Shape CylinderParametricFeature::build() const
{
    return cad::modeling::BasicFeatures::cylinder(
        radius_,
        height_
    );
}

ConeFeature::ConeFeature(
    std::string id,
    const double bottomRadius,
    const double topRadius,
    const double height
)
    : ParametricFeature(std::move(id), "Cone"),
      bottomRadius_(bottomRadius),
      topRadius_(topRadius),
      height_(height)
{
}

std::vector<FeatureProperty> ConeFeature::properties() const
{
    return {numericProperty("bottomRadius", "Bottom Radius", bottomRadius_),
            numericProperty("topRadius", "Top Radius", topRadius_),
            numericProperty("height", "Height", height_)};
}

bool ConeFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "bottomRadius") setBottomRadius(value);
    else if (key == "topRadius") setTopRadius(value);
    else if (key == "height") setHeight(value);
    else return false;
    return true;
}

void ConeFeature::writeParameters(QJsonObject& object) const
{
    object.insert("bottomRadius", bottomRadius_);
    object.insert("topRadius", topRadius_);
    object.insert("height", height_);
}

void ConeFeature::setBottomRadius(const double radius)
{
    bottomRadius_ = radius;
    markDirty();
}

void ConeFeature::setTopRadius(const double radius)
{
    topRadius_ = radius;
    markDirty();
}

void ConeFeature::setHeight(const double height)
{
    height_ = height;
    markDirty();
}

double ConeFeature::bottomRadius() const noexcept
{
    return bottomRadius_;
}

double ConeFeature::topRadius() const noexcept
{
    return topRadius_;
}

double ConeFeature::height() const noexcept
{
    return height_;
}

TopoDS_Shape ConeFeature::build() const
{
    return cad::modeling::BasicFeatures::cone(
        bottomRadius_,
        topRadius_,
        height_
    );
}

SphereFeature::SphereFeature(
    std::string id,
    const double radius
)
    : ParametricFeature(std::move(id), "Sphere"),
      radius_(radius)
{
}

std::vector<FeatureProperty> SphereFeature::properties() const
{
    return {numericProperty("radius", "Radius", radius_)};
}

bool SphereFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key != "radius") return false;
    setRadius(value);
    return true;
}

void SphereFeature::writeParameters(QJsonObject& object) const
{
    object.insert("radius", radius_);
}

void SphereFeature::setRadius(const double radius)
{
    radius_ = radius;
    markDirty();
}

double SphereFeature::radius() const noexcept
{
    return radius_;
}

TopoDS_Shape SphereFeature::build() const
{
    return cad::modeling::BasicFeatures::sphere(radius_);
}

TorusFeature::TorusFeature(
    std::string id,
    const double majorRadius,
    const double minorRadius
)
    : ParametricFeature(std::move(id), "Torus"),
      majorRadius_(majorRadius),
      minorRadius_(minorRadius)
{
}

std::vector<FeatureProperty> TorusFeature::properties() const
{
    return {numericProperty("majorRadius", "Major Radius", majorRadius_),
            numericProperty("minorRadius", "Minor Radius", minorRadius_)};
}

bool TorusFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "majorRadius") setMajorRadius(value);
    else if (key == "minorRadius") setMinorRadius(value);
    else return false;
    return true;
}

void TorusFeature::writeParameters(QJsonObject& object) const
{
    object.insert("majorRadius", majorRadius_);
    object.insert("minorRadius", minorRadius_);
}

void TorusFeature::setMajorRadius(const double radius)
{
    majorRadius_ = radius;
    markDirty();
}

void TorusFeature::setMinorRadius(const double radius)
{
    minorRadius_ = radius;
    markDirty();
}

double TorusFeature::majorRadius() const noexcept
{
    return majorRadius_;
}

double TorusFeature::minorRadius() const noexcept
{
    return minorRadius_;
}

TopoDS_Shape TorusFeature::build() const
{
    return cad::modeling::BasicFeatures::torus(
        majorRadius_,
        minorRadius_
    );
}


HexagonFeature::HexagonFeature(
    std::string id,
    const double acrossFlats,
    const double height
)
    : ParametricFeature(std::move(id), "Hexagon"),
      acrossFlats_(acrossFlats),
      height_(height)
{
}

std::vector<FeatureProperty> HexagonFeature::properties() const
{
    return {numericProperty("acrossFlats", "Across Flats", acrossFlats_),
            numericProperty("height", "Height", height_)};
}

bool HexagonFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "acrossFlats") setAcrossFlats(value);
    else if (key == "height") setHeight(value);
    else return false;
    return true;
}

void HexagonFeature::writeParameters(QJsonObject& object) const
{
    object.insert("acrossFlats", acrossFlats_);
    object.insert("height", height_);
}

void HexagonFeature::setAcrossFlats(
    const double acrossFlats
)
{
    acrossFlats_ = acrossFlats;
    markDirty();
}

void HexagonFeature::setHeight(
    const double height
)
{
    height_ = height;
    markDirty();
}

double HexagonFeature::acrossFlats() const noexcept
{
    return acrossFlats_;
}

double HexagonFeature::height() const noexcept
{
    return height_;
}

TopoDS_Shape HexagonFeature::build() const
{
    if (!std::isfinite(acrossFlats_) || acrossFlats_ <= 0.0) {
        throw std::invalid_argument(
            "acrossFlats must be a finite positive number"
        );
    }

    if (!std::isfinite(height_) || height_ <= 0.0) {
        throw std::invalid_argument(
            "height must be a finite positive number"
        );
    }

    // For a regular hexagon:
    // acrossFlats = sqrt(3) * circumradius.
    const double circumradius =
        acrossFlats_ / std::sqrt(3.0);

    std::array<gp_Pnt, 6> points{};

    constexpr double Pi =
        3.1415926535897932384626433832795;

    for (int index = 0; index < 6; ++index) {
        // 30-degree offset gives horizontal top/bottom flats.
        const double angle =
            Pi / 6.0
            + static_cast<double>(index) * Pi / 3.0;

        points[static_cast<std::size_t>(index)] =
            gp_Pnt(
                circumradius * std::cos(angle),
                circumradius * std::sin(angle),
                0.0
            );
    }

    BRepBuilderAPI_MakeWire wireBuilder;

    for (int index = 0; index < 6; ++index) {
        const int nextIndex =
            (index + 1) % 6;

        wireBuilder.Add(
            BRepBuilderAPI_MakeEdge(
                points[static_cast<std::size_t>(index)],
                points[static_cast<std::size_t>(nextIndex)]
            ).Edge()
        );
    }

    if (!wireBuilder.IsDone()) {
        throw std::runtime_error(
            "Hexagon wire construction failed"
        );
    }

    BRepBuilderAPI_MakeFace faceBuilder(
        wireBuilder.Wire()
    );

    if (!faceBuilder.IsDone()) {
        throw std::runtime_error(
            "Hexagon face construction failed"
        );
    }

    BRepPrimAPI_MakePrism prismBuilder(
        faceBuilder.Face(),
        gp_Vec(0.0, 0.0, height_)
    );

    prismBuilder.Build();

    if (!prismBuilder.IsDone()) {
        throw std::runtime_error(
            "Hexagon extrusion failed"
        );
    }

    TopoDS_Shape result =
        prismBuilder.Shape();

    if (result.IsNull()) {
        throw std::runtime_error(
            "Hexagon extrusion returned a null shape"
        );
    }

    return result;
}

ExtrudeFeature::ExtrudeFeature(
    std::string id,
    const Ptr& profile,
    gp_Vec vector
)
    : ParametricFeature(std::move(id), "Extrude"),
      profile_(profile),
      vector_(std::move(vector))
{
    requireFeature(profile_, "profile");
    addDependency(profile_);
}

ExtrudeFeature::ExtrudeFeature(
    std::string id,
    const std::shared_ptr<SketchFeature>& sketch,
    const double distance,
    const bool reversed
)
    : ParametricFeature(std::move(id), "Extrude"),
      sketch_(sketch),
      distance_(distance),
      reversed_(reversed)
{
    requireFeature(sketch_, "sketch");
    if (!std::isfinite(distance_) || distance_ <= 0.001) {
        throw std::invalid_argument("Extrude distance must be greater than zero");
    }
    addDependency(sketch_);
}

std::vector<FeatureProperty> ExtrudeFeature::properties() const
{
    if (sketch_) {
        return {textProperty("sourceSketchId", "Sketch", sketch_->name()),
                numericProperty("distance", "Distance", distance_),
                {"reversed", "Reverse", reversed_, std::nullopt, std::nullopt, true}};
    }
    return {numericProperty("length", "Length", vector_.Magnitude())};
}

bool ExtrudeFeature::setNumericProperty(const std::string& key, const double value)
{
    if (sketch_) {
        if (key != "distance" || !std::isfinite(value) || value <= 0.001) return false;
        distance_ = value;
        markDirty();
        return true;
    }
    if (key != "length" || vector_.Magnitude() == 0.0) return false;
    setVector(vector_.Normalized() * value);
    return true;
}

bool ExtrudeFeature::setProperty(const std::string& key, const PropertyValue& value)
{
    if (sketch_ && key == "reversed" && std::holds_alternative<bool>(value)) {
        reversed_ = std::get<bool>(value);
        markDirty();
        return true;
    }
    return ParametricFeature::setProperty(key, value);
}

std::vector<std::string> ExtrudeFeature::hiddenDependencyIds() const
{
    if (sketch_) return {sketch_->id()};
    return profile_ ? std::vector<std::string>{profile_->id()} : std::vector<std::string>{};
}

void ExtrudeFeature::writeParameters(QJsonObject& object) const
{
    if (sketch_) {
        object.insert("sourceSketchId", QString::fromStdString(sketch_->id()));
        object.insert("distance", distance_);
        object.insert("reversed", reversed_);
        return;
    }
    object.insert("sourceFeatureId", QString::fromStdString(profile_->id()));
    object.insert("vectorX", vector_.X());
    object.insert("vectorY", vector_.Y());
    object.insert("vectorZ", vector_.Z());
}

void ExtrudeFeature::setVector(gp_Vec vector)
{
    vector_ = std::move(vector);
    markDirty();
}

const ParametricFeature::Ptr&
ExtrudeFeature::profile() const noexcept
{
    return profile_;
}

const gp_Vec&
ExtrudeFeature::vector() const noexcept
{
    return vector_;
}

TopoDS_Shape ExtrudeFeature::build() const
{
    if (sketch_) {
        const auto profile = cad::operations::SketchProfileBuilder::build(*sketch_);
        gp_Vec vector(sketch_->currentFrame().normal);
        if (reversed_) vector.Reverse();
        vector *= distance_;
        std::vector<TopoDS_Shape> solids;
        solids.reserve(profile.faces.size());
        for (const auto& face : profile.faces) {
            if (face.IsNull() || face.ShapeType() != TopAbs_FACE) {
                throw std::runtime_error("Extrude requires closed planar Sketch profiles");
            }
            solids.push_back(cad::modeling::BasicFeatures::extrude(face, vector));
        }
        return compoundOf(solids);
    }
    return cad::modeling::BasicFeatures::extrude(
        profile_->shape(),
        vector_
    );
}

PocketFeature::PocketFeature(
    std::string id,
    const Ptr& target,
    const std::shared_ptr<SketchFeature>& sketch,
    const double depth
)
    : ParametricFeature(std::move(id), "Pocket"),
      target_(target),
      sketch_(sketch),
      depth_(depth)
{
    requireFeature(target_, "pocket target");
    requireFeature(sketch_, "pocket sketch");
    if (sketch_->supportType() != SketchSupportType::Face) {
        throw std::invalid_argument("Pocket requires a Face-attached Sketch");
    }
    if (!std::isfinite(depth_) || depth_ <= 0.001) {
        throw std::invalid_argument("Pocket depth must be greater than zero");
    }
    addDependency(target_);
    addDependency(sketch_);
}

std::vector<FeatureProperty> PocketFeature::properties() const
{
    return {textProperty("targetFeatureId", "Target", target_->name()),
            textProperty("sourceSketchId", "Sketch", sketch_->name()),
            numericProperty("depth", "Depth", depth_),
            textProperty("mode", "Mode", "Blind")};
}

bool PocketFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key != "depth" || !std::isfinite(value) || value <= 0.001) return false;
    depth_ = value;
    markDirty();
    return true;
}

std::vector<std::string> PocketFeature::hiddenDependencyIds() const
{
    return {target_->id(), sketch_->id()};
}

const ParametricFeature::Ptr& PocketFeature::target() const noexcept { return target_; }
const std::shared_ptr<SketchFeature>& PocketFeature::sketch() const noexcept { return sketch_; }
double PocketFeature::depth() const noexcept { return depth_; }

TopoDS_Shape PocketFeature::build() const
{
    if (target_->shape().IsNull()
        || !TopExp_Explorer(target_->shape(), TopAbs_SOLID).More()) {
        throw std::runtime_error("Pocket target is not a solid");
    }
    const auto profile = cad::operations::SketchProfileBuilder::build(*sketch_);
    const auto frame = sketch_->currentFrame();
    const auto support = cad::topology::TopologicalReferenceResolver::resolveAgainstShape(
        *sketch_->faceReference(), target_->shape());
    if (support.status != cad::topology::ResolveStatus::Resolved || !support.shape
        || support.shape->ShapeType() != TopAbs_FACE) {
        throw std::runtime_error("Sketch support face could not be resolved for Pocket");
    }

    const double epsilon = 1.0e-3;
    const double classifierTolerance = 1.0e-7;
    const gp_Pnt plus = frame.origin.Translated(gp_Vec(frame.normal) * epsilon);
    const gp_Pnt minus = frame.origin.Translated(gp_Vec(frame.normal) * -epsilon);
    BRepClass3d_SolidClassifier classifier(target_->shape());
    classifier.Perform(plus, classifierTolerance);
    const bool plusInside = classifier.State() == TopAbs_IN;
    classifier.Perform(minus, classifierTolerance);
    const bool minusInside = classifier.State() == TopAbs_IN;
    if (plusInside == minusInside) {
        throw std::runtime_error("Unable to determine inward pocket direction");
    }
    gp_Vec direction(frame.normal);
    if (minusInside) direction.Reverse();
    std::vector<TopoDS_Shape> tools;
    tools.reserve(profile.faces.size());
    for (const auto& face : profile.faces) {
        if (face.IsNull() || face.ShapeType() != TopAbs_FACE) {
            throw std::runtime_error("Pocket requires closed planar Sketch profiles");
        }
        tools.push_back(cad::modeling::BasicFeatures::extrude(
            face, direction * depth_));
    }
    const TopoDS_Shape tool = compoundOf(tools);
    const TopoDS_Shape result = cad::modeling::BasicFeatures::cut(target_->shape(), tool);
    if (result.IsNull()) throw std::runtime_error("Pocket returned a null shape");

    GProp_GProps beforeProperties;
    GProp_GProps afterProperties;
    BRepGProp::VolumeProperties(target_->shape(), beforeProperties);
    BRepGProp::VolumeProperties(result, afterProperties);
    const double before = beforeProperties.Mass();
    const double after = afterProperties.Mass();
    const double tolerance = std::max(1.0e-7, before * 1.0e-7);
    if (before - after <= tolerance) throw std::runtime_error("Pocket does not intersect target solid");
    if (after <= tolerance) throw std::runtime_error("Pocket removes entire target solid");
    return result;
}

void PocketFeature::writeParameters(QJsonObject& object) const
{
    object.insert("targetFeatureId", QString::fromStdString(target_->id()));
    object.insert("sourceSketchId", QString::fromStdString(sketch_->id()));
    object.insert("depth", depth_);
    object.insert("mode", "Blind");
}

PushPullFeature::PushPullFeature(
    std::string id,
    const Ptr& source,
    const int faceIndex,
    gp_Vec normal,
    const double distance
)
    : ParametricFeature(std::move(id), "Push/Pull"),
      sourceFeatureId_(source ? source->id() : std::string{}),
      source_(source),
      faceIndex_(faceIndex),
      normal_(std::move(normal)),
      distance_(distance)
{
    requireFeature(source, "Push/Pull source");
    if (faceIndex_ <= 0) {
        throw std::invalid_argument("Push/Pull face index must be positive");
    }
    if (normal_.Magnitude() <= 1.0e-9) {
        throw std::invalid_argument("Push/Pull normal must not be zero");
    }
    if (!std::isfinite(distance_) || std::abs(distance_) <= 1.0e-9) {
        throw std::invalid_argument("Push/Pull distance must not be zero");
    }
    normal_.Normalize();
    if (!source->shape().IsNull()) {
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(source->shape(), TopAbs_FACE, faces);
        if (faceIndex_ <= faces.Extent()) {
            faceReference_ = cad::topology::TopologicalSignatureBuilder::createReference(
                source->id(), source->shape(), faces(faceIndex_));
        }
    }
    addDependency(source);
}

PushPullFeature::PushPullFeature(
    std::string id,
    const Ptr& source,
    cad::topology::TopologicalReference faceReference,
    gp_Vec normal,
    const double distance
)
    : ParametricFeature(std::move(id), "Push/Pull"),
      sourceFeatureId_(source ? source->id() : std::string{}),
      source_(source), faceIndex_(0), faceReference_(std::move(faceReference)),
      normal_(std::move(normal)), distance_(distance)
{
    requireFeature(source, "Push/Pull source");
    if (!faceReference_ || faceReference_->featureId != source->id()
        || faceReference_->kind != cad::topology::TopologicalKind::Face)
        throw std::invalid_argument("Push/Pull reference must identify a source Face");
    if (normal_.Magnitude() <= 1.0e-9) throw std::invalid_argument("Push/Pull normal must not be zero");
    if (!std::isfinite(distance_) || std::abs(distance_) <= 1.0e-9)
        throw std::invalid_argument("Push/Pull distance must not be zero");
    normal_.Normalize();
    addDependency(source);
}

std::vector<FeatureProperty> PushPullFeature::properties() const
{
    return {textProperty("sourceFeatureId", "Source", sourceFeatureId_),
            {"faceIndex", "Face", faceIndex_, std::nullopt, std::nullopt, false},
            numericProperty("distance", "Distance", std::abs(distance_))};
}

bool PushPullFeature::setNumericProperty(
    const std::string& key,
    const double value
)
{
    if (key != "distance" || !std::isfinite(value) || value <= 0.001) {
        return false;
    }
    distance_ = distance_ < 0.0 ? -value : value;
    markDirty();
    return true;
}

std::vector<std::string> PushPullFeature::hiddenDependencyIds() const
{
    return {sourceFeatureId_};
}

ParametricFeature::Ptr PushPullFeature::source() const
{
    return source_.lock();
}

const std::string& PushPullFeature::sourceFeatureId() const noexcept
{
    return sourceFeatureId_;
}

int PushPullFeature::faceIndex() const noexcept
{
    return faceIndex_;
}

const cad::topology::TopologicalReference& PushPullFeature::faceReference() const noexcept
{
    return *faceReference_;
}

const gp_Vec& PushPullFeature::normal() const noexcept
{
    return normal_;
}

double PushPullFeature::distance() const noexcept
{
    return distance_;
}

TopoDS_Shape PushPullFeature::build() const
{
    const auto source = source_.lock();
    requireFeature(source, "Push/Pull source");

    TopoDS_Shape selectedShape;
    if (faceReference_) {
        const auto resolved = cad::topology::TopologicalReferenceResolver::resolveAgainstShape(
            *faceReference_, source->shape());
        if (resolved.status != cad::topology::ResolveStatus::Resolved || !resolved.shape)
            throw std::runtime_error(resolved.error.empty()
                ? "Push/Pull face reference is no longer valid" : resolved.error);
        selectedShape = *resolved.shape;
    } else {
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(source->shape(), TopAbs_FACE, faces);
        if (source->shape().IsNull() || faces.Extent() == 0 || faceIndex_ <= 0
            || faceIndex_ > faces.Extent())
            throw std::runtime_error("Push/Pull face reference is no longer valid");
        selectedShape = faces.FindKey(faceIndex_);
    }
    if (selectedShape.IsNull() || selectedShape.ShapeType() != TopAbs_FACE) {
        throw std::runtime_error("Push/Pull source subshape is not a Face");
    }
    const TopoDS_Face face = TopoDS::Face(selectedShape);
    if (!SketchFeature::isPlanarFace(face)) {
        throw std::runtime_error("Push/Pull currently supports planar faces only");
    }
    BRepPrimAPI_MakePrism prismBuilder(face, normal_ * distance_);
    prismBuilder.Build();
    if (!prismBuilder.IsDone() || prismBuilder.Shape().IsNull()) {
        throw std::runtime_error("Push/Pull prism construction failed");
    }
    const TopoDS_Shape prism = prismBuilder.Shape();

    // A Sketch-derived Face has no solid base to fuse or cut. In that case
    // Push/Pull is the face-to-prism operation itself. Solid Face sources
    // retain the existing additive/subtractive behavior below.
    if (source->shape().ShapeType() == TopAbs_FACE) {
        return prism;
    }

    if (distance_ > 0.0) {
        BRepAlgoAPI_Fuse fuse(source->shape(), prism);
        fuse.Build();
        if (!fuse.IsDone() || fuse.Shape().IsNull()) {
            throw std::runtime_error("Push/Pull fuse failed");
        }
        return fuse.Shape();
    }

    BRepAlgoAPI_Cut cut(source->shape(), prism);
    cut.Build();
    if (!cut.IsDone() || cut.Shape().IsNull()) {
        throw std::runtime_error("Push/Pull cut failed");
    }
    return cut.Shape();
}

void PushPullFeature::writeParameters(QJsonObject& object) const
{
    object.insert("sourceFeatureId", QString::fromStdString(sourceFeatureId_));
    if (faceReference_) object.insert("faceReference", cad::topology::toJson(*faceReference_));
    if (faceIndex_ > 0) object.insert("faceIndex", faceIndex_);
    object.insert("normalX", normal_.X());
    object.insert("normalY", normal_.Y());
    object.insert("normalZ", normal_.Z());
    object.insert("distance", distance_);
}

RevolveFeature::RevolveFeature(
    std::string id,
    const Ptr& profile,
    gp_Ax1 axis,
    const double angleRadians
)
    : ParametricFeature(std::move(id), "Revolve"),
      profile_(profile),
      axis_(std::move(axis)),
      angleRadians_(angleRadians)
{
    requireFeature(profile_, "profile");
    addDependency(profile_);
}

void RevolveFeature::setAxis(gp_Ax1 axis)
{
    axis_ = std::move(axis);
    markDirty();
}

void RevolveFeature::setAngleRadians(
    const double angleRadians
)
{
    angleRadians_ = angleRadians;
    markDirty();
}

const ParametricFeature::Ptr&
RevolveFeature::profile() const noexcept
{
    return profile_;
}

const gp_Ax1&
RevolveFeature::axis() const noexcept
{
    return axis_;
}

double RevolveFeature::angleRadians() const noexcept
{
    return angleRadians_;
}

TopoDS_Shape RevolveFeature::build() const
{
    return cad::modeling::BasicFeatures::revolve(
        profile_->shape(),
        axis_,
        angleRadians_
    );
}

void RevolveFeature::writeParameters(QJsonObject& object) const
{
    object.insert("sourceFeatureId", QString::fromStdString(profile_->id()));
    object.insert("axisOriginX", axis_.Location().X());
    object.insert("axisOriginY", axis_.Location().Y());
    object.insert("axisOriginZ", axis_.Location().Z());
    object.insert("axisX", axis_.Direction().X());
    object.insert("axisY", axis_.Direction().Y());
    object.insert("axisZ", axis_.Direction().Z());
    object.insert("angleDegrees", angleRadians_ * 180.0 / std::acos(-1.0));
}

BooleanFeature::BooleanFeature(
    std::string id,
    const Ptr& left,
    const Ptr& right,
    const BooleanOperation operation
)
    : ParametricFeature(std::move(id), "Boolean"),
      left_(left),
      right_(right),
      operation_(operation)
{
    requireFeature(left_, "left");
    requireFeature(right_, "right");

    addDependency(left_);
    addDependency(right_);
}

std::vector<FeatureProperty> BooleanFeature::properties() const
{
    const char* operationName = operation_ == BooleanOperation::Fuse ? "Fuse"
        : operation_ == BooleanOperation::Cut ? "Cut" : "Common";
    return {textProperty("operation", "Operation", operationName),
            textProperty("left", "Left", left_->name()),
            textProperty("right", "Right", right_->name())};
}

std::string BooleanFeature::creationLabel() const
{
    return operation_ == BooleanOperation::Fuse ? "Boolean Fuse"
        : operation_ == BooleanOperation::Cut ? "Boolean Cut" : "Boolean Common";
}

std::vector<std::string> BooleanFeature::hiddenDependencyIds() const
{
    if (operation_ != BooleanOperation::Cut) return {};
    return {left_->id(), right_->id()};
}

void BooleanFeature::writeParameters(QJsonObject& object) const
{
    object.insert("left", QString::fromStdString(left_->id()));
    object.insert("right", QString::fromStdString(right_->id()));
    object.insert("operation", operation_ == BooleanOperation::Fuse ? "Fuse"
        : operation_ == BooleanOperation::Cut ? "Cut" : "Common");
}

void BooleanFeature::setOperation(
    const BooleanOperation operation
)
{
    operation_ = operation;
    markDirty();
}

const ParametricFeature::Ptr&
BooleanFeature::left() const noexcept
{
    return left_;
}

const ParametricFeature::Ptr&
BooleanFeature::right() const noexcept
{
    return right_;
}

BooleanOperation
BooleanFeature::operation() const noexcept
{
    return operation_;
}

TopoDS_Shape BooleanFeature::build() const
{
    switch (operation_) {
        case BooleanOperation::Fuse:
            return cad::modeling::BasicFeatures::fuse(
                left_->shape(),
                right_->shape()
            );

        case BooleanOperation::Cut:
            return cad::modeling::BasicFeatures::cut(
                left_->shape(),
                right_->shape()
            );

        case BooleanOperation::Common:
            return cad::modeling::BasicFeatures::common(
                left_->shape(),
                right_->shape()
            );
    }

    throw std::logic_error("Unknown Boolean operation");
}

FilletFeature::FilletFeature(
    std::string id,
    const Ptr& base,
    std::vector<TopoDS_Edge> edges,
    const double radius
)
    : FilletFeature(
          std::move(id), base, referencesFor(base, edges), radius)
{
}

FilletFeature::FilletFeature(
    std::string id,
    const Ptr& base,
    std::vector<int> edgeIndices,
    const double radius
)
    : FilletFeature(std::move(id), base, legacyReferences(base ? base->id() : "", edgeIndices), radius)
{
}

FilletFeature::FilletFeature(
    std::string id,
    const Ptr& base,
    std::vector<cad::topology::TopologicalReference> references,
    const double radius
)
    : ParametricFeature(std::move(id), "Fillet"),
      base_(base),
      references_(std::move(references)),
      radius_(radius)
{
    requireFeature(base_, "base");
    if (!std::isfinite(radius_) || radius_ <= 0.001) {
        throw std::invalid_argument("Fillet radius must be positive");
    }
    upgradeLegacyReferences(base_, references_);
    validateReferences(references_, base_->id());
    addDependency(base_);
}

std::vector<FeatureProperty> FilletFeature::properties() const
{
    return {textProperty("sourceFeatureId", "Source", base_->id()),
            numericProperty("radius", "Radius", radius_)};
}

bool FilletFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key != "radius" || !std::isfinite(value) || value <= 0.001) return false;
    radius_ = value;
    markDirty();
    return true;
}

std::vector<std::string> FilletFeature::hiddenDependencyIds() const
{
    return {base_->id()};
}

std::string FilletFeature::creationLabel() const
{
    return "Fillet";
}

std::vector<int> FilletFeature::edgeIndices() const
{
    std::vector<int> result;
    for (const auto& reference : references_) {
        if (reference.transientIndex) result.push_back(*reference.transientIndex);
    }
    return result;
}

const std::vector<cad::topology::TopologicalReference>&
FilletFeature::references() const noexcept
{
    return references_;
}

void FilletFeature::setEdges(
    std::vector<TopoDS_Edge> edges
)
{
    references_ = referencesFor(base_, edges);
    edges_ = std::move(edges);
    markDirty();
}

void FilletFeature::setRadius(const double radius)
{
    radius_ = radius;
    markDirty();
}

const ParametricFeature::Ptr&
FilletFeature::base() const noexcept
{
    return base_;
}

const std::vector<TopoDS_Edge>&
FilletFeature::edges() const noexcept
{
    return edges_;
}

double FilletFeature::radius() const noexcept
{
    return radius_;
}

TopoDS_Shape FilletFeature::build() const
{
    return cad::modeling::BasicFeatures::fillet(
        base_->shape(),
        resolveEdges(base_, references_),
        radius_
    );
}

void FilletFeature::writeParameters(QJsonObject& object) const
{
    object.insert("sourceFeatureId", QString::fromStdString(base_->id()));
    writeReferences(object, references_);
    object.insert("radius", radius_);
}

ChamferFeature::ChamferFeature(
    std::string id,
    const Ptr& base,
    std::vector<TopoDS_Edge> edges,
    const double distance
)
    : ChamferFeature(
          std::move(id), base, referencesFor(base, edges), distance)
{
}

ChamferFeature::ChamferFeature(
    std::string id,
    const Ptr& base,
    std::vector<int> edgeIndices,
    const double distance
)
    : ChamferFeature(std::move(id), base, legacyReferences(base ? base->id() : "", edgeIndices), distance)
{
}

ChamferFeature::ChamferFeature(
    std::string id,
    const Ptr& base,
    std::vector<cad::topology::TopologicalReference> references,
    const double distance
)
    : ParametricFeature(std::move(id), "Chamfer"),
      base_(base),
      references_(std::move(references)),
      distance_(distance)
{
    requireFeature(base_, "base");
    if (!std::isfinite(distance_) || distance_ <= 0.001) {
        throw std::invalid_argument("Chamfer distance must be positive");
    }
    upgradeLegacyReferences(base_, references_);
    validateReferences(references_, base_->id());
    addDependency(base_);
}

std::vector<FeatureProperty> ChamferFeature::properties() const
{
    return {textProperty("sourceFeatureId", "Source", base_->id()),
            numericProperty("distance", "Distance", distance_)};
}

bool ChamferFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key != "distance" || !std::isfinite(value) || value <= 0.001) return false;
    distance_ = value;
    markDirty();
    return true;
}

std::vector<std::string> ChamferFeature::hiddenDependencyIds() const
{
    return {base_->id()};
}

std::string ChamferFeature::creationLabel() const
{
    return "Chamfer";
}

std::vector<int> ChamferFeature::edgeIndices() const
{
    std::vector<int> result;
    for (const auto& reference : references_) {
        if (reference.transientIndex) result.push_back(*reference.transientIndex);
    }
    return result;
}

const std::vector<cad::topology::TopologicalReference>&
ChamferFeature::references() const noexcept
{
    return references_;
}

void ChamferFeature::setEdges(
    std::vector<TopoDS_Edge> edges
)
{
    references_ = referencesFor(base_, edges);
    edges_ = std::move(edges);
    markDirty();
}

void ChamferFeature::setDistance(
    const double distance
)
{
    distance_ = distance;
    markDirty();
}

const ParametricFeature::Ptr&
ChamferFeature::base() const noexcept
{
    return base_;
}

const std::vector<TopoDS_Edge>&
ChamferFeature::edges() const noexcept
{
    return edges_;
}

double ChamferFeature::distance() const noexcept
{
    return distance_;
}

TopoDS_Shape ChamferFeature::build() const
{
    return cad::modeling::BasicFeatures::chamfer(
        base_->shape(),
        resolveEdges(base_, references_),
        distance_
    );
}

void ChamferFeature::writeParameters(QJsonObject& object) const
{
    object.insert("sourceFeatureId", QString::fromStdString(base_->id()));
    writeReferences(object, references_);
    object.insert("distance", distance_);
}

ShellFeature::ShellFeature(
    std::string id,
    const Ptr& base,
    std::vector<TopoDS_Face> facesToRemove,
    const double thickness
)
    : ParametricFeature(std::move(id), "Shell"),
      base_(base),
      facesToRemove_(std::move(facesToRemove)),
      thickness_(thickness)
{
    requireFeature(base_, "base");
    faceReferences_ = faceReferencesFor(base_, facesToRemove_);
    addDependency(base_);
}

ShellFeature::ShellFeature(
    std::string id,
    const Ptr& base,
    std::vector<cad::topology::TopologicalReference> faceReferences,
    const double thickness
)
    : ParametricFeature(std::move(id), "Shell"),
      base_(base), faceReferences_(std::move(faceReferences)), thickness_(thickness)
{
    requireFeature(base_, "base");
    validateFaceReferences(faceReferences_, base_->id());
    addDependency(base_);
}

ShellFeature::ShellFeature(
    std::string id,
    const Ptr& base,
    std::vector<int> faceIndices,
    const double thickness
)
    : ParametricFeature(std::move(id), "Shell"),
      base_(base),
      faceIndices_(std::move(faceIndices)),
      thickness_(thickness)
{
    requireFeature(base_, "base");
    if (faceIndices_.empty()) throw std::invalid_argument("Shell requires a removable face");
    addDependency(base_);
}

void ShellFeature::setFacesToRemove(
    std::vector<TopoDS_Face> faces
)
{
    facesToRemove_ = std::move(faces);
    markDirty();
}

void ShellFeature::setThickness(
    const double thickness
)
{
    thickness_ = thickness;
    markDirty();
}

const ParametricFeature::Ptr&
ShellFeature::base() const noexcept
{
    return base_;
}

const std::vector<TopoDS_Face>&
ShellFeature::facesToRemove() const noexcept
{
    return facesToRemove_;
}

const std::vector<cad::topology::TopologicalReference>&
ShellFeature::faceReferences() const noexcept
{
    return faceReferences_;
}

double ShellFeature::thickness() const noexcept
{
    return thickness_;
}

std::vector<FeatureProperty> ShellFeature::properties() const
{
    return {textProperty("sourceFeatureId", "Source", base_->name()),
            numericProperty("thickness", "Thickness", thickness_)};
}

bool ShellFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key != "thickness" || !std::isfinite(value) || value <= 0.001) return false;
    thickness_ = value;
    markDirty();
    return true;
}

std::vector<std::string> ShellFeature::hiddenDependencyIds() const
{
    return {base_->id()};
}

TopoDS_Shape ShellFeature::build() const
{
    auto faces = facesToRemove_;
    if (!faceReferences_.empty()) {
        faces = resolveFaces(base_, faceReferences_);
    } else if (!faceIndices_.empty()) {
        TopTools_IndexedMapOfShape map;
        TopExp::MapShapes(base_->shape(), TopAbs_FACE, map);
        faces.clear();
        for (const int index : faceIndices_) {
            if (index <= 0 || index > map.Extent())
                throw std::runtime_error("Shell face reference is no longer valid");
            faces.push_back(TopoDS::Face(map.FindKey(index)));
        }
    }
    return cad::modeling::BasicFeatures::shell(
        base_->shape(),
        faces,
        thickness_
    );
}

void ShellFeature::writeParameters(QJsonObject& object) const
{
    object.insert("sourceFeatureId", QString::fromStdString(base_->id()));
    if (!faceReferences_.empty()) {
        QJsonArray references;
        for (const auto& reference : faceReferences_)
            references.append(cad::topology::toJson(reference));
        object.insert("topologicalReferences", references);
    }
    QJsonArray indices;
    if (!faceReferences_.empty()) {
        for (const auto& reference : faceReferences_)
            if (reference.transientIndex) indices.append(*reference.transientIndex);
    } else if (!faceIndices_.empty()) {
        for (const int index : faceIndices_) indices.append(index);
    } else {
        TopTools_IndexedMapOfShape map;
        TopExp::MapShapes(base_->shape(), TopAbs_FACE, map);
        for (const auto& face : facesToRemove_) {
            const int index = map.FindIndex(face);
            if (index <= 0) throw std::runtime_error("Shell face is not part of source");
            indices.append(index);
        }
    }
    object.insert("faceIndices", indices);
    object.insert("thickness", thickness_);
}

OffsetFeature::OffsetFeature(
    std::string id,
    const Ptr& base,
    const double distance
)
    : ParametricFeature(std::move(id), "Offset"),
      base_(base),
      distance_(distance)
{
    requireFeature(base_, "base");
    addDependency(base_);
}

void OffsetFeature::setDistance(
    const double distance
)
{
    distance_ = distance;
    markDirty();
}

const ParametricFeature::Ptr&
OffsetFeature::base() const noexcept
{
    return base_;
}

double OffsetFeature::distance() const noexcept
{
    return distance_;
}

TopoDS_Shape OffsetFeature::build() const
{
    return cad::modeling::BasicFeatures::offset(
        base_->shape(),
        distance_
    );
}

void OffsetFeature::writeParameters(QJsonObject& object) const
{
    object.insert("sourceFeatureId", QString::fromStdString(base_->id()));
    object.insert("distance", distance_);
}

LoftFeature::LoftFeature(
    std::string id,
    std::vector<TopoDS_Wire> sections,
    const bool makeSolid,
    const bool ruled
)
    : ParametricFeature(std::move(id), "Loft"),
      sections_(std::move(sections)),
      makeSolid_(makeSolid),
      ruled_(ruled)
{
}

LoftFeature::LoftFeature(
    std::string id,
    std::vector<Ptr> sectionFeatures,
    const bool makeSolid,
    const bool ruled
)
    : ParametricFeature(std::move(id), "Loft"),
      sectionFeatures_(std::move(sectionFeatures)),
      makeSolid_(makeSolid),
      ruled_(ruled)
{
    if (sectionFeatures_.size() < 2)
        throw std::invalid_argument("Loft requires at least two sections");
    for (const auto& section : sectionFeatures_) {
        requireFeature(section, "loft section");
        addDependency(section);
    }
}

void LoftFeature::setSections(
    std::vector<TopoDS_Wire> sections
)
{
    sections_ = std::move(sections);
    markDirty();
}

void LoftFeature::setMakeSolid(
    const bool makeSolid
)
{
    makeSolid_ = makeSolid;
    markDirty();
}

void LoftFeature::setRuled(
    const bool ruled
)
{
    ruled_ = ruled;
    markDirty();
}

const std::vector<TopoDS_Wire>&
LoftFeature::sections() const noexcept
{
    return sections_;
}

bool LoftFeature::makeSolid() const noexcept
{
    return makeSolid_;
}

bool LoftFeature::ruled() const noexcept
{
    return ruled_;
}

TopoDS_Shape LoftFeature::build() const
{
    auto sections = sections_;
    if (!sectionFeatures_.empty()) {
        sections.clear();
        for (const auto& section : sectionFeatures_) sections.push_back(wireFromFeature(section));
    }
    return cad::modeling::BasicFeatures::loft(
        sections,
        makeSolid_,
        ruled_
    );
}

void LoftFeature::writeParameters(QJsonObject& object) const
{
    if (sectionFeatures_.empty())
        throw std::runtime_error("Loft has no serializable section feature references");
    QJsonArray sections;
    for (const auto& section : sectionFeatures_)
        sections.append(QString::fromStdString(section->id()));
    object.insert("sectionFeatureIds", sections);
    object.insert("solid", makeSolid_);
    object.insert("ruled", ruled_);
}

SweepFeature::SweepFeature(
    std::string id,
    TopoDS_Wire path,
    const Ptr& profile
)
    : ParametricFeature(std::move(id), "Sweep"),
      path_(std::move(path)),
      profile_(profile)
{
    requireFeature(profile_, "profile");
    addDependency(profile_);
}

SweepFeature::SweepFeature(
    std::string id,
    const Ptr& pathFeature,
    const Ptr& profile
)
    : ParametricFeature(std::move(id), "Sweep"),
      pathFeature_(pathFeature),
      profile_(profile)
{
    requireFeature(pathFeature_, "sweep path");
    requireFeature(profile_, "profile");
    addDependency(pathFeature_);
    addDependency(profile_);
}

void SweepFeature::setPath(
    TopoDS_Wire path
)
{
    path_ = std::move(path);
    markDirty();
}

const TopoDS_Wire&
SweepFeature::path() const noexcept
{
    return path_;
}

const ParametricFeature::Ptr&
SweepFeature::profile() const noexcept
{
    return profile_;
}

TopoDS_Shape SweepFeature::build() const
{
    const auto path = pathFeature_ ? wireFromFeature(pathFeature_) : path_;
    return cad::modeling::BasicFeatures::sweep(
        path,
        profile_->shape()
    );
}

void SweepFeature::writeParameters(QJsonObject& object) const
{
    if (!pathFeature_)
        throw std::runtime_error("Sweep has no serializable path feature reference");
    object.insert("pathFeatureId", QString::fromStdString(pathFeature_->id()));
    object.insert("profileFeatureId", QString::fromStdString(profile_->id()));
    object.insert("solid", true);
    object.insert("frenet", false);
}

ParametricFeature::Ptr SketchFeature::clone(std::string newId) const
{
    ParametricFeature::Ptr copy;
    if (supportType_ == SketchSupportType::Face) {
        copy = std::make_shared<SketchFeature>(
            std::move(newId), supportSource_.lock(), *faceReference_, entities_);
    } else {
        copy = std::make_shared<SketchFeature>(std::move(newId), width_, height_);
    }
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr FaceFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<FaceFeature>(std::move(newId), source());
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr BoxParametricFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<BoxParametricFeature>(
        std::move(newId), width_, depth_, height_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr CylinderParametricFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<CylinderParametricFeature>(
        std::move(newId), radius_, height_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr ConeFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<ConeFeature>(
        std::move(newId), bottomRadius_, topRadius_, height_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr SphereFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<SphereFeature>(std::move(newId), radius_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr TorusFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<TorusFeature>(
        std::move(newId), majorRadius_, minorRadius_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr HexagonFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<HexagonFeature>(
        std::move(newId), acrossFlats_, height_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr ExtrudeFeature::clone(std::string newId) const
{
    if (sketch_) {
        auto copy = std::make_shared<ExtrudeFeature>(
            std::move(newId), sketch_, distance_, reversed_);
        copyPlacementTo(copy);
        return copy;
    }
    auto copy = std::make_shared<ExtrudeFeature>(
        std::move(newId), profile(), vector_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr PocketFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<PocketFeature>(
        std::move(newId), target_, sketch_, depth_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr PushPullFeature::clone(std::string newId) const
{
    auto copy = faceReference_
        ? std::make_shared<PushPullFeature>(
            std::move(newId), source(), *faceReference_, normal_, distance_)
        : std::make_shared<PushPullFeature>(
            std::move(newId), source(), faceIndex_, normal_, distance_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr RevolveFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<RevolveFeature>(
        std::move(newId), profile(), axis_, angleRadians_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr BooleanFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<BooleanFeature>(
        std::move(newId), left_, right_, operation_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr FilletFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<FilletFeature>(
        std::move(newId), base_, references_, radius_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr ChamferFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<ChamferFeature>(
        std::move(newId), base_, references_, distance_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr ShellFeature::clone(std::string newId) const
{
    auto copy = !faceReferences_.empty()
        ? std::make_shared<ShellFeature>(std::move(newId), base_, faceReferences_, thickness_)
        : faceIndices_.empty()
            ? std::make_shared<ShellFeature>(std::move(newId), base_, facesToRemove_, thickness_)
            : std::make_shared<ShellFeature>(std::move(newId), base_, faceIndices_, thickness_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr OffsetFeature::clone(std::string newId) const
{
    auto copy = std::make_shared<OffsetFeature>(
        std::move(newId), base_, distance_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr LoftFeature::clone(std::string newId) const
{
    auto copy = sectionFeatures_.empty()
        ? std::make_shared<LoftFeature>(std::move(newId), sections_, makeSolid_, ruled_)
        : std::make_shared<LoftFeature>(std::move(newId), sectionFeatures_, makeSolid_, ruled_);
    copyPlacementTo(copy);
    return copy;
}

ParametricFeature::Ptr SweepFeature::clone(std::string newId) const
{
    auto copy = pathFeature_
        ? std::make_shared<SweepFeature>(std::move(newId), pathFeature_, profile_)
        : std::make_shared<SweepFeature>(std::move(newId), path_, profile_);
    copyPlacementTo(copy);
    return copy;
}

} // namespace cad::parametric
