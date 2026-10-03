#include "operations/SketchConstraintSolver.h"

#include <algorithm>
#include <cmath>

namespace cad::operations {
namespace {
constexpr double tolerance = 1.0e-7;

cad::parametric::SketchEntity* findEntity(
    std::vector<cad::parametric::SketchEntity>& entities,
    const cad::parametric::SketchEntityId& id)
{
    for (auto& entity : entities) {
        const auto matches = [&id](const auto& value) { return value.id == id; };
        if (std::visit(matches, entity)) return &entity;
    }
    return nullptr;
}

std::optional<gp_Pnt2d> pointValue(const cad::parametric::SketchEntity& entity,
                                   const cad::parametric::SketchPointRole role)
{
    if (const auto* line = std::get_if<cad::parametric::SketchLine>(&entity)) {
        if (role == cad::parametric::SketchPointRole::LineStart) return line->start;
        if (role == cad::parametric::SketchPointRole::LineEnd) return line->end;
    } else if (const auto* arc = std::get_if<cad::parametric::SketchArc>(&entity)) {
        if (role == cad::parametric::SketchPointRole::ArcStart) return arc->startPoint();
        if (role == cad::parametric::SketchPointRole::ArcEnd) return arc->endPoint();
        if (role == cad::parametric::SketchPointRole::ArcCenter) return arc->center;
    } else if (const auto* circle = std::get_if<cad::parametric::SketchCircle>(&entity)) {
        if (role == cad::parametric::SketchPointRole::CircleCenter) return circle->center;
    }
    return std::nullopt;
}

bool setPoint(cad::parametric::SketchEntity& entity,
              const cad::parametric::SketchPointRole role, const gp_Pnt2d& point)
{
    if (auto* line = std::get_if<cad::parametric::SketchLine>(&entity)) {
        if (role == cad::parametric::SketchPointRole::LineStart) { line->start = point; return true; }
        if (role == cad::parametric::SketchPointRole::LineEnd) { line->end = point; return true; }
    } else if (auto* arc = std::get_if<cad::parametric::SketchArc>(&entity)) {
        const double angle = std::atan2(point.Y() - arc->center.Y(), point.X() - arc->center.X());
        if (role == cad::parametric::SketchPointRole::ArcStart) { arc->startAngle = angle; return true; }
        if (role == cad::parametric::SketchPointRole::ArcEnd) { arc->endAngle = angle; return true; }
        if (role == cad::parametric::SketchPointRole::ArcCenter) return false;
    } else if (auto* circle = std::get_if<cad::parametric::SketchCircle>(&entity)) {
        if (role == cad::parametric::SketchPointRole::CircleCenter) { circle->center = point; return true; }
    }
    return false;
}

bool samePoint(const gp_Pnt2d& a, const gp_Pnt2d& b)
{
    return a.Distance(b) <= tolerance;
}

} // namespace

SketchSolveResult SketchConstraintSolver::solve(
    const std::vector<cad::parametric::SketchEntity>& source,
    const std::vector<cad::parametric::SketchConstraint>& constraints)
{
    SketchSolveResult result;
    result.entities = source;
    for (const auto& constraint : constraints) {
        if (const auto* coincident = std::get_if<cad::parametric::CoincidentConstraint>(&constraint)) {
            const auto* a = findEntity(result.entities, coincident->a.entityId);
            const auto* b = findEntity(result.entities, coincident->b.entityId);
            if (!a || !b) { result.status = SolveStatus::Failed; result.error = "Constraint references missing entity"; return result; }
            if (!std::holds_alternative<cad::parametric::SketchLine>(*a)
                || !std::holds_alternative<cad::parametric::SketchLine>(*b)) {
                result.status = SolveStatus::Failed;
                result.error = "Coincident currently supports Line endpoints only";
                return result;
            }
        }
    }
    for (const auto& constraint : constraints) {
        const auto lineId = [](const auto& value) -> std::optional<std::string> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, cad::parametric::HorizontalConstraint>
                || std::is_same_v<T, cad::parametric::VerticalConstraint>) return value.entityId;
            return std::nullopt;
        };
        const auto id = std::visit(lineId, constraint);
        if (!id) continue;
        for (const auto& other : constraints) {
            const auto otherId = std::visit(lineId, other);
            if (!otherId || *otherId != *id || constraint.index() == other.index()) continue;
            const auto* entity = findEntity(result.entities, *id);
            const auto* line = entity ? std::get_if<cad::parametric::SketchLine>(entity) : nullptr;
            if (line && line->start.Distance(line->end) > tolerance) {
                result.status = SolveStatus::Failed;
                result.error = "Constraint conflict: Line cannot be both Horizontal and Vertical";
                return result;
            }
        }
    }
    for (int iteration = 0; iteration < 40; ++iteration) {
        bool changed = false;
        for (const auto& constraint : constraints) {
            if (const auto* horizontal = std::get_if<cad::parametric::HorizontalConstraint>(&constraint)) {
                auto* entity = findEntity(result.entities, horizontal->entityId);
                if (!entity || !std::holds_alternative<cad::parametric::SketchLine>(*entity)) {
                    result.status = SolveStatus::Failed; result.error = "Horizontal constraint requires a Line"; return result;
                }
                auto& line = std::get<cad::parametric::SketchLine>(*entity);
                const double y = horizontal->anchorStart ? line.start.Y() : line.end.Y();
                if (horizontal->anchorStart) {
                    if (std::abs(line.end.Y() - y) > tolerance) { line.end.SetY(y); changed = true; }
                } else if (std::abs(line.start.Y() - y) > tolerance) { line.start.SetY(y); changed = true; }
            } else if (const auto* vertical = std::get_if<cad::parametric::VerticalConstraint>(&constraint)) {
                auto* entity = findEntity(result.entities, vertical->entityId);
                if (!entity || !std::holds_alternative<cad::parametric::SketchLine>(*entity)) {
                    result.status = SolveStatus::Failed; result.error = "Vertical constraint requires a Line"; return result;
                }
                auto& line = std::get<cad::parametric::SketchLine>(*entity);
                const double x = vertical->anchorStart ? line.start.X() : line.end.X();
                if (vertical->anchorStart) {
                    if (std::abs(line.end.X() - x) > tolerance) { line.end.SetX(x); changed = true; }
                } else if (std::abs(line.start.X() - x) > tolerance) { line.start.SetX(x); changed = true; }
            } else {
                const auto& coincident = std::get<cad::parametric::CoincidentConstraint>(constraint);
                auto* master = findEntity(result.entities, coincident.a.entityId);
                auto* slave = findEntity(result.entities, coincident.b.entityId);
                const auto masterPoint = pointValue(*master, coincident.a.role);
                const auto slavePoint = pointValue(*slave, coincident.b.role);
                if (!masterPoint || !slavePoint || !setPoint(*slave, coincident.b.role, *masterPoint)) {
                    result.status = SolveStatus::Failed; result.error = "Invalid Coincident point reference"; return result;
                }
                if (!samePoint(*masterPoint, *slavePoint)) changed = true;
            }
        }
        if (!changed) return result;
    }
    result.status = SolveStatus::Failed;
    result.error = "Constraint solver did not converge";
    return result;
}

std::optional<cad::parametric::SketchPointRef> SketchConstraintSolver::pointAt(
    const std::vector<cad::parametric::SketchEntity>& entities,
    const gp_Pnt2d& point, const double hitTolerance)
{
    double best = hitTolerance;
    std::optional<cad::parametric::SketchPointRef> result;
    for (const auto& entity : entities) {
        const auto visit = [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, cad::parametric::SketchLine>) {
                for (const auto& candidate : {std::pair{value.start, cad::parametric::SketchPointRole::LineStart},
                                               std::pair{value.end, cad::parametric::SketchPointRole::LineEnd}}) {
                    const double distance = point.Distance(candidate.first);
                    if (distance <= best) { best = distance; result = {{value.id, candidate.second}}; }
                }
            } else if constexpr (std::is_same_v<T, cad::parametric::SketchArc>) {
                for (const auto& candidate : {std::pair{value.startPoint(), cad::parametric::SketchPointRole::ArcStart},
                                               std::pair{value.endPoint(), cad::parametric::SketchPointRole::ArcEnd}}) {
                    const double distance = point.Distance(candidate.first);
                    if (distance <= best) { best = distance; result = {{value.id, candidate.second}}; }
                }
            }
        };
        std::visit(visit, entity);
    }
    return result;
}

std::optional<cad::parametric::SketchEntityId> SketchConstraintSolver::lineAt(
    const std::vector<cad::parametric::SketchEntity>& entities,
    const gp_Pnt2d& point, const double hitTolerance)
{
    for (const auto& entity : entities) {
        const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
        if (!line) continue;
        const gp_Vec2d direction(line->start, line->end);
        const gp_Vec2d offset(line->start, point);
        const double length2 = direction.SquareMagnitude();
        if (length2 <= tolerance) continue;
        const double t = std::clamp(offset.Dot(direction) / length2, 0.0, 1.0);
        const gp_Pnt2d closest(line->start.X() + t * direction.X(),
                               line->start.Y() + t * direction.Y());
        if (closest.Distance(point) <= hitTolerance) return line->id;
    }
    return std::nullopt;
}

} // namespace cad::operations
