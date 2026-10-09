#pragma once

#include "operations/ParametricFeatures.h"

#include <optional>
#include <string>
#include <vector>

namespace cad::operations {

enum class SketchChamferError {
    None,
    NoCornerSelected,
    InvalidDistance,
    InvalidAngle,
    DistanceTooLarge,
    DegenerateLine,
    AmbiguousCorner,
    OverlappingChamfers,
    ConstraintConflict,
    InvalidProfile,
    SolverFailure
};

struct SketchChamferValidationResult
{
    bool valid{false};
    SketchChamferError error{SketchChamferError::None};
    std::string message;
    std::vector<cad::parametric::SketchEntityId> affectedEntities;
    std::optional<double> maxAllowedDistance;
};

struct SketchChamferPlan
{
    bool changed{false};
    std::string error;
    SketchChamferValidationResult validation;
    std::vector<cad::parametric::SketchEntity> entities;
    std::vector<cad::parametric::SketchConstraint> constraints;
    cad::parametric::SketchEntityId firstLineId;
    cad::parametric::SketchEntityId secondLineId;
    cad::parametric::SketchEntityId chamferLineId;
    cad::parametric::SketchChamferMode mode{cad::parametric::SketchChamferMode::EqualDistance};
    double firstDistance{0.0};
    double secondDistance{0.0};
    double angleRadians{0.0};
};

class SketchChamferService final
{
public:
    static std::optional<std::pair<cad::parametric::SketchEntityId,
                                   cad::parametric::SketchEntityId>> cornerAt(
        const std::vector<cad::parametric::SketchEntity>& entities,
        const gp_Pnt2d& point, double hitTolerance);

    static SketchChamferPlan analyze(
        const cad::parametric::SketchFeature& sketch,
        const cad::parametric::SketchEntityId& firstLineId,
        const cad::parametric::SketchEntityId& secondLineId,
        cad::parametric::SketchChamferMode mode,
        double firstDistance, double secondDistance = 0.0,
        double angleRadians = 0.0);
};

} // namespace cad::operations
