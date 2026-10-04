#pragma once

#include "application/FeatureEditingService.h"
#include "application/Selection.h"
#include "model/Body.h"
#include "model/Document.h"
#include "model/Feature.h"
#include "operations/ParametricFeatures.h"
#include "operations/PatternFeatures.h"
#include "operations/SketchConstraintSolver.h"

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
    ModelingResult createSketchOnFace(const SelectionSnapshot& selection);
    ModelingResult addSketchLine(
        const std::string& sketchId, const gp_Pnt2d& start, const gp_Pnt2d& end);
    ModelingResult addSketchCircle(
        const std::string& sketchId, const gp_Pnt2d& center, double radius);
    ModelingResult addSketchArc(
        const std::string& sketchId, const gp_Pnt2d& center,
        const gp_Pnt2d& start, const gp_Pnt2d& end);
    ModelingResult trimSketchEntity(const std::string& sketchId, const gp_Pnt2d& click,
                                    double hitTolerance);
    ModelingResult extendSketchEntity(const std::string& sketchId, const gp_Pnt2d& click,
                                      double endpointTolerance);
    ModelingResult addSketchHorizontal(const std::string& sketchId,
                                       const std::string& lineId, bool anchorStart = true);
    ModelingResult addSketchVertical(const std::string& sketchId,
                                     const std::string& lineId, bool anchorStart = true);
    ModelingResult addSketchCoincident(const std::string& sketchId,
                                       const cad::parametric::SketchPointRef& a,
                                       const cad::parametric::SketchPointRef& b);
    ModelingResult addSketchDistance(const std::string& sketchId,
                                     const std::string& lineId, double value,
                                     bool anchorStart = true);
    ModelingResult addSketchRadius(const std::string& sketchId,
                                   const std::string& entityId, double value);
    ModelingResult addSketchHorizontalDistance(
        const std::string& sketchId, const cad::parametric::SketchPointRef& first,
        const cad::parametric::SketchPointRef& second, double value);
    ModelingResult addSketchVerticalDistance(
        const std::string& sketchId, const cad::parametric::SketchPointRef& first,
        const cad::parametric::SketchPointRef& second, double value);
    ModelingResult addSketchAngle(const std::string& sketchId,
                                  const std::string& lineId, double radians,
                                  bool anchorStart = true);
    ModelingResult addSketchParallel(const std::string& sketchId,
                                     const std::string& firstLineId,
                                     const std::string& secondLineId,
                                     bool anchorStart = true);
    ModelingResult addSketchPerpendicular(const std::string& sketchId,
                                          const std::string& firstLineId,
                                          const std::string& secondLineId,
                                          bool anchorStart = true);
    ModelingResult addSketchAngleBetweenLines(const std::string& sketchId,
                                              const std::string& referenceLineId,
                                              const std::string& dependentLineId,
                                              double radians, bool anchorStart = true);
    ModelingResult updateSketchDistance(const std::string& sketchId,
                                        const std::string& constraintId, double value);
    ModelingResult updateSketchRadius(const std::string& sketchId,
                                      const std::string& constraintId, double value);
    ModelingResult updateSketchHorizontalDistance(const std::string& sketchId,
                                                  const std::string& constraintId, double value);
    ModelingResult updateSketchVerticalDistance(const std::string& sketchId,
                                                const std::string& constraintId, double value);
    ModelingResult updateSketchAngle(const std::string& sketchId,
                                     const std::string& constraintId, double radians);
    ModelingResult updateSketchAngleBetweenLines(const std::string& sketchId,
                                                 const std::string& constraintId, double radians);
    ModelingResult removeSketchConstraint(const std::string& sketchId,
                                          const std::string& constraintId);
    ModelingResult createPrimitive(PrimitiveKind kind) override;
    ModelingResult createFace(const std::vector<std::string>& selection) override;
    ModelingResult createExtrude(const std::vector<std::string>& selection) override;
    ModelingResult createExtrudeFromSketch(
        const SelectionSnapshot& selection,
        double distance = 20.0,
        bool reversed = false
    );
    ModelingResult createPocketFromSketch(
        const SelectionSnapshot& selection,
        double depth = 10.0
    );
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
    bool canEditSketch(const SelectionSnapshot& selection) const;

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
