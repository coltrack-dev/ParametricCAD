#pragma once

#include "application/FeatureEditingService.h"
#include "model/Body.h"
#include "model/Document.h"
#include "model/Feature.h"
#include "operations/ParametricFeatures.h"

#include <QUndoStack>
#include <QStringList>

#include <string>
#include <vector>

namespace cad::application {
class ModelingController final : public FeatureEditingService
{
public:
    ModelingController() = default;

    cad::parametric::Body& body() noexcept;
    const cad::parametric::Body& body() const noexcept;
    Document& document() noexcept;
    QUndoStack& undoStack() noexcept;

    std::vector<FeatureDescriptor> features() const override;
    ModelingResult createBox();
    ModelingResult createCylinder();
    ModelingResult createSketch();
    ModelingResult createPrimitive(PrimitiveKind kind) override;
    ModelingResult createFace(const std::vector<std::string>& selection) override;
    ModelingResult createExtrude(const std::vector<std::string>& selection) override;
    ModelingResult createBoolean(
        BooleanKind operation,
        const std::vector<std::string>& selection,
        const std::string& currentId = {}
    ) override;
    ModelingResult deleteFeature(const std::string& featureId) override;
    void undo() override;
    void redo() override;
    ModelingResult setFeatureProperty(
        const std::string& featureId,
        const std::string& propertyKey,
        const cad::parametric::PropertyValue& value
    ) override;
    QStringList dependentNames(const std::string& featureId);
    void clearProject();

    ModelingActionState actionState(
        const std::vector<std::string>& selection
    ) const override;

    void replaceProject(Document document, cad::parametric::Body body);

private:
    ModelingResult addFeature(
        const cad::parametric::ParametricFeature::Ptr& feature
    );

    Document document_;
    cad::parametric::Body body_;
    QUndoStack undoStack_;
};

} // namespace cad::application
