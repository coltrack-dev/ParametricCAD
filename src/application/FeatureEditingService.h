#pragma once

#include "model/ParametricFeature.h"
#include "application/Selection.h"

#include <string>
#include <vector>

namespace cad::application {

enum class PrimitiveKind { Box, Cylinder, Cone, Sphere, Torus, Hexagon, Sketch };
enum class BooleanKind { Fuse, Cut, Common };

struct FeatureDescriptor
{
    std::string id;
    std::string name;
    bool visible{true};
    cad::parametric::FeatureState state;
    std::string error;
    std::vector<cad::parametric::FeatureProperty> properties;
};

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
    bool canPocket{false};
    bool canBoolean{false};
    bool canFillet{false};
    bool canChamfer{false};
    bool canShell{false};
    bool canRevolve{false};
    bool canSweep{false};
    bool canSketchOnFace{false};
    bool canEditSketch{false};
};

class FeatureEditingService
{
public:
    virtual ~FeatureEditingService() = default;
    virtual std::vector<FeatureDescriptor> features() const = 0;
    virtual ModelingActionState actionState(
        const std::vector<std::string>& selection
    ) const = 0;
    virtual ModelingResult setFeatureProperty(
        const std::string& featureId,
        const std::string& propertyKey,
        const cad::parametric::PropertyValue& value
    ) = 0;
    virtual ModelingResult createPrimitive(PrimitiveKind kind) = 0;
    virtual ModelingResult createFace(const std::vector<std::string>& selection) = 0;
    virtual ModelingResult createExtrude(const std::vector<std::string>& selection) = 0;
    virtual ModelingResult createBoolean(
        BooleanKind operation,
        const std::vector<std::string>& selection,
        const std::string& currentId = {}
    ) = 0;
    virtual ModelingResult deleteFeature(const std::string& featureId) = 0;
    virtual ModelingResult setFeatureVisibility(
        const std::vector<std::string>& featureIds, bool visible) = 0;
    virtual ModelingResult showAllFeatures() = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;
};

} // namespace cad::application
