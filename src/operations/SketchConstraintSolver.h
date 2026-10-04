#pragma once

#include "operations/ParametricFeatures.h"

#include <optional>
#include <string>
#include <vector>

namespace cad::operations {

enum class SolveStatus { Solved, Failed };

struct SketchSolveResult
{
    SolveStatus status{SolveStatus::Solved};
    std::vector<cad::parametric::SketchEntity> entities;
    std::string error;
};

class SketchConstraintSolver final
{
public:
    static SketchSolveResult solve(
        const std::vector<cad::parametric::SketchEntity>& entities,
        const std::vector<cad::parametric::SketchConstraint>& constraints);

    static std::optional<cad::parametric::SketchPointRef> pointAt(
        const std::vector<cad::parametric::SketchEntity>& entities,
        const gp_Pnt2d& point, double tolerance);
    static std::optional<cad::parametric::SketchEntityId> lineAt(
        const std::vector<cad::parametric::SketchEntity>& entities,
        const gp_Pnt2d& point, double tolerance);
    static std::optional<cad::parametric::SketchEntityId> circleOrArcAt(
        const std::vector<cad::parametric::SketchEntity>& entities,
        const gp_Pnt2d& point, double tolerance);
};

} // namespace cad::operations
