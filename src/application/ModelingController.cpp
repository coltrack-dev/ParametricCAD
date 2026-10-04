#include "application/ModelingController.h"
#include "application/SelectionResolver.h"

#include "commands/FeatureCommands.h"
#include "operations/ParametricFeatures.h"
#include "operations/PatternFeatures.h"
#include "operations/SketchProfileBuilder.h"
#include "operations/SketchTrimService.h"
#include "operations/SketchExtendService.h"
#include "operations/SketchConstraintSolver.h"

#include <TopoDS.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace cad::application {

namespace {
std::string id(const char* prefix)
{
    return std::string(prefix) + "-"
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

ModelingResult failure(const std::exception& error)
{
    return {false, {}, error.what()};
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
        result.push_back({feature->id(), feature->name(), feature->state(),
                          feature->error(), feature->properties()});
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

ModelingResult ModelingController::createSketch()
{
    return addFeature(std::make_shared<cad::parametric::SketchFeature>(
        id("sketch"), 100.0, 60.0));
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

ModelingResult ModelingController::addSketchLine(
    const std::string& sketchId, const gp_Pnt2d& start, const gp_Pnt2d& end)
{
    const auto feature = body_.findFeature(sketchId);
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature);
    if (!sketch) return {false, {}, "Active Sketch does not exist"};
    try {
        undoStack_.push(new cad::commands::AddSketchEntityCommand(
            body_, sketch, cad::parametric::SketchLine{start, end}));
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
    try {
        (void)cad::operations::SketchProfileBuilder::build(*sketch);
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
    try {
        (void)cad::operations::SketchProfileBuilder::build(*sketch);
        return addFeature(std::make_shared<cad::parametric::PocketFeature>(
            id("pocket"), target, sketch, depth));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createLinearPattern(
    const std::vector<std::string>& selection
)
{
    if (selection.size() != 1) return {false, {}, "Select exactly one source feature"};
    const auto source = body_.findFeature(selection.front());
    if (!source) return {false, {}, "Pattern source does not exist"};
    try {
        return addFeature(std::make_shared<cad::parametric::LinearPatternFeature>(
            id("linear-pattern"),
            std::vector<cad::parametric::ParametricFeature::Ptr>{source},
            gp_Vec(1.0, 0.0, 0.0), 100.0, 2, true));
    } catch (const std::exception& error) {
        return failure(error);
    }
}

ModelingResult ModelingController::createPathPattern(
    const std::vector<std::string>& selection
)
{
    if (selection.size() != 2) return {false, {}, "Select source and path features"};
    const auto source = body_.findFeature(selection[0]);
    const auto path = body_.findFeature(selection[1]);
    if (!source || !path) return {false, {}, "Pattern source or path does not exist"};
    try {
        return addFeature(std::make_shared<cad::parametric::PathPatternFeature>(
            id("path-pattern"),
            std::vector<cad::parametric::ParametricFeature::Ptr>{source},
            path, 100.0, 2,
            cad::parametric::PathPatternDistribution::FitCount,
            cad::parametric::PathPatternOrientation::Fixed,
            0.0, 0.0, true));
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
        const auto feature = std::make_shared<cad::parametric::PushPullFeature>(
            id("pushpull"), source, faceIndex, normal, distance);
        return addFeature(feature);
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
            state.canExtrude = false;
            state.canPocket = sketch && sketch->supportType() == cad::parametric::SketchSupportType::Face
                && sketch->faceReference()
                && body_.findFeature(sketch->faceReference()->featureId);
            if (state.canPocket) {
                try { (void)cad::operations::SketchProfileBuilder::build(*sketch); }
                catch (const std::exception&) { state.canPocket = false; }
            }
        }
    }
    return state;
}

ModelingActionState ModelingController::actionState(
    const SelectionSnapshot& selection
) const
{
    auto state = actionState(selection.selectedObjectIds());
    if (selection.items.size() == 1
        && selection.items.front().kind == SelectionKind::Object) {
        const auto feature = body_.findFeature(selection.items.front().featureId);
        const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature);
        if (sketch) {
            try {
                (void)cad::operations::SketchProfileBuilder::build(*sketch);
                state.canExtrude = true;
            } catch (const std::exception&) {}
        }
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
