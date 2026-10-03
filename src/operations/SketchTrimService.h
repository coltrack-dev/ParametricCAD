#pragma once

#include "operations/ParametricFeatures.h"

#include <string>
#include <optional>
#include <vector>

namespace cad::operations {

struct SketchIntersection
{
    gp_Pnt2d point;
    double parameterOnA{0.0};
    double parameterOnB{0.0};
};

struct TrimPreview
{
    std::size_t entityIndex{0};
    double removedStart{0.0};
    double removedEnd{0.0};
};

struct TrimPlan
{
    bool changed{false};
    std::string error;
    std::size_t entityIndex{0};
    std::vector<cad::parametric::SketchEntity> removedEntities;
    std::vector<cad::parametric::SketchEntity> replacements;
    TrimPreview preview;
};

using SketchTrimResult = TrimPlan;

class SketchTrimService final
{
public:
    static TrimPlan analyzeTrim(const cad::parametric::SketchFeature& sketch,
                                const gp_Pnt2d& click, double hitTolerance);
    static std::optional<TrimPlan> previewTrim(
        const cad::parametric::SketchFeature& sketch,
        const gp_Pnt2d& click, double hitTolerance);
    static TrimPlan trim(const cad::parametric::SketchFeature& sketch,
                         const gp_Pnt2d& click, double hitTolerance);

    static std::vector<SketchIntersection> intersections(
        const cad::parametric::SketchEntity& a,
        const cad::parametric::SketchEntity& b);
};

} // namespace cad::operations
