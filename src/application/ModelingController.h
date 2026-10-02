#pragma once

#include "model/Body.h"
#include "model/Document.h"
#include "model/Feature.h"
#include "operations/ParametricFeatures.h"

#include <QUndoStack>
#include <QStringList>

#include <string>
#include <vector>

namespace cad::application {

struct ModelingResult
{
    bool success{false};
    std::string id;
    std::string error;
};

struct ModelingActionState
{
    bool canDelete{false};
    bool canCreateFace{false};
    bool canExtrude{false};
    bool canBoolean{false};
};

class ModelingController final
{
public:
    ModelingController() = default;

    cad::parametric::Body& body() noexcept;
    const cad::parametric::Body& body() const noexcept;
    Document& document() noexcept;
    QUndoStack& undoStack() noexcept;

    ModelingResult createBox();
    ModelingResult createCylinder();
    ModelingResult createSketch();
    ModelingResult createFace(const std::vector<std::string>& selection);
    ModelingResult createExtrude(const std::vector<std::string>& selection);
    ModelingResult createBoolean(
        cad::parametric::BooleanOperation operation,
        const std::vector<std::string>& selection,
        const std::string& currentId = {}
    );
    ModelingResult deleteFeature(const std::string& featureId);
    QStringList dependentNames(const std::string& featureId);
    void clearProject();

    ModelingActionState actionState(
        const std::vector<std::string>& selection
    ) const;

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
