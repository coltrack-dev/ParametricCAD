#include "application/ModelingController.h"

#include "commands/FeatureCommands.h"
#include "operations/ParametricFeatures.h"

#include <QUuid>

#include <algorithm>
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
