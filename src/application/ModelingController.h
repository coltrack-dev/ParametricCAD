#pragma once

#include "application/FeatureEditingService.h"
#include "application/Selection.h"
#include "model/Body.h"
#include "model/Document.h"
#include "model/Feature.h"
#include "operations/ParametricFeatures.h"
#include "operations/PatternFeatures.h"

#include <QUndoStack>
#include <QStringList>

#include <string>
#include <optional>
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
    ModelingResult createLinearPattern(const std::vector<std::string>& selection);
    ModelingResult createPathPattern(const std::vector<std::string>& selection);
    ModelingResult createFillet(
        const SelectionSnapshot& selection,
        double radius = 3.0
    );
    ModelingResult createChamfer(
        const SelectionSnapshot& selection,
        double distance = 3.0
    );
    ModelingResult pushPull(
        const std::string& targetId,
        int faceIndex,
        const gp_Vec& normal,
        double distance
    );
    ModelingResult transformFeature(
        const std::string& featureId,
        const gp_Trsf& before,
        const gp_Trsf& after
    );
    ModelingResult transformFeatureDelta(
        const std::string& featureId,
        const gp_Trsf& delta
    );
    ModelingResult duplicateFeature(const std::string& featureId);
    ModelingResult duplicateFeatureWithDelta(
        const std::string& featureId,
        const gp_Trsf& delta
    );
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
    ModelingActionState actionState(
        const SelectionSnapshot& selection
    ) const;

    void replaceProject(Document document, cad::parametric::Body body);

private:
    ModelingResult addFeature(
        const cad::parametric::ParametricFeature::Ptr& feature
    );

    Document document_;
    cad::parametric::Body body_;
    QUndoStack undoStack_;
    std::string lastCopyFeatureId_;
    std::optional<gp_Trsf> lastCopyDelta_;
};

} // namespace cad::application
