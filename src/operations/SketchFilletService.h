#pragma once

#include "operations/ParametricFeatures.h"

#include <string>
#include <optional>
#include <utility>
#include <vector>

namespace cad::operations {

enum class SketchFilletError {
    None,
    NoCornerSelected,
    InvalidRadius,
    RadiusTooLarge,
    DegenerateLine,
    ParallelLines,
    OverlappingFillets,
    ConstraintConflict,
    SelfIntersection,
    InvalidProfile,
    SolverFailure
};

struct SketchFilletValidationResult
{
    bool valid{false};
    SketchFilletError error{SketchFilletError::None};
    std::string message;
    std::vector<cad::parametric::SketchEntityId> affectedEntities;
    std::optional<double> maxAllowedRadius;
};

struct SketchFilletPlan
{
    bool changed{false};
    std::string error;
    SketchFilletValidationResult validation;
    std::vector<cad::parametric::SketchEntity> entities;
    std::vector<cad::parametric::SketchConstraint> constraints;
    cad::parametric::SketchEntityId firstLineId;
    cad::parametric::SketchEntityId secondLineId;
    cad::parametric::SketchEntityId arcId;
    double radius{0.0};
    double maximumRadius{0.0};
};

class SketchFilletService final
{
public:
    static std::optional<std::pair<cad::parametric::SketchEntityId,
                                   cad::parametric::SketchEntityId>> cornerAt(
        const std::vector<cad::parametric::SketchEntity>& entities,
        const gp_Pnt2d& point, double hitTolerance);
    static SketchFilletPlan analyze(
        const cad::parametric::SketchFeature& sketch,
        const cad::parametric::SketchEntityId& firstLineId,
        const cad::parametric::SketchEntityId& secondLineId,
        double radius);
};

} // namespace cad::operations
