#include "application/ModelingController.h"

#include "commands/FeatureCommands.h"
#include "operations/ParametricFeatures.h"

#include <QUuid>

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

ModelingResult ModelingController::createBoolean(
    const cad::parametric::BooleanOperation operation,
    const std::vector<std::string>& selection,
    const std::string& currentId
)
{
    if (selection.size() != 2) return {false, {}, "Select exactly two features"};
    auto left = body_.findFeature(selection[0]);
    auto right = body_.findFeature(selection[1]);
    if (!left || !right) return {false, {}, "Selected feature does not exist"};
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
    }
    return state;
}

void ModelingController::replaceProject(Document document, cad::parametric::Body body)
{
    document_ = std::move(document);
    body_ = std::move(body);
    undoStack_.clear();
}

} // namespace cad::application
