#pragma once

#include "operations/ParametricFeatures.h"

#include <optional>
#include <string>
#include <vector>

namespace cad::operations {

enum class ExtendEndpoint { Start, End };

struct ExtendPlan
{
    bool changed{false};
    std::string error;
    std::size_t entityIndex{0};
    ExtendEndpoint endpoint{ExtendEndpoint::End};
    cad::parametric::SketchEntity originalEntity;
    cad::parametric::SketchEntity extendedEntity;
    std::vector<cad::parametric::SketchEntity> extensionSpan;
};

class SketchExtendService final
{
public:
    static ExtendPlan analyzeExtend(const cad::parametric::SketchFeature& sketch,
                                    const gp_Pnt2d& click,
                                    double endpointTolerance);
    static std::optional<ExtendPlan> previewExtend(
        const cad::parametric::SketchFeature& sketch,
        const gp_Pnt2d& click,
        double endpointTolerance);
    static ExtendPlan extend(const cad::parametric::SketchFeature& sketch,
                             const gp_Pnt2d& click,
                             double endpointTolerance);
};

} // namespace cad::operations
