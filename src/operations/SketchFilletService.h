#pragma once

#include "operations/ParametricFeatures.h"

#include <string>
#include <vector>

namespace cad::operations {

struct SketchFilletPlan
{
    bool changed{false};
    std::string error;
    std::vector<cad::parametric::SketchEntity> entities;
    std::vector<cad::parametric::SketchConstraint> constraints;
    cad::parametric::SketchEntityId firstLineId;
    cad::parametric::SketchEntityId secondLineId;
    cad::parametric::SketchEntityId arcId;
    double radius{0.0};
};

class SketchFilletService final
{
public:
    static SketchFilletPlan analyze(
        const cad::parametric::SketchFeature& sketch,
        const cad::parametric::SketchEntityId& firstLineId,
        const cad::parametric::SketchEntityId& secondLineId,
        double radius);
};

} // namespace cad::operations
