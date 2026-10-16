#include "application/ModelingController.h"
#include "application/ModelTransaction.h"
#include "application/SelectionResolver.h"

#include "commands/FeatureCommands.h"
#include "operations/ParametricFeatures.h"
#include "operations/PatternFeatures.h"
#include "operations/SketchProfileBuilder.h"
#include "operations/SketchPathBuilder.h"
#include "operations/SketchTrimService.h"
#include "operations/SketchExtendService.h"
#include "operations/SketchFilletService.h"
#include "operations/SketchConstraintSolver.h"

#include <TopoDS.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GeomAbs_CurveType.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace cad::application {

namespace {
using cad::parametric::ComponentResolver;
using cad::parametric::ModelComponent;

std::string id(const char* prefix)
{
    return std::string(prefix) + "-"
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

ModelingResult failure(const std::exception& error)
{
    return {false, {}, error.what()};
}

bool containsVisibilityCommand(const QUndoCommand* command)
{
    if (dynamic_cast<const cad::commands::SetFeatureVisibilityCommand*>(command) != nullptr
        || dynamic_cast<const cad::commands::SetVisibilityGroupsCommand*>(command) != nullptr
        || dynamic_cast<const cad::commands::SetVisibilityFiltersCommand*>(command) != nullptr
        || dynamic_cast<const cad::commands::SetVisibilityPresetsCommand*>(command) != nullptr
        || dynamic_cast<const cad::commands::SetVisibilityConfigurationCommand*>(command) != nullptr) {
        return true;
    }
    for (int index = 0; index < command->childCount(); ++index) {
        if (containsVisibilityCommand(command->child(index))) return true;
    }
    return false;
}

ModelingResult componentFailure(const std::string& message)
{
    return {false, {}, message, {}};
}

ModelComponent componentFromShape(const TopoDS_Shape& shape, const std::string& id,
                                  const std::string& sourceFeatureId)
{
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    if (bounds.IsVoid()) throw std::runtime_error("Component result has empty bounds");
    ModelComponent component;
    component.id = id;
    component.name = "Component result";
    component.sourceFeatureId = sourceFeatureId;
    bounds.Get(component.minX, component.minY, component.minZ,
               component.maxX, component.maxY, component.maxZ);
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    component.volume = properties.Mass();
    return component;
}

std::vector<ModelComponent> componentMetadata(const cad::parametric::Body& body)
{
    if (!body.components().empty()) return body.components();
    std::vector<ModelComponent> result;
    for (const auto& feature : body.features()) {
        const auto generated = ComponentResolver::enumerate(body, feature->id());
        result.insert(result.end(), generated.begin(), generated.end());
    }
    return result;
}

void collectFeatureAndDependencies(
    const cad::parametric::ParametricFeature::Ptr& feature,
    std::vector<std::string>& ids)
{
    if (!feature) return;
    if (std::find(ids.begin(), ids.end(), feature->id()) != ids.end()) return;
    ids.push_back(feature->id());
    for (const auto& weakDependency : feature->dependencies())
        collectFeatureAndDependencies(weakDependency.lock(), ids);
}

bool validateSketchProfile(
    const cad::parametric::SketchFeature& sketch,
    std::string& error)
{
    try {
        const auto profile = cad::operations::SketchProfileBuilder::build(sketch);
        if (profile.faces.empty()) {
            error = "Sketch does not contain a closed profile";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

gp_Pnt sketchWorldPoint(const cad::parametric::SketchFrame& frame, const gp_Pnt2d& point)
{
    gp_Pnt result = frame.origin;
    result.Translate(gp_Vec(frame.xDirection) * point.X()
        + gp_Vec(frame.yDirection) * point.Y());
    return result;
}

bool sameSegment(const gp_Pnt& a, const gp_Pnt& b, const gp_Pnt& c, const gp_Pnt& d)
{
    constexpr double tolerance = 1.0e-6;
    return (a.Distance(c) <= tolerance && b.Distance(d) <= tolerance)
        || (a.Distance(d) <= tolerance && b.Distance(c) <= tolerance);
}

double orientation2d(const gp_Pnt2d& a, const gp_Pnt2d& b, const gp_Pnt2d& c)
{
    return (b.X() - a.X()) * (c.Y() - a.Y())
        - (b.Y() - a.Y()) * (c.X() - a.X());
}

bool segmentsIntersect(const gp_Pnt2d& a, const gp_Pnt2d& b,
                       const gp_Pnt2d& c, const gp_Pnt2d& d)
{
    constexpr double tolerance = 1.0e-9;
    const auto onSegment = [tolerance](const gp_Pnt2d& p, const gp_Pnt2d& q,
                                       const gp_Pnt2d& r) {
        return std::abs(orientation2d(p, q, r)) <= tolerance
            && r.X() >= std::min(p.X(), q.X()) - tolerance
            && r.X() <= std::max(p.X(), q.X()) + tolerance
            && r.Y() >= std::min(p.Y(), q.Y()) - tolerance
            && r.Y() <= std::max(p.Y(), q.Y()) + tolerance;
    };
    const double abC = orientation2d(a, b, c);
    const double abD = orientation2d(a, b, d);
    const double cdA = orientation2d(c, d, a);
    const double cdB = orientation2d(c, d, b);
    if (((abC > tolerance && abD < -tolerance) || (abC < -tolerance && abD > tolerance))
        && ((cdA > tolerance && cdB < -tolerance) || (cdA < -tolerance && cdB > tolerance))) {
        return true;
    }
    return onSegment(a, b, c) || onSegment(a, b, d)
        || onSegment(c, d, a) || onSegment(c, d, b);
}

std::optional<gp_Trsf> transformToComponent(
    const cad::parametric::ParametricFeature::Ptr& root,
    const std::string& componentId)
{
    if (!root) return std::nullopt;
    if (root->id() == componentId) {
        return gp_Trsf();
    }
    if (const auto transform = std::dynamic_pointer_cast<cad::parametric::TransformFeature>(root)) {
        for (const auto& weakDependency : transform->dependencies()) {
            const auto dependency = weakDependency.lock();
            if (!dependency) continue;
            const auto nested = transformToComponent(dependency, componentId);
            if (!nested) continue;
            gp_Trsf result = transform->transformation();
            result.Multiply(*nested);
            return result;
        }
        return std::nullopt;
    }
    for (const auto& weakDependency : root->dependencies()) {
        const auto dependency = weakDependency.lock();
        const auto nested = transformToComponent(dependency, componentId);
        if (nested) return nested;
    }
    return std::nullopt;
}
}

cad::parametric::Body& ModelingController::body() noexcept { return body_; }
const cad::parametric::Body& ModelingController::body() const noexcept { return body_; }
Document& ModelingController::document() noexcept { return document_; }
QUndoStack& ModelingController::undoStack() noexcept { return undoStack_; }
void ModelingController::undo() { undoStack_.undo(); }
void ModelingController::redo() { undoStack_.redo(); }

std::vector<FeatureDescriptor> ModelingController::features() const
{
    std::vector<FeatureDescriptor> result;
    result.reserve(body_.features().size());
    for (const auto& feature : body_.features()) {
        result.push_back({feature->id(), feature->name(), feature->userVisible(),
                          feature->state(), feature->error(), feature->properties()});
    }
    return result;
}

ModelingResult ModelingController::addFeature(
    const cad::parametric::ParametricFeature::Ptr& feature
)
{
    try {
        undoStack_.push(new cad::commands::AddFeatureCommand(
            body_, feature, QString::fromStdString(feature->creationLabel())));
        return {true, feature->id(), {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::importFeatures(
    std::vector<cad::parametric::ParametricFeature::Ptr> features)
{
    if (features.empty()) return {false, {}, "Import produced no features"};
    const auto firstId = features.front()->id();
    try {
        undoStack_.push(new cad::commands::ImportFeaturesCommand(
            body_, std::move(features), "Import IFC"));
        return {true, firstId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createBox()
{
    return addFeature(std::make_shared<cad::parametric::BoxParametricFeature>(
        id("box"), 100.0, 70.0, 30.0));
}

ModelingResult ModelingController::createCylinder()
{
    return addFeature(std::make_shared<cad::parametric::CylinderParametricFeature>(
        id("cylinder"), 25.0, 60.0));
}

ModelingResult ModelingController::createSketch(
    const cad::parametric::SketchSupportType support)
{
    return addFeature(std::make_shared<cad::parametric::SketchFeature>(
        id("sketch"), support, 100.0, 60.0));
}

ModelingResult ModelingController::createSketchOnFace(
    const SelectionSnapshot& selection)
{
    if (selection.items.size() != 1
        || selection.items.front().kind != SelectionKind::Face
        || !selection.items.front().subshapeIndex) {
        return {false, {}, "Sketch on Face requires exactly one Face"};
    }
    const auto& item = selection.items.front();
    const auto source = body_.findFeature(item.featureId);
    if (!source) return {false, {}, "Sketch support feature does not exist"};
    SelectionResolver resolver(body_);
    const auto face = resolver.resolve(item);
    if (!face || !cad::parametric::SketchFeature::isPlanarFace(*face)) {
        return {false, {}, "Sketch on Face currently supports planar faces only"};
    }
    try {
        const auto reference = cad::topology::TopologicalSignatureBuilder::createReference(
            source->id(), source->shape(), *face);
        return addFeature(std::make_shared<cad::parametric::SketchFeature>(
            id("sketch"), source, reference));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createSketchOnFace(
    const cad::topology::TopologicalReference& faceReference)
{
    if (faceReference.kind != cad::topology::TopologicalKind::Face
        || faceReference.featureId.empty()) {
        return {false, {}, "Face reference must identify a feature Face"};
    }
    const auto source = body_.findFeature(faceReference.featureId);
    if (!source) return {false, {}, "Sketch support feature does not exist"};
    const cad::topology::TopologicalReferenceResolver resolver(body_);
    const auto resolved = resolver.resolve(faceReference, source->shape());
    if (resolved.status != cad::topology::ResolveStatus::Resolved || !resolved.shape) {
        const auto message = resolved.status == cad::topology::ResolveStatus::Ambiguous
            ? "Face reference is ambiguous"
            : resolved.status == cad::topology::ResolveStatus::Missing
                ? "Face reference could not be resolved"
                : "Face reference does not match the support feature";
        return {false, {}, std::string(message)};
    }
    if (resolved.shape->ShapeType() != TopAbs_FACE
        || !cad::parametric::SketchFeature::isPlanarFace(*resolved.shape)) {
        return {false, {}, "Sketch on Face currently supports planar faces only"};
    }
    try {
        const auto stableReference = cad::topology::TopologicalSignatureBuilder::createReference(
            source->id(), source->shape(), *resolved.shape);
        return addFeature(std::make_shared<cad::parametric::SketchFeature>(
            id("sketch"), source, stableReference));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::addSketchLine(
    const std::string& sketchId, const gp_Pnt2d& start, const gp_Pnt2d& end,
    const bool construction)
{
    const auto feature = body_.findFeature(sketchId);
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature);
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    try {
        undoStack_.push(new cad::commands::AddSketchEntityCommand(
            body_, sketch, cad::parametric::SketchLine{start, end, {}, construction}));
        return {true, sketchId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::replaceSketchProfile(
    const std::string& sketchId,
    const std::vector<std::pair<gp_Pnt2d, gp_Pnt2d>>& segments)
{
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        body_.findFeature(sketchId));
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    if (segments.size() < 3) return {false, {}, "Sketch profile requires at least three segments"};

    constexpr double tolerance = 1.0e-7;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const auto& [start, end] = segments[index];
        if (!std::isfinite(start.X()) || !std::isfinite(start.Y())
            || !std::isfinite(end.X()) || !std::isfinite(end.Y())
            || start.Distance(end) <= tolerance) {
            return {false, {}, "Sketch profile contains a zero-length or non-finite segment"};
        }
        const auto& next = segments[(index + 1) % segments.size()].first;
        if (end.Distance(next) > tolerance)
            return {false, {}, "Sketch profile is not an ordered closed chain"};
    }
    for (std::size_t first = 0; first < segments.size(); ++first) {
        for (std::size_t second = first + 1; second < segments.size(); ++second) {
            const bool adjacent = second == first + 1
                || (first == 0 && second + 1 == segments.size());
            if (!adjacent && segmentsIntersect(
                    segments[first].first, segments[first].second,
                    segments[second].first, segments[second].second)) {
                return {false, {}, "Sketch profile contains intersecting or branching segments"};
            }
        }
    }

    std::vector<cad::parametric::SketchEntity> after;
    after.reserve(segments.size());
    for (std::size_t index = 0; index < segments.size(); ++index) {
        std::string entityId;
        bool construction = false;
        if (index < sketch->entities().size()) {
            if (const auto* line = std::get_if<cad::parametric::SketchLine>(
                    &sketch->entities()[index])) {
                entityId = line->id;
                construction = line->construction;
            }
        }
        if (entityId.empty()) entityId = id("entity");
        after.emplace_back(cad::parametric::SketchLine{
            segments[index].first, segments[index].second, entityId, construction});
    }

    try {
        ModelTransaction transaction(body_, undoStack_, "Replace Sketch Profile");
        undoStack_.push(new cad::commands::ReplaceSketchCommand(
            body_, sketch, sketch->entities(), std::move(after),
            sketch->constraints(), sketch->constraints(), "Replace Sketch Profile"));
        if (!transaction.commit()) {
            const auto message = transaction.error().isEmpty()
                ? QStringLiteral("Split transaction failed") : transaction.error();
            return {false, {}, message.toStdString()};
        }
        return {true, sketchId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::editSketchLine(
    const std::string& sketchId, const std::string& entityId,
    const gp_Pnt2d& start, const gp_Pnt2d& end)
{
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        body_.findFeature(sketchId));
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    auto entities = sketch->entities();
    auto found = std::find_if(entities.begin(), entities.end(),
        [&entityId](const auto& entity) {
            const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
            return line && line->id == entityId;
        });
    if (found == entities.end()) return {false, {}, "Sketch Line does not exist"};
    const auto construction = std::get<cad::parametric::SketchLine>(*found).construction;
    *found = cad::parametric::SketchLine{start, end, entityId, construction};
    try {
        undoStack_.push(new cad::commands::ReplaceSketchCommand(
            body_, sketch, sketch->entities(), std::move(entities),
            sketch->constraints(), sketch->constraints(), "Edit Sketch Line"));
        return {true, sketchId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::removeSketchEntity(
    const std::string& sketchId, const std::string& entityId)
{
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        body_.findFeature(sketchId));
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    if (sketch->hasConstraintsForEntity(entityId))
        return {false, {}, "Cannot remove a constrained Sketch entity"};
    auto entities = sketch->entities();
    const auto before = entities.size();
    entities.erase(std::remove_if(entities.begin(), entities.end(),
        [&entityId](const auto& entity) {
            return std::visit([&entityId](const auto& value) { return value.id == entityId; }, entity);
        }), entities.end());
    if (entities.size() == before) return {false, {}, "Sketch entity does not exist"};
    try {
        undoStack_.push(new cad::commands::ReplaceSketchCommand(
            body_, sketch, sketch->entities(), std::move(entities),
            sketch->constraints(), sketch->constraints(), "Remove Sketch Entity"));
        return {true, sketchId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::toggleSketchEntityConstruction(
    const std::string& sketchId, const std::string& entityId)
{
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        body_.findFeature(sketchId));
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    const auto found = std::find_if(sketch->entities().begin(), sketch->entities().end(),
        [&entityId](const auto& entity) {
            return std::visit([&entityId](const auto& value) { return value.id == entityId; }, entity);
        });
    if (found == sketch->entities().end()) return {false, {}, "Sketch entity does not exist"};
    const bool before = std::visit([](const auto& value) { return value.construction; }, *found);
    try {
        undoStack_.push(new cad::commands::ToggleSketchEntityConstructionCommand(
            body_, sketch, entityId, before, !before));
        return {true, sketchId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::addSketchCircle(
    const std::string& sketchId, const gp_Pnt2d& center, const double radius)
{
    const auto feature = body_.findFeature(sketchId);
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature);
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    try {
        undoStack_.push(new cad::commands::AddSketchEntityCommand(
            body_, sketch, cad::parametric::SketchCircle{center, radius}));
        return {true, sketchId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::addSketchArc(
    const std::string& sketchId,
    const gp_Pnt2d& center,
    const gp_Pnt2d& start,
    const gp_Pnt2d& end)
{
    const auto feature = body_.findFeature(sketchId);
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature);
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    const double radius = center.Distance(start);
    if (!std::isfinite(radius) || radius <= 1.0e-6
        || center.Distance(end) <= 1.0e-6) {
        return {false, {}, "Sketch arc requires distinct center and endpoints"};
    }
    const double startAngle = std::atan2(start.Y() - center.Y(), start.X() - center.X());
    const double endAngle = std::atan2(end.Y() - center.Y(), end.X() - center.X());
    try {
        undoStack_.push(new cad::commands::AddSketchEntityCommand(
            body_, sketch, cad::parametric::SketchArc{
                center, radius, startAngle, endAngle, false}));
        return {true, sketchId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::addSketchThreePointArc(
    const std::string& sketchId,
    const gp_Pnt2d& start,
    const gp_Pnt2d& end,
    const gp_Pnt2d& point)
{
    const auto feature = body_.findFeature(sketchId);
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature);
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    const auto arc = cad::parametric::sketchArcFromThreePoints(start, end, point);
    if (!arc) return {false, {}, "Sketch 3-point arc requires non-collinear points"};
    try {
        undoStack_.push(new cad::commands::AddSketchEntityCommand(body_, sketch, *arc));
        return {true, sketchId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::filletSketchLines(
    const std::string& sketchId, const std::string& firstLineId,
    const std::string& secondLineId, const double radius)
{
    return filletSketchCorners(sketchId, {{firstLineId, secondLineId}}, radius);
}

ModelingResult ModelingController::filletSketchCorners(
    const std::string& sketchId,
    const std::vector<SketchFilletCorner>& corners,
    const double radius)
{
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        body_.findFeature(sketchId));
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    if (corners.empty()) return {false, {}, "Sketch Fillet requires at least one corner"};
    const auto working = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        sketch->clone("fillet-preview"));
    if (!working) return {false, {}, "Sketch Fillet could not prepare a working Sketch"};
    auto entities = sketch->entities();
    auto constraints = sketch->constraints();
    working->replaceEntities(0, working->entityCount(), entities);
    working->replaceConstraints(constraints);
    const auto lineById = [](const std::vector<cad::parametric::SketchEntity>& values,
                             const std::string& id) -> const cad::parametric::SketchLine* {
        for (const auto& entity : values) {
            if (const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
                line && line->id == id) return line;
        }
        return nullptr;
    };
    for (std::size_t cornerIndex = 0; cornerIndex < corners.size(); ++cornerIndex) {
        const auto& [firstLineId, secondLineId] = corners[cornerIndex];
        auto plan = cad::operations::SketchFilletService::analyze(
            *working, firstLineId, secondLineId, radius);
        if (!plan.changed) {
            return {false, {}, "Sketch Fillet corner " + std::to_string(cornerIndex + 1)
                + " failed: " + plan.error};
        }
        const auto* originalFirst = lineById(working->entities(), firstLineId);
        const auto* originalSecond = lineById(working->entities(), secondLineId);
        if (!originalFirst || !originalSecond)
            return {false, {}, "Sketch Fillet changed the selected Lines unexpectedly"};
        const auto arcId = "entity-" + QUuid::createUuid().toString(
            QUuid::WithoutBraces).toStdString();
        auto& arc = std::get<cad::parametric::SketchArc>(plan.entities.back());
        arc.id = arcId;
        const auto firstLine = lineById(plan.entities, firstLineId);
        const auto secondLine = lineById(plan.entities, secondLineId);
        if (!firstLine || !secondLine)
            return {false, {}, "Sketch Fillet changed the selected Lines unexpectedly"};
        const auto pointRole = [](const cad::parametric::SketchLine& line,
                                  const cad::parametric::SketchLine& original) {
            return line.start.Distance(original.start) > 1.0e-7
                ? cad::parametric::SketchPointRole::LineStart
                : cad::parametric::SketchPointRole::LineEnd;
        };
        const auto firstRole = pointRole(*firstLine, *originalFirst);
        const auto secondRole = pointRole(*secondLine, *originalSecond);
        plan.constraints.push_back(cad::parametric::TangentConstraint{firstLineId, arcId, {}});
        plan.constraints.push_back(cad::parametric::TangentConstraint{secondLineId, arcId, {}});
        plan.constraints.push_back(cad::parametric::CoincidentConstraint{
            {firstLineId, firstRole}, {arcId, cad::parametric::SketchPointRole::ArcStart}, {}});
        plan.constraints.push_back(cad::parametric::CoincidentConstraint{
            {secondLineId, secondRole}, {arcId, cad::parametric::SketchPointRole::ArcEnd}, {}});
        plan.constraints.push_back(cad::parametric::RadiusConstraint{arcId, radius, {}});
        for (auto& constraint : plan.constraints) {
            std::visit([](auto& value) {
                if (value.id.empty()) value.id = "constraint-" + QUuid::createUuid().toString(
                    QUuid::WithoutBraces).toStdString();
            }, constraint);
        }
        entities = std::move(plan.entities);
        constraints = std::move(plan.constraints);
        working->replaceEntities(0, working->entityCount(), entities);
        working->replaceConstraints(constraints);
    }
    const auto solved = cad::operations::SketchConstraintSolver::solve(entities, constraints);
    if (solved.status != cad::operations::SolveStatus::Solved)
        return {false, {}, "Sketch Fillet validation failed: " + solved.error};
    try {
        undoStack_.push(new cad::commands::ReplaceSketchCommand(
            body_, sketch, sketch->entities(), std::move(entities),
            sketch->constraints(), std::move(constraints), "Sketch Fillet"));
        return {true, sketchId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::chamferSketchCorners(
    const std::string& sketchId,
    const std::vector<SketchChamferCorner>& corners,
    const cad::parametric::SketchChamferMode mode,
    const double firstDistance, const double secondDistance,
    const double angleRadians)
{
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        body_.findFeature(sketchId));
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    if (corners.empty()) return {false, {}, "Sketch Chamfer requires at least one corner"};
    const auto working = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        sketch->clone("chamfer-preview"));
    if (!working) return {false, {}, "Sketch Chamfer could not prepare a working Sketch"};
    auto entities = sketch->entities();
    auto constraints = sketch->constraints();
    working->replaceEntities(0, working->entityCount(), entities);
    working->replaceConstraints(constraints);
    const auto lineById = [](const std::vector<cad::parametric::SketchEntity>& values,
                             const std::string& lineId) -> const cad::parametric::SketchLine* {
        for (const auto& entity : values)
            if (const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
                line && line->id == lineId) return line;
        return nullptr;
    };
    for (std::size_t cornerIndex = 0; cornerIndex < corners.size(); ++cornerIndex) {
        const auto& [firstLineId, secondLineId] = corners[cornerIndex];
        auto plan = cad::operations::SketchChamferService::analyze(
            *working, firstLineId, secondLineId, mode, firstDistance, secondDistance, angleRadians);
        if (!plan.changed)
            return {false, {}, "Sketch Chamfer corner " + std::to_string(cornerIndex + 1)
                + " failed: " + plan.error};
        const auto* originalFirst = lineById(working->entities(), firstLineId);
        const auto* originalSecond = lineById(working->entities(), secondLineId);
        if (!originalFirst || !originalSecond)
            return {false, {}, "Sketch Chamfer changed the selected Lines unexpectedly"};
        auto planEntities = plan.entities;
        const auto chamferId = id("entity");
        auto& chamfer = std::get<cad::parametric::SketchLine>(planEntities.back());
        chamfer.id = chamferId;
        const auto* firstLine = lineById(planEntities, firstLineId);
        const auto* secondLine = lineById(planEntities, secondLineId);
        if (!firstLine || !secondLine)
            return {false, {}, "Sketch Chamfer changed the selected Lines unexpectedly"};
        const auto pointRole = [](const cad::parametric::SketchLine& line,
                                  const cad::parametric::SketchLine& original) {
            return line.start.Distance(original.start) > 1.0e-7
                ? cad::parametric::SketchPointRole::LineStart
                : cad::parametric::SketchPointRole::LineEnd;
        };
        const auto firstRole = pointRole(*firstLine, *originalFirst);
        const auto secondRole = pointRole(*secondLine, *originalSecond);
        const auto constraintId = [] { return id("constraint"); };
        plan.constraints.push_back(cad::parametric::ChamferConstraint{
            firstLineId, secondLineId, chamferId, mode,
            firstDistance, secondDistance, angleRadians, constraintId()});
        plan.constraints.push_back(cad::parametric::CoincidentConstraint{
            {firstLineId, firstRole}, {chamferId, cad::parametric::SketchPointRole::LineStart}, constraintId()});
        plan.constraints.push_back(cad::parametric::CoincidentConstraint{
            {secondLineId, secondRole}, {chamferId, cad::parametric::SketchPointRole::LineEnd}, constraintId()});
        entities = std::move(planEntities);
        constraints = plan.constraints;
        working->replaceEntities(0, working->entityCount(), entities);
        working->replaceConstraints(constraints);
    }
    const auto solved = cad::operations::SketchConstraintSolver::solve(entities, constraints);
    if (solved.status != cad::operations::SolveStatus::Solved)
        return {false, {}, "Sketch Chamfer validation failed: " + solved.error};
    for (const auto& entity : solved.entities) {
        if (const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
            line && line->start.Distance(line->end) <= 1.0e-7) {
            return {false, {}, "Sketch Chamfer validation failed: zero-length Line"};
        }
    }
    for (const auto& constraint : constraints) {
        const auto* chamfer = std::get_if<cad::parametric::ChamferConstraint>(&constraint);
        if (!chamfer) continue;
        const auto findLine = [&solved](const std::string& lineId) {
            return std::find_if(solved.entities.begin(), solved.entities.end(),
                [&lineId](const auto& entity) {
                    const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
                    return line && line->id == lineId;
                });
        };
        const auto chamferLine = findLine(chamfer->chamferLineId);
        if (chamferLine == solved.entities.end())
            return {false, {}, "Sketch Chamfer validation failed: chamfer Line is missing"};
        const auto* line = std::get_if<cad::parametric::SketchLine>(&*chamferLine);
        if (!line || line->start.Distance(line->end) <= 1.0e-7)
            return {false, {}, "Sketch Chamfer validation failed: invalid chamfer Line"};
    }
    try {
        undoStack_.push(new cad::commands::ReplaceSketchCommand(
            body_, sketch, sketch->entities(), solved.entities,
            sketch->constraints(), std::move(constraints), "Sketch Chamfer"));
        return {true, sketchId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::trimSketchEntity(const std::string& sketchId,
                                                    const gp_Pnt2d& click,
                                                    const double hitTolerance)
{
    const auto feature = body_.findFeature(sketchId);
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature);
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    const auto trim = cad::operations::SketchTrimService::trim(*sketch, click, hitTolerance);
    if (!trim.changed) return {false, {}, trim.error};
    if (trim.entityIndex < sketch->entities().size()) {
        const auto id = std::visit([](const auto& entity) { return entity.id; },
            sketch->entities()[trim.entityIndex]);
        if (sketch->hasConstraintsForEntity(id))
            return {false, {}, "Trim of constrained entity is not supported yet"};
    }
    try {
        undoStack_.push(new cad::commands::TrimSketchEntityCommand(
            body_, sketch, trim.entityIndex, sketch->entities()[trim.entityIndex], trim.replacements));
        return {true, sketchId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::extendSketchEntity(const std::string& sketchId,
                                                      const gp_Pnt2d& click,
                                                      const double endpointTolerance)
{
    const auto feature = body_.findFeature(sketchId);
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature);
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    const auto plan = cad::operations::SketchExtendService::extend(
        *sketch, click, endpointTolerance);
    if (!plan.changed) return {false, {}, plan.error};
    const auto targetId = std::visit([](const auto& entity) { return entity.id; }, plan.originalEntity);
    for (const auto& constraint : sketch->constraints()) {
        const bool dimensional = std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, cad::parametric::DistanceConstraint>
                || std::is_same_v<T, cad::parametric::RadiusConstraint>
                || std::is_same_v<T, cad::parametric::AngleConstraint>)
                return item.entityId == targetId;
            else if constexpr (std::is_same_v<T, cad::parametric::HorizontalDistanceConstraint>
                || std::is_same_v<T, cad::parametric::VerticalDistanceConstraint>)
                return item.first.entityId == targetId || item.second.entityId == targetId;
            else if constexpr (std::is_same_v<T, cad::parametric::ParallelConstraint>
                || std::is_same_v<T, cad::parametric::PerpendicularConstraint>)
                return item.firstLineId == targetId || item.secondLineId == targetId;
            else if constexpr (std::is_same_v<T, cad::parametric::AngleBetweenLinesConstraint>)
                return item.referenceLineId == targetId || item.dependentLineId == targetId;
            else if constexpr (std::is_same_v<T, cad::parametric::TangentConstraint>)
                return item.firstEntityId == targetId || item.secondEntityId == targetId;
            else if constexpr (std::is_same_v<T, cad::parametric::EqualConstraint>)
                return item.referenceEntityId == targetId || item.dependentEntityId == targetId;
            return false;
        }, constraint);
        if (dimensional)
            return {false, {}, "Extend of an entity with a dimensional constraint is not supported"};
    }
    auto beforeEntities = sketch->entities();
    auto afterEntities = beforeEntities;
    afterEntities[plan.entityIndex] = plan.extendedEntity;
    if (!sketch->constraints().empty()) {
        const auto solved = cad::operations::SketchConstraintSolver::solve(
            afterEntities, sketch->constraints());
        if (solved.status != cad::operations::SolveStatus::Solved)
            return {false, {}, solved.error};
        afterEntities = solved.entities;
    }
    try {
        undoStack_.push(new cad::commands::ExtendSketchEntityCommand(
            body_, sketch, plan.entityIndex, plan.originalEntity, plan.extendedEntity,
            beforeEntities, afterEntities));
        return {true, sketchId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

namespace {
std::shared_ptr<cad::parametric::SketchFeature> sketchFor(
    cad::parametric::Body& body, const std::string& id)
{
    return std::dynamic_pointer_cast<cad::parametric::SketchFeature>(body.findFeature(id));
}

bool hasEntity(const cad::parametric::SketchFeature& sketch, const std::string& id)
{
    return std::any_of(sketch.entities().begin(), sketch.entities().end(),
        [&id](const auto& entity) {
            return std::visit([&id](const auto& value) { return value.id == id; }, entity);
        });
}

ModelingResult pushConstraint(
    cad::parametric::Body& body, QUndoStack& stack,
    const std::shared_ptr<cad::parametric::SketchFeature>& sketch,
    cad::parametric::SketchConstraint constraint)
{
    std::visit([](auto& value) {
        if (value.id.empty()) {
            value.id = "constraint-"
                + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        }
    }, constraint);
    auto beforeEntities = sketch->entities();
    auto beforeConstraints = sketch->constraints();
    auto afterConstraints = beforeConstraints;
    afterConstraints.push_back(constraint);
    const auto solved = cad::operations::SketchConstraintSolver::solve(
        beforeEntities, afterConstraints);
    if (solved.status != cad::operations::SolveStatus::Solved)
        return {false, {}, solved.error};
    try {
        stack.push(new cad::commands::AddSketchConstraintCommand(
            body, sketch, std::move(constraint), beforeEntities, solved.entities,
            beforeConstraints, afterConstraints));
        return {true, sketch->id(), {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}
}

ModelingResult ModelingController::addSketchHorizontal(
    const std::string& sketchId, const std::string& lineId, const bool anchorStart)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch || !hasEntity(*sketch, lineId)) return {false, {}, "Sketch Line does not exist"};
    const auto entity = std::find_if(sketch->entities().begin(), sketch->entities().end(),
        [&lineId](const auto& value) {
            return std::visit([&lineId](const auto& item) {
                return item.id == lineId && std::is_same_v<std::decay_t<decltype(item)>, cad::parametric::SketchLine>;
            }, value);
        });
    if (entity == sketch->entities().end() || !std::holds_alternative<cad::parametric::SketchLine>(*entity))
        return {false, {}, "Horizontal constraint requires a Line"};
    return pushConstraint(body_, undoStack_, sketch,
        cad::parametric::HorizontalConstraint{lineId, anchorStart});
}

ModelingResult ModelingController::addSketchVertical(
    const std::string& sketchId, const std::string& lineId, const bool anchorStart)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch || !hasEntity(*sketch, lineId)) return {false, {}, "Sketch Line does not exist"};
    const auto entity = std::find_if(sketch->entities().begin(), sketch->entities().end(),
        [&lineId](const auto& value) {
            return std::visit([&lineId](const auto& item) {
                return item.id == lineId && std::is_same_v<std::decay_t<decltype(item)>, cad::parametric::SketchLine>;
            }, value);
        });
    if (entity == sketch->entities().end() || !std::holds_alternative<cad::parametric::SketchLine>(*entity))
        return {false, {}, "Vertical constraint requires a Line"};
    return pushConstraint(body_, undoStack_, sketch,
        cad::parametric::VerticalConstraint{lineId, anchorStart});
}

ModelingResult ModelingController::addSketchCoincident(
    const std::string& sketchId, const cad::parametric::SketchPointRef& a,
    const cad::parametric::SketchPointRef& b)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch || !hasEntity(*sketch, a.entityId) || !hasEntity(*sketch, b.entityId))
        return {false, {}, "Coincident constraint references missing entity"};
    return pushConstraint(body_, undoStack_, sketch,
        cad::parametric::CoincidentConstraint{a, b});
}

ModelingResult ModelingController::addSketchDistance(
    const std::string& sketchId, const std::string& lineId, const double value,
    const bool anchorStart)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch || !hasEntity(*sketch, lineId))
        return {false, {}, "Sketch Line does not exist"};
    const auto line = std::find_if(sketch->entities().begin(), sketch->entities().end(),
        [&lineId](const auto& entity) {
            return std::visit([&](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                return std::is_same_v<T, cad::parametric::SketchLine> && item.id == lineId;
            }, entity);
        });
    if (line == sketch->entities().end() || !std::holds_alternative<cad::parametric::SketchLine>(*line))
        return {false, {}, "Distance constraint requires a Line"};
    if (!std::isfinite(value) || value <= 1.0e-7)
        return {false, {}, "Distance constraint value must be finite and positive"};
    return pushConstraint(body_, undoStack_, sketch,
        cad::parametric::DistanceConstraint{lineId, value, anchorStart, {}});
}

ModelingResult ModelingController::addSketchRadius(
    const std::string& sketchId, const std::string& entityId, const double value)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch || !hasEntity(*sketch, entityId))
        return {false, {}, "Sketch Circle or Arc does not exist"};
    const auto entity = std::find_if(sketch->entities().begin(), sketch->entities().end(),
        [&entityId](const auto& candidate) {
            return std::visit([&](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                return (std::is_same_v<T, cad::parametric::SketchCircle>
                    || std::is_same_v<T, cad::parametric::SketchArc>) && item.id == entityId;
            }, candidate);
        });
    if (entity == sketch->entities().end())
        return {false, {}, "Radius constraint requires a Circle or Arc"};
    if (!std::isfinite(value) || value <= 1.0e-7)
        return {false, {}, "Radius constraint value must be finite and positive"};
    return pushConstraint(body_, undoStack_, sketch,
        cad::parametric::RadiusConstraint{entityId, value, {}});
}

ModelingResult addPointDistanceConstraint(
    cad::parametric::Body& body, QUndoStack& stack, const std::string& sketchId,
    const cad::parametric::SketchPointRef& first,
    const cad::parametric::SketchPointRef& second, const double value, const bool horizontal)
{
    const auto sketch = sketchFor(body, sketchId);
    if (!sketch || !hasEntity(*sketch, first.entityId) || !hasEntity(*sketch, second.entityId))
        return {false, {}, "Distance constraint references missing entity"};
    if (!std::isfinite(value)) return {false, {}, "Point distance must be finite"};
    return pushConstraint(body, stack, sketch, horizontal
        ? cad::parametric::SketchConstraint{cad::parametric::HorizontalDistanceConstraint{first, second, value, {}}}
        : cad::parametric::SketchConstraint{cad::parametric::VerticalDistanceConstraint{first, second, value, {}}});
}

ModelingResult ModelingController::addSketchHorizontalDistance(
    const std::string& sketchId, const cad::parametric::SketchPointRef& first,
    const cad::parametric::SketchPointRef& second, const double value)
{ return addPointDistanceConstraint(body_, undoStack_, sketchId, first, second, value, true); }

ModelingResult ModelingController::addSketchVerticalDistance(
    const std::string& sketchId, const cad::parametric::SketchPointRef& first,
    const cad::parametric::SketchPointRef& second, const double value)
{ return addPointDistanceConstraint(body_, undoStack_, sketchId, first, second, value, false); }

ModelingResult ModelingController::addSketchAngle(
    const std::string& sketchId, const std::string& lineId, const double radians,
    const bool anchorStart)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch || !hasEntity(*sketch, lineId)) return {false, {}, "Sketch Line does not exist"};
    const auto line = std::find_if(sketch->entities().begin(), sketch->entities().end(),
        [&lineId](const auto& entity) {
            return std::visit([&](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                return std::is_same_v<T, cad::parametric::SketchLine> && item.id == lineId;
            }, entity);
        });
    if (line == sketch->entities().end()) return {false, {}, "Angle constraint requires a Line"};
    if (!std::isfinite(radians)) return {false, {}, "Angle constraint must be finite"};
    return pushConstraint(body_, undoStack_, sketch,
        cad::parametric::AngleConstraint{lineId, radians, anchorStart, {}});
}

ModelingResult addLineRelationConstraint(
    cad::parametric::Body& body, QUndoStack& stack, const std::string& sketchId,
    const std::string& firstLineId, const std::string& secondLineId,
    const bool anchorStart, const bool perpendicular)
{
    const auto sketch = sketchFor(body, sketchId);
    if (!sketch || !hasEntity(*sketch, firstLineId) || !hasEntity(*sketch, secondLineId))
        return {false, {}, "Line relation references missing entity"};
    if (firstLineId == secondLineId) return {false, {}, "Line relation requires two different Lines"};
    const auto isLine = [&sketch](const std::string& id) {
        return std::any_of(sketch->entities().begin(), sketch->entities().end(),
            [&id](const auto& entity) {
                return std::visit([&](const auto& item) {
                    using T = std::decay_t<decltype(item)>;
                    return std::is_same_v<T, cad::parametric::SketchLine> && item.id == id;
                }, entity);
            });
    };
    if (!isLine(firstLineId) || !isLine(secondLineId))
        return {false, {}, "Parallel and Perpendicular require Lines"};
    return pushConstraint(body, stack, sketch, perpendicular
        ? cad::parametric::SketchConstraint{cad::parametric::PerpendicularConstraint{firstLineId, secondLineId, anchorStart, {}}}
        : cad::parametric::SketchConstraint{cad::parametric::ParallelConstraint{firstLineId, secondLineId, anchorStart, {}}});
}

ModelingResult ModelingController::addSketchParallel(
    const std::string& sketchId, const std::string& firstLineId,
    const std::string& secondLineId, const bool anchorStart)
{ return addLineRelationConstraint(body_, undoStack_, sketchId, firstLineId, secondLineId, anchorStart, false); }

ModelingResult ModelingController::addSketchPerpendicular(
    const std::string& sketchId, const std::string& firstLineId,
    const std::string& secondLineId, const bool anchorStart)
{ return addLineRelationConstraint(body_, undoStack_, sketchId, firstLineId, secondLineId, anchorStart, true); }

ModelingResult ModelingController::addSketchAngleBetweenLines(
    const std::string& sketchId, const std::string& referenceLineId,
    const std::string& dependentLineId, const double radians, const bool anchorStart)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch || !std::isfinite(radians)) return {false, {}, "AngleBetweenLines value must be finite"};
    const auto isLine = [&sketch](const std::string& id) {
        return std::any_of(sketch->entities().begin(), sketch->entities().end(),
            [&id](const auto& entity) { return std::visit([&](const auto& item) {
                return std::is_same_v<std::decay_t<decltype(item)>, cad::parametric::SketchLine> && item.id == id;
            }, entity); });
    };
    if (referenceLineId == dependentLineId || !isLine(referenceLineId) || !isLine(dependentLineId))
        return {false, {}, "AngleBetweenLines requires two different Lines"};
    return pushConstraint(body_, undoStack_, sketch,
        cad::parametric::SketchConstraint{cad::parametric::AngleBetweenLinesConstraint{
            referenceLineId, dependentLineId, radians, anchorStart, {}}});
}

ModelingResult ModelingController::addSketchTangent(
    const std::string& sketchId, const std::string& firstEntityId, const std::string& secondEntityId)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch || firstEntityId == secondEntityId) return {false, {}, "Tangent requires two different entities"};
    return pushConstraint(body_, undoStack_, sketch,
        cad::parametric::SketchConstraint{cad::parametric::TangentConstraint{firstEntityId, secondEntityId, {}}});
}

ModelingResult ModelingController::addSketchEqual(
    const std::string& sketchId, const std::string& referenceEntityId, const std::string& dependentEntityId)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch || referenceEntityId == dependentEntityId) return {false, {}, "Equal requires two different entities"};
    return pushConstraint(body_, undoStack_, sketch,
        cad::parametric::SketchConstraint{cad::parametric::EqualConstraint{referenceEntityId, dependentEntityId, {}}});
}

namespace {
ModelingResult updateDimensionalConstraint(
    cad::parametric::Body& body, QUndoStack& stack, const std::string& sketchId,
    const std::string& constraintId, const double value, const bool radius)
{
    const auto sketch = sketchFor(body, sketchId);
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    if (!std::isfinite(value) || value <= 1.0e-7)
        return {false, {}, radius ? "Radius constraint value must be finite and positive"
                                   : "Distance constraint value must be finite and positive"};
    auto before = sketch->constraints();
    auto after = before;
    bool found = false;
    for (auto& constraint : after) {
        if (radius) {
            if (auto* item = std::get_if<cad::parametric::RadiusConstraint>(&constraint);
                item && item->id == constraintId) { item->value = value; found = true; }
        } else if (auto* item = std::get_if<cad::parametric::DistanceConstraint>(&constraint);
                   item && item->id == constraintId) { item->value = value; found = true; }
    }
    if (!found) return {false, {}, "Dimensional constraint does not exist"};
    const auto solved = cad::operations::SketchConstraintSolver::solve(sketch->entities(), after);
    if (solved.status != cad::operations::SolveStatus::Solved)
        return {false, {}, solved.error};
    try {
        stack.push(new cad::commands::UpdateSketchConstraintCommand(
            body, sketch, sketch->entities(), solved.entities, before, after));
        return {true, sketchId, {}};
    } catch (const std::exception& error) { return failure(error); }
}
}

ModelingResult ModelingController::updateSketchDistance(
    const std::string& sketchId, const std::string& constraintId, const double value)
{ return updateDimensionalConstraint(body_, undoStack_, sketchId, constraintId, value, false); }

ModelingResult ModelingController::updateSketchRadius(
    const std::string& sketchId, const std::string& constraintId, const double value)
{ return updateDimensionalConstraint(body_, undoStack_, sketchId, constraintId, value, true); }

ModelingResult ModelingController::updateSketchChamfer(
    const std::string& sketchId, const std::string& constraintId,
    const double firstDistance, const double secondDistance, const double angleRadians)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch || !std::isfinite(firstDistance) || firstDistance <= 1.0e-7)
        return {false, {}, "Chamfer distance must be finite and positive"};
    auto before = sketch->constraints();
    auto after = before;
    bool found = false;
    for (auto& constraint : after) {
        auto* item = std::get_if<cad::parametric::ChamferConstraint>(&constraint);
        if (!item || item->id != constraintId) continue;
        if (item->mode == cad::parametric::SketchChamferMode::TwoDistances
            && (!std::isfinite(secondDistance) || secondDistance <= 1.0e-7))
            return {false, {}, "Chamfer second distance must be finite and positive"};
        if (item->mode == cad::parametric::SketchChamferMode::DistanceAngle
            && (!std::isfinite(angleRadians) || angleRadians <= 1.0e-7
                || angleRadians >= 3.14159265358979323846 - 1.0e-7))
            return {false, {}, "Chamfer angle is outside the valid range"};
        item->firstDistance = firstDistance;
        item->secondDistance = secondDistance;
        item->angleRadians = angleRadians;
        found = true;
    }
    if (!found) return {false, {}, "Chamfer constraint does not exist"};
    const auto solved = cad::operations::SketchConstraintSolver::solve(sketch->entities(), after);
    if (solved.status != cad::operations::SolveStatus::Solved)
        return {false, {}, solved.error};
    try {
        undoStack_.push(new cad::commands::UpdateSketchConstraintCommand(
            body_, sketch, sketch->entities(), solved.entities, before, after));
        return {true, sketchId, {}};
    } catch (const std::exception& error) { return failure(error); }
}

ModelingResult updatePointDistanceConstraint(
    cad::parametric::Body& body, QUndoStack& stack, const std::string& sketchId,
    const std::string& constraintId, const double value, const bool horizontal)
{
    const auto sketch = sketchFor(body, sketchId);
    if (!sketch || !std::isfinite(value)) return {false, {}, "Point distance must be finite"};
    auto before = sketch->constraints();
    auto after = before;
    bool found = false;
    for (auto& constraint : after) {
        if (horizontal) {
            if (auto* item = std::get_if<cad::parametric::HorizontalDistanceConstraint>(&constraint);
                item && item->id == constraintId) { item->value = value; found = true; }
        } else if (auto* item = std::get_if<cad::parametric::VerticalDistanceConstraint>(&constraint);
                   item && item->id == constraintId) { item->value = value; found = true; }
    }
    if (!found) return {false, {}, "Point distance constraint does not exist"};
    const auto solved = cad::operations::SketchConstraintSolver::solve(sketch->entities(), after);
    if (solved.status != cad::operations::SolveStatus::Solved) return {false, {}, solved.error};
    try {
        stack.push(new cad::commands::UpdateSketchConstraintCommand(
            body, sketch, sketch->entities(), solved.entities, before, after));
        return {true, sketchId, {}};
    } catch (const std::exception& error) { return failure(error); }
}

ModelingResult ModelingController::updateSketchHorizontalDistance(
    const std::string& sketchId, const std::string& constraintId, const double value)
{ return updatePointDistanceConstraint(body_, undoStack_, sketchId, constraintId, value, true); }

ModelingResult ModelingController::updateSketchVerticalDistance(
    const std::string& sketchId, const std::string& constraintId, const double value)
{ return updatePointDistanceConstraint(body_, undoStack_, sketchId, constraintId, value, false); }

ModelingResult ModelingController::updateSketchAngle(
    const std::string& sketchId, const std::string& constraintId, const double radians)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch || !std::isfinite(radians)) return {false, {}, "Angle constraint must be finite"};
    auto before = sketch->constraints();
    auto after = before;
    bool found = false;
    for (auto& constraint : after) if (auto* item = std::get_if<cad::parametric::AngleConstraint>(&constraint);
        item && item->id == constraintId) { item->radians = radians; found = true; }
    if (!found) return {false, {}, "Angle constraint does not exist"};
    const auto solved = cad::operations::SketchConstraintSolver::solve(sketch->entities(), after);
    if (solved.status != cad::operations::SolveStatus::Solved) return {false, {}, solved.error};
    try {
        undoStack_.push(new cad::commands::UpdateSketchConstraintCommand(
            body_, sketch, sketch->entities(), solved.entities, before, after));
        return {true, sketchId, {}};
    } catch (const std::exception& error) { return failure(error); }
}

ModelingResult ModelingController::updateSketchAngleBetweenLines(
    const std::string& sketchId, const std::string& constraintId, const double radians)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch || !std::isfinite(radians)) return {false, {}, "AngleBetweenLines value must be finite"};
    auto before = sketch->constraints();
    auto after = before;
    bool found = false;
    for (auto& constraint : after) {
        if (auto* item = std::get_if<cad::parametric::AngleBetweenLinesConstraint>(&constraint);
            item && item->id == constraintId) { item->angleRadians = radians; found = true; }
    }
    if (!found) return {false, {}, "AngleBetweenLines constraint does not exist"};
    const auto solved = cad::operations::SketchConstraintSolver::solve(sketch->entities(), after);
    if (solved.status != cad::operations::SolveStatus::Solved) return {false, {}, solved.error};
    try {
        undoStack_.push(new cad::commands::UpdateSketchConstraintCommand(
            body_, sketch, sketch->entities(), solved.entities, before, after));
        return {true, sketchId, {}};
    } catch (const std::exception& error) { return failure(error); }
}

ModelingResult ModelingController::removeSketchConstraint(
    const std::string& sketchId, const std::string& constraintId)
{
    const auto sketch = sketchFor(body_, sketchId);
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    const auto before = sketch->constraints();
    auto after = before;
    const auto it = std::find_if(after.begin(), after.end(), [&constraintId](const auto& constraint) {
        return std::visit([&constraintId](const auto& item) { return item.id == constraintId; }, constraint);
    });
    if (it == after.end()) return {false, {}, "Sketch constraint does not exist"};
    after.erase(it);
    const auto solved = cad::operations::SketchConstraintSolver::solve(sketch->entities(), after);
    if (solved.status != cad::operations::SolveStatus::Solved)
        return {false, {}, solved.error};
    try {
        undoStack_.push(new cad::commands::RemoveSketchConstraintCommand(
            body_, sketch, sketch->entities(), solved.entities, before, after));
        return {true, sketchId, {}};
    } catch (const std::exception& error) { return failure(error); }
}

ModelingResult ModelingController::createPrimitive(const PrimitiveKind kind)
{
    switch (kind) {
    case PrimitiveKind::Box: return createBox();
    case PrimitiveKind::Cylinder: return createCylinder();
    case PrimitiveKind::Sketch: return createSketch();
    case PrimitiveKind::Cone:
        return addFeature(std::make_shared<cad::parametric::ConeFeature>(
            id("cone"), 30.0, 15.0, 60.0));
    case PrimitiveKind::Sphere:
        return addFeature(std::make_shared<cad::parametric::SphereFeature>(
            id("sphere"), 35.0));
    case PrimitiveKind::Torus:
        return addFeature(std::make_shared<cad::parametric::TorusFeature>(
            id("torus"), 45.0, 12.0));
    case PrimitiveKind::Hexagon:
        return addFeature(std::make_shared<cad::parametric::HexagonFeature>(
            id("hexagon"), 30.0, 12.0));
    }
    return {false, {}, "Unsupported primitive"};
}

ModelingResult ModelingController::createFace(
    const std::vector<std::string>& selection
)
{
    if (selection.size() != 1) return {false, {}, "Select exactly one Sketch"};
    const auto source = body_.findFeature(selection.front());
    if (!source || source->role() != cad::parametric::FeatureRole::Sketch) {
        return {false, {}, "Selected feature is not a Sketch"};
    }
    try {
        return addFeature(std::make_shared<cad::parametric::FaceFeature>(
            id("face"), source));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createExtrude(
    const std::vector<std::string>& selection
)
{
    if (selection.size() != 1) return {false, {}, "Select exactly one Face"};
    const auto profile = body_.findFeature(selection.front());
    if (!profile || profile->role() != cad::parametric::FeatureRole::Face) {
        return {false, {}, "Selected feature is not a Face"};
    }
    try {
        return addFeature(std::make_shared<cad::parametric::ExtrudeFeature>(
            id("extrude"), profile, gp_Vec(0, 0, 20)));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createExtrudeFromSketch(
    const SelectionSnapshot& selection,
    const double distance,
    const bool reversed)
{
    if (selection.selectedObjectIds().size() != 1 || selection.items.size() != 1
        || selection.items.front().kind != SelectionKind::Object) {
        return {false, {}, "Extrude requires exactly one Sketch object"};
    }
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        body_.findFeature(selection.items.front().featureId));
    if (!sketch) return {false, {}, "Selected feature is not a Sketch"};
    std::string profileError;
    if (!validateSketchProfile(*sketch, profileError)) return {false, {}, profileError};
    try {
        return addFeature(std::make_shared<cad::parametric::ExtrudeFeature>(
            id("extrude"), sketch, distance, reversed));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createPocketFromSketch(
    const SelectionSnapshot& selection,
    const double depth)
{
    if (selection.selectedObjectIds().size() != 1 || selection.items.size() != 1
        || selection.items.front().kind != SelectionKind::Object) {
        return {false, {}, "Pocket requires exactly one Sketch object"};
    }
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        body_.findFeature(selection.items.front().featureId));
    if (!sketch) return {false, {}, "Selected feature is not a Sketch"};
    if (sketch->supportType() != cad::parametric::SketchSupportType::Face
        || !sketch->faceReference()) {
        return {false, {}, "Pocket requires a Face-attached Sketch"};
    }
    const auto target = body_.findFeature(sketch->faceReference()->featureId);
    if (!target) return {false, {}, "Pocket target feature does not exist"};
    std::string profileError;
    if (!validateSketchProfile(*sketch, profileError)) return {false, {}, profileError};
    try {
        return addFeature(std::make_shared<cad::parametric::PocketFeature>(
            id("pocket"), target, sketch, depth));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createShell(
    const SelectionSnapshot& selection,
    const double thickness)
{
    const auto featureIds = selection.featureIds();
    if (featureIds.size() != 1) {
        return {false, {}, "Shell requires one solid and its opening Faces"};
    }
    const auto source = body_.findFeature(featureIds.front());
    if (!source) return {false, {}, "Shell source feature does not exist"};

    try {
        SelectionResolver resolver(body_);
        std::vector<cad::topology::TopologicalReference> references;
        for (const auto& item : selection.items) {
            if (item.kind != SelectionKind::Face || !item.subshapeIndex
                || item.featureId != source->id()) continue;
            const auto face = resolver.resolve(item);
            if (!face || face->ShapeType() != TopAbs_FACE) {
                return {false, {}, "Shell opening Face is invalid"};
            }
            references.push_back(cad::topology::TopologicalSignatureBuilder::createReference(
                source->id(), source->shape(), *face));
        }
        if (references.empty()) {
            TopTools_IndexedMapOfShape faces;
            TopExp::MapShapes(source->shape(), TopAbs_FACE, faces);
            if (faces.Extent() == 0) return {false, {}, "Shell source has no removable Face"};
            references.push_back(cad::topology::TopologicalSignatureBuilder::createReference(
                source->id(), source->shape(), TopoDS::Face(faces.FindKey(1))));
        }
        return addFeature(std::make_shared<cad::parametric::ShellFeature>(
            id("shell"), source, std::move(references), thickness));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createRevolve(
    const SelectionSnapshot& selection,
    cad::parametric::RevolveAxisDefinition axis,
    const double angleDegrees)
{
    if (selection.items.size() != 1 || selection.items.front().kind != SelectionKind::Object) {
        return {false, {}, "Revolve requires exactly one Sketch object"};
    }
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        body_.findFeature(selection.items.front().featureId));
    if (!sketch) return {false, {}, "Revolve requires a Sketch profile"};
    std::string profileError;
    if (!validateSketchProfile(*sketch, profileError)) return {false, {}, profileError};
    if (!std::isfinite(angleDegrees) || std::abs(angleDegrees) <= 1.0e-9
        || std::abs(angleDegrees) > 360.0) {
        return {false, {}, "Revolve angle must be between -360 and 360 degrees"};
    }
    if (axis.type == cad::parametric::RevolveAxisType::SketchLine) {
        if (axis.sketchFeatureId != sketch->id() || axis.sketchLineId.empty()) {
            return {false, {}, "Revolve Sketch line axis is invalid"};
        }
        const auto found = std::find_if(sketch->entities().begin(), sketch->entities().end(),
            [&axis](const auto& entity) {
                const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
                return line && line->id == axis.sketchLineId;
            });
        if (found == sketch->entities().end()) {
            return {false, {}, "Revolve Sketch line axis could not be resolved"};
        }
    }
    if (axis.type == cad::parametric::RevolveAxisType::ModelEdge) {
        const auto axisSource = body_.findFeature(axis.edgeFeatureId);
        if (!axisSource || !axis.edgeReference) {
            return {false, {}, "Revolve model Edge axis is invalid"};
        }
        if (axis.edgeReference->featureId != axisSource->id()
            || axis.edgeReference->kind != cad::topology::TopologicalKind::Edge) {
            return {false, {}, "Revolve model Edge reference is invalid"};
        }
        try {
            const auto resolved = cad::topology::TopologicalReferenceResolver::resolveAgainstShape(
                *axis.edgeReference, axisSource->shape());
            if (resolved.status != cad::topology::ResolveStatus::Resolved || !resolved.shape
                || resolved.shape->ShapeType() != TopAbs_EDGE) {
                return {false, {}, "Revolve model Edge axis could not be resolved"};
            }
            BRepAdaptor_Curve curve(TopoDS::Edge(*resolved.shape));
            if (curve.GetType() != GeomAbs_Line)
                return {false, {}, "Revolve model Edge axis must be linear"};
        } catch (const std::exception& error) {
            return failure(error);
        }
        return addFeature(std::make_shared<cad::parametric::RevolveFeature>(
            id("revolve"), sketch, std::move(axis),
            angleDegrees * std::acos(-1.0) / 180.0, axisSource));
    }
    try {
        return addFeature(std::make_shared<cad::parametric::RevolveFeature>(
            id("revolve"), sketch, std::move(axis),
            angleDegrees * std::acos(-1.0) / 180.0));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createSweep(
    const SelectionSnapshot& profileSelection,
    const SelectionSnapshot& pathSelection)
{
    if (profileSelection.items.size() != 1
        || profileSelection.items.front().kind != SelectionKind::Object) {
        return {false, {}, "Sweep requires exactly one Sketch object"};
    }
    const auto profile = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        body_.findFeature(profileSelection.items.front().featureId));
    if (!profile) return {false, {}, "Sweep requires a Sketch profile"};
    std::string profileError;
    if (!validateSketchProfile(*profile, profileError)) return {false, {}, profileError};
    if (pathSelection.items.size() != 1) {
        return {false, {}, "Sweep requires exactly one path Edge or Sketch"};
    }
    if (pathSelection.items.front().kind == SelectionKind::Object) {
        const auto pathSketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            body_.findFeature(pathSelection.items.front().featureId));
        if (!pathSketch) return {false, {}, "Sweep Sketch path does not exist"};
        if (pathSketch->id() == profile->id())
            return {false, {}, "Sweep profile and path Sketch must be different"};
        try {
            const auto path = cad::operations::SketchPathBuilder::build(*pathSketch);
            return addFeature(std::make_shared<cad::parametric::SweepFeature>(
                id("sweep"), pathSketch, path.entityIds, profile));
        } catch (const std::exception& error) {
            return failure(error);
        }
    }
    if (pathSelection.items.front().kind != SelectionKind::Edge
        || !pathSelection.items.front().subshapeIndex) {
        return {false, {}, "Sweep requires exactly one path Edge or Sketch"};
    }
    const auto& item = pathSelection.items.front();
    const auto owner = body_.findFeature(item.featureId);
    if (!owner) return {false, {}, "Sweep path owner does not exist"};
    try {
        SelectionResolver resolver(body_);
        const auto edge = resolver.resolve(item);
        if (!edge || edge->ShapeType() != TopAbs_EDGE)
            return {false, {}, "Sweep path selection is not an Edge"};
        const auto reference = cad::topology::TopologicalSignatureBuilder::createReference(
            owner->id(), owner->shape(), *edge);
        return addFeature(std::make_shared<cad::parametric::SweepFeature>(
            id("sweep"), owner, reference, profile));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::resolveRevolveAxis(
    const std::string& profileFeatureId,
    const SelectionSnapshot& selection,
    cad::parametric::RevolveAxisDefinition& axis) const
{
    if (selection.items.size() != 1
        || selection.items.front().kind != SelectionKind::Edge
        || !selection.items.front().subshapeIndex) {
        return {false, {}, "Select exactly one Edge for the Revolve axis"};
    }
    const auto& item = selection.items.front();
    const auto owner = body_.findFeature(item.featureId);
    const auto profile = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        body_.findFeature(profileFeatureId));
    if (!owner || !profile) return {false, {}, "Revolve axis source is unavailable"};
    SelectionResolver resolver(body_);
    const auto selected = resolver.resolve(item);
    if (!selected || selected->ShapeType() != TopAbs_EDGE)
        return {false, {}, "Selected Revolve axis is not an Edge"};
    try {
        const TopoDS_Edge edge = TopoDS::Edge(*selected);
        BRepAdaptor_Curve curve(edge);
        if (curve.GetType() != GeomAbs_Line)
            return {false, {}, "Selected Revolve axis Edge must be linear"};
        const gp_Pnt start = curve.Value(curve.FirstParameter());
        const gp_Pnt end = curve.Value(curve.LastParameter());
        if (start.Distance(end) <= 1.0e-9)
            return {false, {}, "Revolve axis line has zero length"};

        if (owner->id() == profile->id()) {
            for (const auto& entity : profile->entities()) {
                const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
                if (!line) continue;
                const auto sketchStart = sketchWorldPoint(profile->currentFrame(), line->start);
                const auto sketchEnd = sketchWorldPoint(profile->currentFrame(), line->end);
                if (!sameSegment(start, end, sketchStart, sketchEnd)) continue;
                axis = {};
                axis.type = cad::parametric::RevolveAxisType::SketchLine;
                axis.sketchFeatureId = profile->id();
                axis.sketchLineId = line->id;
                return {true, {}, {}};
            }
            return {false, {}, "Selected Sketch Edge is not a Sketch line entity"};
        }

        axis = {};
        axis.type = cad::parametric::RevolveAxisType::ModelEdge;
        axis.edgeFeatureId = owner->id();
        axis.edgeReference = cad::topology::TopologicalSignatureBuilder::createReference(
            owner->id(), owner->shape(), edge);
        return {true, {}, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::updateRevolveAxis(
    const std::string& featureId,
    cad::parametric::RevolveAxisDefinition axis)
{
    const auto feature = std::dynamic_pointer_cast<cad::parametric::RevolveFeature>(
        body_.findFeature(featureId));
    if (!feature) return {false, {}, "Revolve feature does not exist"};
    try {
        cad::parametric::ParametricFeature::Ptr source;
        if (axis.type == cad::parametric::RevolveAxisType::ModelEdge) {
            source = body_.findFeature(axis.edgeFeatureId);
            if (!source) return {false, {}, "Revolve axis feature does not exist"};
        }
        using AxisState = std::pair<cad::parametric::RevolveAxisDefinition,
            cad::parametric::ParametricFeature::Ptr>;
        const AxisState before{feature->axisDefinition(), feature->axisSource()};
        const AxisState after{std::move(axis), source};
        undoStack_.push(new cad::commands::ChangeFeatureParameterCommand<
            cad::parametric::RevolveFeature, AxisState>(
                body_, feature, before, after,
                [](auto& value, const AxisState& state) {
                    value.setAxisDefinition(state.first, state.second);
                }, "Change Revolve Axis"));
        return {true, featureId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createLinearPattern(
    const std::vector<std::string>& selection
)
{
    return createLinearPattern(selection, gp_Vec(1.0, 0.0, 0.0), 100.0, 2, true);
}

ModelingResult ModelingController::createLinearPattern(
    const std::vector<std::string>& selection, const gp_Vec direction,
    const double spacing, const int count, const bool includeSource)
{
    if (selection.size() != 1) return {false, {}, "Select exactly one source feature"};
    const auto source = body_.findFeature(selection.front());
    if (!source) return {false, {}, "Pattern source does not exist"};
    try {
        return addFeature(std::make_shared<cad::parametric::LinearPatternFeature>(
            id("linear-pattern"),
            std::vector<cad::parametric::ParametricFeature::Ptr>{source},
            direction, spacing, count, includeSource));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createPathPattern(
    const std::vector<std::string>& selection
)
{
    return createPathPattern(selection, 100.0, 2,
        cad::parametric::PathPatternDistribution::FitCount,
        cad::parametric::PathPatternOrientation::Fixed, 0.0, 0.0, true);
}

ModelingResult ModelingController::createPathPattern(
    const std::vector<std::string>& selection, const double spacing, const int count,
    const cad::parametric::PathPatternDistribution distribution,
    const cad::parametric::PathPatternOrientation orientation,
    const double startOffset, const double endOffset, const bool includeSource)
{
    if (selection.size() != 2) return {false, {}, "Select source and path features"};
    const auto source = body_.findFeature(selection[0]);
    const auto path = body_.findFeature(selection[1]);
    if (!source || !path) return {false, {}, "Pattern source or path does not exist"};
    try {
        return addFeature(std::make_shared<cad::parametric::PathPatternFeature>(
            id("path-pattern"),
            std::vector<cad::parametric::ParametricFeature::Ptr>{source}, path,
            spacing, count, distribution, orientation, startOffset, endOffset, includeSource));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

namespace {

struct EdgeSelectionInput
{
    std::string sourceId;
    std::vector<cad::topology::TopologicalReference> references;
};

std::optional<EdgeSelectionInput> resolveEdgeSelection(
    const SelectionSnapshot& selection,
    const cad::parametric::Body& body,
    std::string& error
)
{
    if (selection.items.empty()) {
        error = "Select at least one edge";
        return std::nullopt;
    }

    std::string sourceId;
    cad::application::SelectionResolver resolver(body);
    std::vector<cad::topology::TopologicalReference> references;
    for (const auto& item : selection.items) {
        if (item.kind != SelectionKind::Edge || !item.subshapeIndex) {
            error = "Fillet and Chamfer require edge-only selection";
            return std::nullopt;
        }
        if (sourceId.empty()) sourceId = item.featureId;
        if (item.featureId != sourceId) {
            error = "Selected edges must belong to the same feature";
            return std::nullopt;
        }
        const auto shape = resolver.resolve(item);
        if (!shape || shape->ShapeType() != TopAbs_EDGE) {
            error = "Selected edge reference is invalid for the current topology";
            return std::nullopt;
        }
        references.push_back(cad::topology::TopologicalSignatureBuilder::createReference(
            sourceId, body.findFeature(sourceId)->shape(), *shape));
    }
    return EdgeSelectionInput{sourceId, std::move(references)};
}

} // namespace

ModelingResult ModelingController::createFillet(
    const SelectionSnapshot& selection,
    const double radius
)
{
    std::string error;
    const auto input = resolveEdgeSelection(selection, body_, error);
    if (!input) return {false, {}, error};
    const auto source = body_.findFeature(input->sourceId);
    try {
        return addFeature(std::make_shared<cad::parametric::FilletFeature>(
            id("fillet"), source, input->references, radius));
    } catch (const std::exception& exception) {
        return failure(exception);
    }
}

ModelingResult ModelingController::createChamfer(
    const SelectionSnapshot& selection,
    const double distance
)
{
    std::string error;
    const auto input = resolveEdgeSelection(selection, body_, error);
    if (!input) return {false, {}, error};
    const auto source = body_.findFeature(input->sourceId);
    try {
        return addFeature(std::make_shared<cad::parametric::ChamferFeature>(
            id("chamfer"), source, input->references, distance));
    } catch (const std::exception& exception) {
        return failure(exception);
    }
}

ModelingResult ModelingController::createChamfer(
    const std::vector<cad::topology::TopologicalReference>& references,
    const double distance)
{
    if (references.empty()) return {false, {}, "Chamfer requires at least one edge reference"};
    if (!std::isfinite(distance) || distance <= 0.0)
        return {false, {}, "Chamfer distance must be positive and finite"};
    const auto sourceId = references.front().featureId;
    const auto source = body_.findFeature(sourceId);
    if (!source) return {false, {}, "Chamfer source feature does not exist"};
    std::vector<TopoDS_Edge> edges;
    cad::topology::TopologicalReferenceResolver resolver(body_);
    for (const auto& reference : references) {
        if (reference.featureId != sourceId
            || reference.kind != cad::topology::TopologicalKind::Edge)
            return {false, {}, "Chamfer edges must belong to one feature and be Edges"};
        const auto resolved = resolver.resolve(reference);
        if (resolved.status != cad::topology::ResolveStatus::Resolved || !resolved.shape
            || resolved.shape->ShapeType() != TopAbs_EDGE) {
            return {false, {}, resolved.status == cad::topology::ResolveStatus::Ambiguous
                ? "Chamfer edge reference is ambiguous"
                : "Chamfer edge reference could not be resolved"};
        }
        edges.push_back(TopoDS::Edge(*resolved.shape));
    }
    try {
        return addFeature(std::make_shared<cad::parametric::ChamferFeature>(
            id("chamfer"), source, edges, distance));
    } catch (const std::exception& exception) {
        return failure(exception);
    }
}

ModelingResult ModelingController::pushPull(
    const std::string& targetId,
    const int faceIndex,
    const gp_Vec& normal,
    const double distance
)
{
    const auto source = body_.findFeature(targetId);
    if (!source) return {false, {}, "Push/Pull target does not exist"};
    try {
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(source->shape(), TopAbs_FACE, faces);
        if (faceIndex <= 0 || faceIndex > faces.Extent())
            return {false, {}, "Push/Pull face reference is invalid"};
        const auto reference = cad::topology::TopologicalSignatureBuilder::createReference(
            source->id(), source->shape(), faces(faceIndex));
        return pushPull(targetId, reference, normal, distance);
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::pushPull(
    const std::string& targetId,
    const cad::topology::TopologicalReference& faceReference,
    const gp_Vec& normal,
    const double distance
)
{
    const auto source = body_.findFeature(targetId);
    if (!source) return {false, {}, "Push/Pull target does not exist"};
    try {
        if (faceReference.featureId != source->id()
            || faceReference.kind != cad::topology::TopologicalKind::Face) {
            return {false, {}, "Push/Pull face reference does not belong to the target"};
        }
        return addFeature(std::make_shared<cad::parametric::PushPullFeature>(
            id("pushpull"), source, faceReference, normal, distance));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::transformFeature(
    const std::string& featureId,
    const gp_Trsf& before,
    const gp_Trsf& after
)
{
    try {
        undoStack_.push(new cad::commands::TransformFeatureCommand(
            body_, featureId, before, after));
        return {true, featureId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::transformFeatureDelta(
    const std::string& featureId,
    const gp_Trsf& delta
)
{
    const auto feature = body_.findFeature(featureId);
    if (!feature) return {false, {}, "Transform target does not exist"};
    gp_Trsf after = delta;
    after.Multiply(feature->placement());
    return transformFeature(featureId, feature->placement(), after);
}

ModelingResult ModelingController::createTransform(
    const std::vector<std::string>& featureIds,
    const cad::parametric::TransformOperation operation,
    const cad::parametric::TransformResultMode resultMode,
    const gp_Trsf& transformation
)
{
    if (featureIds.empty()) return {false, {}, "Transform requires at least one source feature"};
    std::vector<cad::parametric::ParametricFeature::Ptr> sources;
    sources.reserve(featureIds.size());
    std::vector<std::string> uniqueIds;
    uniqueIds.reserve(featureIds.size());
    for (const auto& featureId : featureIds) {
        if (std::find(uniqueIds.begin(), uniqueIds.end(), featureId) != uniqueIds.end())
            continue;
        const auto source = body_.findFeature(featureId);
        if (!source) return {false, {}, "Transform source does not exist: " + featureId};
        if (source->shape().IsNull())
            return {false, {}, "Transform source has no valid shape: " + featureId};
        uniqueIds.push_back(featureId);
        sources.push_back(source);
    }
    if (sources.empty()) return {false, {}, "Transform requires at least one source feature"};

    try {
        const auto transformId = id("transform");
        auto transform = std::make_shared<cad::parametric::TransformFeature>(
            transformId, sources, operation, resultMode, transformation);
        ModelTransaction transaction(body_, undoStack_, "Transform Features");
        undoStack_.push(new cad::commands::AddFeatureCommand(
            body_, transform, QStringLiteral("Transform Features")));
        if (resultMode == cad::parametric::TransformResultMode::ReplaceResult) {
            std::vector<std::string> hiddenIds;
            for (const auto& source : sources)
                collectFeatureAndDependencies(source, hiddenIds);
            std::vector<bool> before;
            before.reserve(hiddenIds.size());
            for (const auto& featureId : hiddenIds)
                before.push_back(body_.findFeature(featureId)->userVisible());
            undoStack_.push(new cad::commands::SetFeatureVisibilityCommand(
                body_, hiddenIds, before,
                std::vector<bool>(hiddenIds.size(), false),
                QStringLiteral("Replace Transformed Result")));
        }
        if (!transaction.commit()) return {false, {}, body_.lastError()};
        return {true, transformId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::transformComponent(
    const std::string& componentId,
    const cad::parametric::TransformOperation operation,
    const cad::parametric::TransformResultMode resultMode,
    const gp_Trsf& transformation)
{
    std::string resolveError;
    const auto resolved = cad::parametric::ComponentResolver::resolve(
        body_, componentId, resolveError);
    if (!resolved) return componentFailure(resolveError);
    const auto source = body_.findFeature(resolved->component.sourceFeatureId);
    if (!source) return componentFailure("Component source feature does not exist");
    try {
        const auto featureId = id("component-transform");
        auto transformed = std::make_shared<cad::parametric::ComponentTransformFeature>(
            featureId, source, resolved->component, operation, resultMode, transformation);
        if (!transformed->recompute()) return componentFailure(transformed->error());
        const auto resultComponentId = featureId + ":component";
        auto resultComponent = componentFromShape(transformed->shape(), resultComponentId, featureId);
        resultComponent.parentComponentId = componentId;

        auto before = componentMetadata(body_);
        auto after = before;
        auto sourceIterator = std::find_if(after.begin(), after.end(),
            [&componentId](const ModelComponent& component) { return component.id == componentId; });
        if (sourceIterator == after.end()) return componentFailure("Component metadata is unavailable");
        if (resultMode == cad::parametric::TransformResultMode::ReplaceResult) {
            sourceIterator->visible = false;
            sourceIterator->replacementComponentIds = {resultComponentId};
        }
        after.push_back(resultComponent);

        ModelTransaction transaction(body_, undoStack_, "Transform Component");
        undoStack_.push(new cad::commands::AddFeatureCommand(body_, transformed,
            QStringLiteral("Transform Component")));
        undoStack_.push(new cad::commands::ReplaceComponentsCommand(body_, before,
            after, QStringLiteral("Update Component Replacement")));
        if (!transaction.commit()) return componentFailure(transaction.error().toStdString());
        return {true, featureId, {}, {resultComponentId}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::splitComponent(
    const std::string& componentId, const gp_Pnt& origin, const gp_Dir& normal)
{
    std::string resolveError;
    const auto resolved = cad::parametric::ComponentResolver::resolve(
        body_, componentId, resolveError);
    if (!resolved) return componentFailure(resolveError);
    const auto source = body_.findFeature(resolved->component.sourceFeatureId);
    if (!source) return componentFailure("Component source feature does not exist");
    try {
        const auto negativeId = id("component-split-negative");
        const auto positiveId = id("component-split-positive");
        auto negative = std::make_shared<cad::parametric::ComponentSplitFeature>(
            negativeId, source, resolved->component, origin, normal,
            cad::parametric::ComponentSplitSide::Negative);
        auto positive = std::make_shared<cad::parametric::ComponentSplitFeature>(
            positiveId, source, resolved->component, origin, normal,
            cad::parametric::ComponentSplitSide::Positive);
        if (!negative->recompute()) return componentFailure(negative->error());
        if (!positive->recompute()) return componentFailure(positive->error());
        BRepCheck_Analyzer negativeAnalyzer(negative->shape());
        BRepCheck_Analyzer positiveAnalyzer(positive->shape());
        if (!negativeAnalyzer.IsValid() || !positiveAnalyzer.IsValid())
            return componentFailure("Component split produced an invalid B-Rep");

        const auto negativeComponentId = negativeId + ":component";
        const auto positiveComponentId = positiveId + ":component";
        auto negativeComponent = componentFromShape(negative->shape(), negativeComponentId, negativeId);
        auto positiveComponent = componentFromShape(positive->shape(), positiveComponentId, positiveId);
        negativeComponent.parentComponentId = componentId;
        positiveComponent.parentComponentId = componentId;
        auto before = componentMetadata(body_);
        auto after = before;
        auto sourceIterator = std::find_if(after.begin(), after.end(),
            [&componentId](const ModelComponent& component) { return component.id == componentId; });
        if (sourceIterator == after.end()) return componentFailure("Component metadata is unavailable");
        sourceIterator->visible = false;
        sourceIterator->replacementComponentIds = {negativeComponentId, positiveComponentId};
        after.push_back(negativeComponent);
        after.push_back(positiveComponent);

        ModelTransaction transaction(body_, undoStack_, "Split Component");
        undoStack_.push(new cad::commands::AddFeatureCommand(body_, negative,
            QStringLiteral("Split Component")));
        undoStack_.push(new cad::commands::AddFeatureCommand(body_, positive,
            QStringLiteral("Split Component")));
        undoStack_.push(new cad::commands::ReplaceComponentsCommand(body_, before,
            after, QStringLiteral("Update Component Replacement")));
        if (!transaction.commit()) return componentFailure(transaction.error().toStdString());
        return {true, negativeId, {}, {negativeComponentId, positiveComponentId}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::splitFeature(
    const std::string& wholeFeatureId,
    const std::string& componentFeatureId,
    const double splitCoordinate,
    const gp_Vec& moveNegative)
{
    const auto whole = body_.findFeature(wholeFeatureId);
    const auto component = body_.findFeature(componentFeatureId);
    if (!whole) return {false, {}, "Split whole feature does not exist"};
    if (!component) return {false, {}, "Split component feature does not exist"};
    if (!std::isfinite(splitCoordinate)) return {false, {}, "Split coordinate must be finite"};
    const auto componentTransform = transformToComponent(whole, componentFeatureId);
    if (!componentTransform) {
        return {false, {}, "Split component is not a dependency of the selected result"};
    }
    const auto transformFeature = std::dynamic_pointer_cast<cad::parametric::TransformFeature>(whole);
    if (!transformFeature) {
        return {false, {}, "Split currently requires a composite Transform result"};
    }
    try {
        const auto negativeId = id("split-negative");
        const auto positiveId = id("split-positive");
        const auto movedId = id("transform");
        const auto assemblyId = id("split-assembly");
        auto negative = std::make_shared<cad::parametric::SplitSegmentFeature>(
            negativeId, component, *componentTransform, splitCoordinate,
            cad::parametric::SplitSide::Negative);
        auto positive = std::make_shared<cad::parametric::SplitSegmentFeature>(
            positiveId, component, *componentTransform, splitCoordinate,
            cad::parametric::SplitSide::Positive);
        gp_Trsf translation;
        translation.SetTranslation(moveNegative);
        auto moved = std::make_shared<cad::parametric::TransformFeature>(
            movedId, std::vector<cad::parametric::ParametricFeature::Ptr>{negative},
            cad::parametric::TransformOperation::Translate,
            cad::parametric::TransformResultMode::ReplaceResult, translation);
        auto assembly = std::make_shared<cad::parametric::SplitAssemblyFeature>(
            assemblyId, whole, component, negative, positive, moved,
            *componentTransform, splitCoordinate);

        // Validate the complete candidate before touching the document.  This
        // also preserves the concrete OCCT diagnostic instead of losing it in
        // a deferred Body recompute rollback.
        if (!negative->recompute()) return {false, {}, negative->error()};
        if (!positive->recompute()) return {false, {}, positive->error()};
        if (!moved->recompute()) return {false, {}, moved->error()};
        if (!assembly->recompute()) return {false, {}, assembly->error()};

        ModelTransaction transaction(body_, undoStack_, "Split and Move Wall");
        undoStack_.push(new cad::commands::AddFeatureCommand(body_, negative,
            QStringLiteral("Split Wall Segment")));
        undoStack_.push(new cad::commands::AddFeatureCommand(body_, positive,
            QStringLiteral("Split Wall Segment")));
        undoStack_.push(new cad::commands::AddFeatureCommand(body_, moved,
            QStringLiteral("Move Wall Segment")));
        // The assembly consumes the moved segment by reference in its visible result.
        undoStack_.push(new cad::commands::AddFeatureCommand(body_, assembly,
            QStringLiteral("Assemble Split Wall")));
        undoStack_.push(new cad::commands::SetFeatureVisibilityCommand(
            body_, {wholeFeatureId}, {whole->userVisible()}, {false},
            QStringLiteral("Hide Original Wall Result")));
        undoStack_.push(new cad::commands::SetFeatureVisibilityCommand(
            body_, {negativeId, positiveId, movedId}, {true, true, true},
            {false, false, false}, QStringLiteral("Hide Split Construction Results")));
        if (!transaction.commit()) {
            const auto message = transaction.error().isEmpty()
                ? QStringLiteral("Split transaction failed") : transaction.error();
            return {false, {}, message.toStdString()};
        }
        assembly->setUserVisible(true);
        return {true, assemblyId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::duplicateFeature(const std::string& featureId)
{
    if (featureId == lastCopyFeatureId_ && lastCopyDelta_) {
        return duplicateFeatureWithDelta(featureId, *lastCopyDelta_);
    }
    return duplicateFeatureWithDelta(featureId, gp_Trsf());
}

ModelingResult ModelingController::duplicateFeatureWithDelta(
    const std::string& featureId,
    const gp_Trsf& delta
)
{
    const auto source = body_.findFeature(featureId);
    if (!source) return {false, {}, "Duplicate target does not exist"};

    try {
        const std::string copyId = id("copy");
        const auto copy = source->clone(copyId);
        if (!copy) return {false, {}, "Feature type does not support duplication"};
        gp_Trsf placement = delta;
        placement.Multiply(source->placement());
        copy->setPlacement(placement);
        undoStack_.push(new cad::commands::DuplicateFeatureCommand(
            body_, copy, QStringLiteral("Duplicate Feature")));
        lastCopyFeatureId_ = copyId;
        lastCopyDelta_ = delta;
        return {true, copyId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createBoolean(
    const BooleanKind requestedOperation,
    const std::vector<std::string>& selection,
    const std::string& currentId
)
{
    if (selection.size() != 2) return {false, {}, "Select exactly two features"};
    auto left = body_.findFeature(selection[0]);
    auto right = body_.findFeature(selection[1]);
    if (!left || !right) return {false, {}, "Selected feature does not exist"};
    const auto operation = requestedOperation == BooleanKind::Fuse
        ? cad::parametric::BooleanOperation::Fuse
        : requestedOperation == BooleanKind::Cut
            ? cad::parametric::BooleanOperation::Cut
            : cad::parametric::BooleanOperation::Common;
    if (operation == cad::parametric::BooleanOperation::Cut && !currentId.empty()) {
        const auto current = body_.findFeature(currentId);
        if (!current) return {false, {}, "Selected cutting tool does not exist"};
        if (current->id() == left->id()) std::swap(left, right);
    }
    try {
        return addFeature(std::make_shared<cad::parametric::BooleanFeature>(
            id("boolean"), left, right, operation));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::setFeatureProperty(
    const std::string& featureId,
    const std::string& propertyKey,
    const cad::parametric::PropertyValue& value
)
{
    const auto feature = body_.findFeature(featureId);
    if (!feature) return {false, {}, "Feature does not exist"};
    const auto properties = feature->properties();
    const auto property = std::find_if(properties.begin(), properties.end(),
        [&propertyKey](const auto& candidate) { return candidate.key == propertyKey; });
    if (property == properties.end() || !property->editable) {
        return {false, {}, "Property is not editable"};
    }
    if (property->value.index() != value.index()) {
        return {false, {}, "Property value has an invalid type"};
    }
    if (const auto numeric = std::get_if<double>(&value)) {
        if (property->minimum && *numeric < *property->minimum) {
            return {false, {}, "Property value is below the minimum"};
        }
        if (property->maximum && *numeric > *property->maximum) {
            return {false, {}, "Property value is above the maximum"};
        }
    }
    try {
        undoStack_.push(new cad::commands::ChangeParametricPropertyCommand(
            body_, feature, propertyKey, property->value, value,
            QString("Change ") + QString::fromStdString(feature->name())
                + " " + QString::fromStdString(property->label)));
        return {true, featureId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::deleteFeature(const std::string& featureId)
{
    try {
        if (!body_.findFeature(featureId)) {
            return {false, {}, "Cannot delete unknown feature"};
        }
        undoStack_.push(new cad::commands::RemoveFeatureCommand(body_, featureId));
        return {true, featureId, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::setFeatureVisibility(
    const std::vector<std::string>& featureIds, const bool visible)
{
    if (featureIds.empty()) return {false, {}, "No features selected"};
    std::vector<std::string> ids;
    std::vector<bool> before;
    std::vector<bool> after;
    for (const auto& featureId : featureIds) {
        const auto feature = body_.findFeature(featureId);
        if (!feature) return {false, {}, "Feature does not exist: " + featureId};
        if (std::find(ids.begin(), ids.end(), featureId) != ids.end()) continue;
        ids.push_back(featureId);
        before.push_back(feature->userVisible());
        after.push_back(visible);
    }
    if (ids.empty() || std::equal(before.begin(), before.end(), after.begin())) {
        return {true, {}, {}};
    }
    try {
        undoStack_.push(new cad::commands::SetFeatureVisibilityCommand(
            body_, std::move(ids), std::move(before), std::move(after),
            visible ? "Show Features" : "Hide Features"));
        return {true, {}, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::showAllFeatures()
{
    std::vector<std::string> ids;
    std::vector<bool> before;
    std::vector<bool> after;
    for (const auto& feature : body_.features()) {
        ids.push_back(feature->id());
        before.push_back(feature->userVisible());
        after.push_back(true);
    }
    if (ids.empty() || std::equal(before.begin(), before.end(), after.begin())) {
        return {true, {}, {}};
    }
    try {
        undoStack_.push(new cad::commands::SetFeatureVisibilityCommand(
            body_, std::move(ids), std::move(before), std::move(after), "Show All Features"));
        return {true, {}, {}};
    } catch (const std::exception& error) {
        return failure(error);
    }
}

QStringList ModelingController::dependentNames(const std::string& featureId)
{
    if (!body_.findFeature(featureId)) return {};
    try {
        auto command = std::make_unique<cad::commands::RemoveFeatureCommand>(
            body_, featureId);
        return command->dependentNames();
    } catch (const std::exception&) {
        return {};
    }
}

void ModelingController::clearProject()
{
    if (document_.features().empty() && body_.features().empty()) return;
    undoStack_.push(new cad::commands::ClearProjectCommand(document_, body_));
}

bool ModelingController::isVisibilityCommandAt(const int index) const noexcept
{
    if (index < 0 || index >= undoStack_.count()) return false;
    return containsVisibilityCommand(undoStack_.command(index));
}

ModelingActionState ModelingController::actionState(
    const std::vector<std::string>& selection
) const
{
    ModelingActionState state;
    state.canDelete = selection.size() == 1 && body_.findFeature(selection.front());
    state.canBoolean = selection.size() == 2
        && body_.findFeature(selection[0]) && body_.findFeature(selection[1]);
    if (selection.size() == 1) {
        const auto feature = body_.findFeature(selection.front());
        state.canCreateFace = feature && feature->role() == cad::parametric::FeatureRole::Sketch;
        state.canExtrude = feature && feature->role() == cad::parametric::FeatureRole::Face;
        if (feature && feature->role() == cad::parametric::FeatureRole::Sketch) {
            const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature);
            std::string profileError;
            const bool profileValid = sketch && validateSketchProfile(*sketch, profileError);
            state.canExtrude = profileValid;
            state.canPocket = sketch && sketch->supportType() == cad::parametric::SketchSupportType::Face
                && sketch->faceReference()
                && body_.findFeature(sketch->faceReference()->featureId);
            if (state.canPocket) {
                state.canPocket = profileValid;
            }
            state.canRevolve = profileValid;
            state.canSweep = profileValid;
        }
    }
    return state;
}

ModelingActionState ModelingController::actionState(
    const SelectionSnapshot& selection
) const
{
    auto state = actionState(selection.selectedObjectIds());
    if (selection.items.size() != 1
        || selection.items.front().kind != SelectionKind::Object) {
        state.canExtrude = false;
        state.canRevolve = false;
        state.canSweep = false;
    }
    if (!selection.items.empty()) {
        const auto& first = selection.items.front();
        const bool edgeSelection = first.kind == SelectionKind::Edge
            && first.subshapeIndex && !first.featureId.empty();
        const bool sameSource = edgeSelection && std::all_of(
            selection.items.begin(), selection.items.end(),
            [&first](const auto& item) {
                return item.kind == SelectionKind::Edge
                    && item.subshapeIndex
                    && item.featureId == first.featureId;
            });
        state.canFillet = sameSource;
        state.canChamfer = sameSource;
        if (selection.featureIds().size() == 1) {
            const auto source = body_.findFeature(selection.featureIds().front());
        state.canShell = source && !source->shape().IsNull()
                && source->role() != cad::parametric::FeatureRole::Sketch;
        }
        if (selection.items.size() == 1 && first.kind == SelectionKind::Face
            && first.subshapeIndex && !first.featureId.empty()) {
            const auto feature = body_.findFeature(first.featureId);
            if (feature) {
                SelectionResolver resolver(body_);
                const auto face = resolver.resolve(first);
                state.canSketchOnFace = face
                    && cad::parametric::SketchFeature::isPlanarFace(*face);
            }
        }
    }
    state.canEditSketch = canEditSketch(selection);
    return state;
}

bool ModelingController::canEditSketch(const SelectionSnapshot& selection) const
{
    if (selection.items.size() != 1
        || selection.items.front().kind != SelectionKind::Object) return false;
    const auto feature = body_.findFeature(selection.items.front().featureId);
    return std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature) != nullptr;
}

void ModelingController::replaceProject(Document document, cad::parametric::Body body)
{
    document_ = std::move(document);
    body_ = std::move(body);
    undoStack_.clear();
}

} // namespace cad::application
