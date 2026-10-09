#include "operations/SketchConstraintSolver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace cad::operations {
namespace {
constexpr double tolerance = 1.0e-7;
constexpr double twoPi = 6.283185307179586476925286766559;

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

bool setAxisCoordinate(cad::parametric::SketchEntity& entity,
                       const cad::parametric::SketchPointRole role,
                       const gp_Pnt2d& point, const bool horizontal)
{
    if (auto* line = std::get_if<cad::parametric::SketchLine>(&entity)) {
        gp_Pnt2d* target = nullptr;
        if (role == cad::parametric::SketchPointRole::LineStart) target = &line->start;
        if (role == cad::parametric::SketchPointRole::LineEnd) target = &line->end;
        if (!target) return false;
        if (horizontal) target->SetX(point.X()); else target->SetY(point.Y());
        return true;
    }
    if (role == cad::parametric::SketchPointRole::CircleCenter) {
        auto* circle = std::get_if<cad::parametric::SketchCircle>(&entity);
        if (!circle) return false;
        if (horizontal) circle->center.SetX(point.X()); else circle->center.SetY(point.Y());
        return true;
    }
    if (role == cad::parametric::SketchPointRole::ArcCenter) {
        auto* arc = std::get_if<cad::parametric::SketchArc>(&entity);
        if (!arc) return false;
        if (horizontal) arc->center.SetX(point.X()); else arc->center.SetY(point.Y());
        return true;
    }
    return false;
}

std::string pointKey(const cad::parametric::SketchPointRef& ref)
{
    return ref.entityId + ":" + std::to_string(static_cast<int>(ref.role));
}

cad::parametric::SketchLine* lineById(
    std::vector<cad::parametric::SketchEntity>& entities, const std::string& id)
{
    auto* entity = findEntity(entities, id);
    return entity ? std::get_if<cad::parametric::SketchLine>(entity) : nullptr;
}

bool applyLineOrientation(cad::parametric::SketchLine& dependent,
                          const cad::parametric::SketchLine& reference,
                          const bool perpendicular, const bool anchorStart,
                          const double tolerance, bool& changed)
{
    const gp_Pnt2d anchor = anchorStart ? dependent.start : dependent.end;
    const gp_Pnt2d moving = anchorStart ? dependent.end : dependent.start;
    const double length = anchor.Distance(moving);
    const double referenceLength = reference.start.Distance(reference.end);
    if (length <= tolerance || referenceLength <= tolerance) return false;
    gp_Vec2d direction(reference.start, reference.end);
    direction.Normalize();
    if (perpendicular) direction = gp_Vec2d(-direction.Y(), direction.X());
    gp_Vec2d current(anchor, moving);
    if (current.Dot(direction) < 0.0) direction.Reverse();
    const gp_Pnt2d solved(anchor.X() + length * direction.X(),
        anchor.Y() + length * direction.Y());
    if (anchorStart) {
        if (!samePoint(dependent.end, solved)) { dependent.end = solved; changed = true; }
    } else if (!samePoint(dependent.start, solved)) {
        dependent.start = solved; changed = true;
    }
    return true;
}

double normalizedAngle(const double angle)
{
    return std::remainder(angle, twoPi);
}

bool anglesCompatible(const double first, const double second, const double tolerance)
{
    return std::abs(normalizedAngle(first - second)) <= tolerance;
}

bool angleModuloPiCompatible(const double first, const double second, const double tolerance)
{
    return std::abs(std::remainder(first - second, 3.14159265358979323846)) <= tolerance;
}

bool applyAngleBetweenLines(cad::parametric::SketchLine& dependent,
                            const cad::parametric::SketchLine& reference,
                            const double relativeAngle, const bool anchorStart,
                            const double tolerance, bool& changed)
{
    const gp_Pnt2d anchor = anchorStart ? dependent.start : dependent.end;
    const gp_Pnt2d moving = anchorStart ? dependent.end : dependent.start;
    const double length = anchor.Distance(moving);
    const double referenceLength = reference.start.Distance(reference.end);
    if (length <= tolerance || referenceLength <= tolerance) return false;
    const double referenceAngle = std::atan2(reference.end.Y() - reference.start.Y(),
        reference.end.X() - reference.start.X());
    const double targetAngle = normalizedAngle(referenceAngle + relativeAngle);
    const gp_Pnt2d solved(anchor.X() + length * std::cos(targetAngle),
        anchor.Y() + length * std::sin(targetAngle));
    if (anchorStart) {
        if (!samePoint(dependent.end, solved)) { dependent.end = solved; changed = true; }
    } else if (!samePoint(dependent.start, solved)) {
        dependent.start = solved; changed = true;
    }
    return true;
}

bool arcContainsAngle(const cad::parametric::SketchArc& arc, const double angle)
{
    double delta = arc.clockwise ? std::remainder(arc.startAngle - angle, twoPi)
                                 : std::remainder(angle - arc.startAngle, twoPi);
    if (delta < 0.0) delta += twoPi;
    return delta <= std::abs(arc.signedSweep()) + tolerance;
}

bool applyTangentFromStart(cad::parametric::SketchLine& line, const gp_Pnt2d& center,
                  const double radius, const cad::parametric::SketchArc* arc,
                  bool& changed)
{
    const double length = line.start.Distance(line.end);
    const double distance = line.start.Distance(center);
    if (length <= tolerance || distance <= radius + tolerance) return false;
    const double base = std::atan2(center.Y() - line.start.Y(), center.X() - line.start.X());
    const double offset = std::asin(radius / distance);
    const double current = std::atan2(line.end.Y() - line.start.Y(), line.end.X() - line.start.X());
    const double first = base + offset;
    const double second = base - offset;
    const double target = std::abs(normalizedAngle(first - current)) <= std::abs(normalizedAngle(second - current)) ? first : second;
    const gp_Pnt2d tangentPoint(line.start.X() + std::cos(target) * distance * std::cos(offset),
        line.start.Y() + std::sin(target) * distance * std::cos(offset));
    if (arc && !arcContainsAngle(*arc, std::atan2(tangentPoint.Y() - arc->center.Y(), tangentPoint.X() - arc->center.X()))) return false;
    const gp_Pnt2d solved(line.start.X() + length * std::cos(target), line.start.Y() + length * std::sin(target));
    if (!samePoint(line.end, solved)) { line.end = solved; changed = true; }
    return true;
}

bool applyTangent(cad::parametric::SketchLine& line, const gp_Pnt2d& center,
                  const double radius, const cad::parametric::SketchArc* arc,
                  bool& changed)
{
    // Tangency may be represented at either endpoint. The historical solver
    // keeps line.start fixed, so reverse the line temporarily when its start
    // is the endpoint already on the circle.
    const double startError = std::abs(line.start.Distance(center) - radius);
    const double endError = std::abs(line.end.Distance(center) - radius);
    if (startError < endError) {
        std::swap(line.start, line.end);
        const bool solved = applyTangentFromStart(line, center, radius, arc, changed);
        std::swap(line.start, line.end);
        return solved;
    }
    return applyTangentFromStart(line, center, radius, arc, changed);
}

bool applyLineArcTangentFromStart(cad::parametric::SketchLine& line,
                                   cad::parametric::SketchArc& arc,
                                   bool& changed)
{
    const double length = line.start.Distance(line.end);
    if (length <= tolerance || arc.radius <= tolerance) return false;
    gp_Vec2d direction(line.start, line.end);
    direction.Normalize();
    const gp_Vec2d normal(-direction.Y(), direction.X());
    const gp_Pnt2d oldCenter = arc.center;
    const gp_Pnt2d oldStart = arc.startPoint();
    const gp_Pnt2d oldEnd = arc.endPoint();
    const gp_Pnt2d candidateLeft(
        line.start.X() + normal.X() * arc.radius,
        line.start.Y() + normal.Y() * arc.radius);
    const gp_Pnt2d candidateRight(
        line.start.X() - normal.X() * arc.radius,
        line.start.Y() - normal.Y() * arc.radius);
    const gp_Pnt2d center = candidateLeft.Distance(oldCenter) <= candidateRight.Distance(oldCenter)
        ? candidateLeft : candidateRight;
    const gp_Vec2d offset(line.start, center);
    const gp_Pnt2d tangent(line.start.X() + direction.X() * offset.Dot(direction),
                           line.start.Y() + direction.Y() * offset.Dot(direction));
    const bool startTangent = std::abs(line.start.Distance(oldCenter) - arc.radius)
        <= std::abs(line.end.Distance(oldCenter) - arc.radius);
    const bool arcStart = tangent.Distance(oldStart) <= tangent.Distance(oldEnd);
    if (startTangent) {
        if (!samePoint(line.start, tangent)) { line.start = tangent; changed = true; }
    } else if (!samePoint(line.end, tangent)) {
        line.end = tangent;
        changed = true;
    }
    if (!samePoint(arc.center, center)) { arc.center = center; changed = true; }
    const double angle = std::atan2(tangent.Y() - center.Y(), tangent.X() - center.X());
    if (arcStart) {
        if (std::abs(arc.startAngle - angle) > tolerance) { arc.startAngle = angle; changed = true; }
    } else if (std::abs(arc.endAngle - angle) > tolerance) {
        arc.endAngle = angle;
        changed = true;
    }
    return true;
}

bool applyLineArcTangent(cad::parametric::SketchLine& line,
                         cad::parametric::SketchArc& arc,
                         bool& changed)
{
    const double startError = std::abs(line.start.Distance(arc.center) - arc.radius);
    const double endError = std::abs(line.end.Distance(arc.center) - arc.radius);
    if (endError < startError) {
        std::swap(line.start, line.end);
        const bool solved = applyLineArcTangentFromStart(line, arc, changed);
        std::swap(line.start, line.end);
        return solved;
    }
    return applyLineArcTangentFromStart(line, arc, changed);
}

bool applyFilletRadius(std::vector<cad::parametric::SketchEntity>& entities,
                       const std::vector<cad::parametric::SketchConstraint>& constraints,
                       cad::parametric::SketchArc& arc, const double radius,
                       bool& changed)
{
    std::array<cad::parametric::SketchLine*, 2> lines{nullptr, nullptr};
    std::size_t count = 0;
    for (const auto& constraint : constraints) {
        const auto* tangent = std::get_if<cad::parametric::TangentConstraint>(&constraint);
        if (!tangent || tangent->secondEntityId != arc.id || count == lines.size()) continue;
        auto* entity = findEntity(entities, tangent->firstEntityId);
        auto* line = entity ? std::get_if<cad::parametric::SketchLine>(entity) : nullptr;
        if (!line || std::find(lines.begin(), lines.end(), line) != lines.end()) continue;
        lines[count++] = line;
    }
    if (count != lines.size()) return false;
    gp_Vec2d firstDirection(lines[0]->start, lines[0]->end);
    gp_Vec2d secondDirection(lines[1]->start, lines[1]->end);
    const double denominator = firstDirection.Crossed(secondDirection);
    if (std::abs(denominator) <= tolerance) return false;
    const gp_Vec2d between(lines[0]->start, lines[1]->start);
    const double parameter = between.Crossed(secondDirection) / denominator;
    const gp_Pnt2d vertex(lines[0]->start.X() + parameter * firstDirection.X(),
                          lines[0]->start.Y() + parameter * firstDirection.Y());
    std::array<gp_Vec2d, 2> rays;
    std::array<gp_Pnt2d, 2> outers;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto& line = *lines[index];
        outers[index] = vertex.Distance(line.start) >= vertex.Distance(line.end)
            ? line.start : line.end;
        rays[index] = gp_Vec2d(vertex, outers[index]);
        if (rays[index].Magnitude() <= tolerance) return false;
        rays[index].Normalize();
    }
    const double dot = std::clamp(rays[0].Dot(rays[1]), -1.0, 1.0);
    const double theta = std::acos(dot);
    const double tangentDistance = radius / std::tan(theta / 2.0);
    if (!std::isfinite(tangentDistance) || tangentDistance <= tolerance) return false;
    gp_Vec2d bisector = rays[0] + rays[1];
    if (bisector.SquareMagnitude() <= tolerance * tolerance) return false;
    bisector.Normalize();
    if (gp_Vec2d(vertex, arc.center).Dot(bisector) < 0.0) bisector.Reverse();
    const double centerDistance = radius / std::sin(theta / 2.0);
    const gp_Pnt2d center(vertex.X() + bisector.X() * centerDistance,
                          vertex.Y() + bisector.Y() * centerDistance);
    const gp_Pnt2d firstTangent(vertex.X() + rays[0].X() * tangentDistance,
                                vertex.Y() + rays[0].Y() * tangentDistance);
    const gp_Pnt2d secondTangent(vertex.X() + rays[1].X() * tangentDistance,
                                 vertex.Y() + rays[1].Y() * tangentDistance);
    const gp_Pnt2d oldStart = arc.startPoint();
    const auto setLineTangent = [&](cad::parametric::SketchLine& line,
                                    const gp_Pnt2d& tangent) {
        if (vertex.Distance(line.start) <= vertex.Distance(line.end)) {
            if (!samePoint(line.start, tangent)) line.start = tangent;
        } else if (!samePoint(line.end, tangent)) {
            line.end = tangent;
        }
    };
    setLineTangent(*lines[0], firstTangent);
    setLineTangent(*lines[1], secondTangent);
    const double firstAngle = std::atan2(firstTangent.Y() - center.Y(), firstTangent.X() - center.X());
    const double secondAngle = std::atan2(secondTangent.Y() - center.Y(), secondTangent.X() - center.X());
    const bool firstIsStart = oldStart.Distance(firstTangent) <= oldStart.Distance(secondTangent);
    if (arc.radius != radius || !samePoint(arc.center, center)) { arc.radius = radius; arc.center = center; changed = true; }
    if (firstIsStart) {
        if (std::abs(arc.startAngle - firstAngle) > tolerance) { arc.startAngle = firstAngle; changed = true; }
        if (std::abs(arc.endAngle - secondAngle) > tolerance) { arc.endAngle = secondAngle; changed = true; }
    } else {
        if (std::abs(arc.startAngle - secondAngle) > tolerance) { arc.startAngle = secondAngle; changed = true; }
        if (std::abs(arc.endAngle - firstAngle) > tolerance) { arc.endAngle = firstAngle; changed = true; }
    }
    return true;
}

} // namespace

SketchSolveResult SketchConstraintSolver::solve(
    const std::vector<cad::parametric::SketchEntity>& source,
    const std::vector<cad::parametric::SketchConstraint>& constraints)
{
    SketchSolveResult result;
    result.entities = source;
    for (const auto& constraint : constraints) {
        if (const auto* equal = std::get_if<cad::parametric::EqualConstraint>(&constraint)) {
            const auto* first = findEntity(result.entities, equal->referenceEntityId);
            const auto* second = findEntity(result.entities, equal->dependentEntityId);
            if (!first || !second) { result.status = SolveStatus::Failed; result.error = "Equal target entity is missing"; return result; }
            const bool firstLine = std::holds_alternative<cad::parametric::SketchLine>(*first);
            const bool secondLine = std::holds_alternative<cad::parametric::SketchLine>(*second);
            const bool firstRound = std::holds_alternative<cad::parametric::SketchCircle>(*first) || std::holds_alternative<cad::parametric::SketchArc>(*first);
            const bool secondRound = std::holds_alternative<cad::parametric::SketchCircle>(*second) || std::holds_alternative<cad::parametric::SketchArc>(*second);
            if (!((firstLine && secondLine) || (firstRound && secondRound))) { result.status = SolveStatus::Failed; result.error = "Equal constraint requires compatible entity types"; return result; }
        }
    }
    for (const auto& constraint : constraints) {
        if (const auto* coincident = std::get_if<cad::parametric::CoincidentConstraint>(&constraint)) {
            const auto* a = findEntity(result.entities, coincident->a.entityId);
            const auto* b = findEntity(result.entities, coincident->b.entityId);
            if (!a || !b) { result.status = SolveStatus::Failed; result.error = "Constraint references missing entity"; return result; }
            if (!pointValue(*a, coincident->a.role)
                || !pointValue(*b, coincident->b.role)) {
                result.status = SolveStatus::Failed;
                result.error = "Coincident references an invalid Sketch point";
                return result;
            }
        }
    }
    std::unordered_map<std::string, double> angleBetweenValues;
    for (const auto& constraint : constraints) {
        const auto* item = std::get_if<cad::parametric::AngleBetweenLinesConstraint>(&constraint);
        if (!item) continue;
        if (item->referenceLineId == item->dependentLineId) {
            result.status = SolveStatus::Failed;
            result.error = "AngleBetweenLines requires two different Lines";
            return result;
        }
        const auto* reference = lineById(result.entities, item->referenceLineId);
        const auto* dependent = lineById(result.entities, item->dependentLineId);
        if (!reference || !dependent) {
            result.status = SolveStatus::Failed;
            result.error = "AngleBetweenLines constraint requires two Lines";
            return result;
        }
        if (!std::isfinite(item->angleRadians)) {
            result.status = SolveStatus::Failed;
            result.error = "AngleBetweenLines value must be finite";
            return result;
        }
        const auto key = item->referenceLineId + "->" + item->dependentLineId;
        const auto [it, inserted] = angleBetweenValues.emplace(key, normalizedAngle(item->angleRadians));
        if (!inserted && !anglesCompatible(it->second, item->angleRadians, tolerance)) {
            result.status = SolveStatus::Failed;
            result.error = "Conflicting AngleBetweenLines constraints";
            return result;
        }
        for (const auto& other : constraints) {
            if (const auto* reversed = std::get_if<cad::parametric::AngleBetweenLinesConstraint>(&other);
                reversed && reversed->referenceLineId == item->dependentLineId
                && reversed->dependentLineId == item->referenceLineId
                && !anglesCompatible(item->angleRadians, -reversed->angleRadians, tolerance)) {
                result.status = SolveStatus::Failed;
                result.error = "Conflicting reversed AngleBetweenLines constraints";
                return result;
            }
            if (const auto* parallel = std::get_if<cad::parametric::ParallelConstraint>(&other);
                parallel && parallel->firstLineId == item->referenceLineId
                && parallel->secondLineId == item->dependentLineId
                && !angleModuloPiCompatible(item->angleRadians, 0.0, tolerance)) {
                result.status = SolveStatus::Failed;
                result.error = "AngleBetweenLines conflicts with Parallel constraint";
                return result;
            }
            if (const auto* perpendicular = std::get_if<cad::parametric::PerpendicularConstraint>(&other);
                perpendicular && perpendicular->firstLineId == item->referenceLineId
                && perpendicular->secondLineId == item->dependentLineId
                && !angleModuloPiCompatible(item->angleRadians, 3.14159265358979323846 / 2.0, tolerance)) {
                result.status = SolveStatus::Failed;
                result.error = "AngleBetweenLines conflicts with Perpendicular constraint";
                return result;
            }
        }
    }
    for (const auto& constraint : constraints) {
        const auto* parallel = std::get_if<cad::parametric::ParallelConstraint>(&constraint);
        const auto* perpendicular = std::get_if<cad::parametric::PerpendicularConstraint>(&constraint);
        if (!parallel && !perpendicular) continue;
        const auto& first = parallel ? parallel->firstLineId : perpendicular->firstLineId;
        const auto& second = parallel ? parallel->secondLineId : perpendicular->secondLineId;
        if (first == second) {
            result.status = SolveStatus::Failed;
            result.error = "Parallel or Perpendicular constraint requires two different Lines";
            return result;
        }
        for (const auto& other : constraints) {
            if (parallel && std::get_if<cad::parametric::PerpendicularConstraint>(&other)
                && std::get<cad::parametric::PerpendicularConstraint>(other).firstLineId == first
                && std::get<cad::parametric::PerpendicularConstraint>(other).secondLineId == second) {
                result.status = SolveStatus::Failed; result.error = "Parallel and Perpendicular constraints conflict"; return result;
            }
            if (perpendicular && std::get_if<cad::parametric::ParallelConstraint>(&other)
                && std::get<cad::parametric::ParallelConstraint>(other).firstLineId == first
                && std::get<cad::parametric::ParallelConstraint>(other).secondLineId == second) {
                result.status = SolveStatus::Failed; result.error = "Parallel and Perpendicular constraints conflict"; return result;
            }
        }
    }
    std::unordered_map<std::string, double> horizontalDistances;
    std::unordered_map<std::string, double> verticalDistances;
    std::unordered_map<std::string, double> angles;
    for (const auto& constraint : constraints) {
        if (const auto* item = std::get_if<cad::parametric::HorizontalDistanceConstraint>(&constraint)) {
            if (!std::isfinite(item->value)) {
                result.status = SolveStatus::Failed; result.error = "Horizontal distance must be finite"; return result;
            }
            const auto key = pointKey(item->first) + "->" + pointKey(item->second);
            const auto [it, inserted] = horizontalDistances.emplace(key, item->value);
            if (!inserted && std::abs(it->second - item->value) > tolerance) {
                result.status = SolveStatus::Failed; result.error = "Conflicting horizontal distance constraints"; return result;
            }
        } else if (const auto* item = std::get_if<cad::parametric::VerticalDistanceConstraint>(&constraint)) {
            if (!std::isfinite(item->value)) {
                result.status = SolveStatus::Failed; result.error = "Vertical distance must be finite"; return result;
            }
            const auto key = pointKey(item->first) + "->" + pointKey(item->second);
            const auto [it, inserted] = verticalDistances.emplace(key, item->value);
            if (!inserted && std::abs(it->second - item->value) > tolerance) {
                result.status = SolveStatus::Failed; result.error = "Conflicting vertical distance constraints"; return result;
            }
        } else if (const auto* item = std::get_if<cad::parametric::AngleConstraint>(&constraint)) {
            if (!std::isfinite(item->radians)) {
                result.status = SolveStatus::Failed; result.error = "Angle constraint must be finite"; return result;
            }
            const auto [it, inserted] = angles.emplace(item->entityId, item->radians);
            if (!inserted && std::abs(std::remainder(it->second - item->radians, twoPi)) > tolerance) {
                result.status = SolveStatus::Failed; result.error = "Conflicting angle constraints"; return result;
            }
        }
    }
    for (const auto& constraint : constraints) {
        const auto* angle = std::get_if<cad::parametric::AngleConstraint>(&constraint);
        if (!angle) continue;
        const auto* entity = findEntity(result.entities, angle->entityId);
        const auto* line = entity ? std::get_if<cad::parametric::SketchLine>(entity) : nullptr;
        if (!line) { result.status = SolveStatus::Failed; result.error = "Angle constraint requires a Line"; return result; }
        for (const auto& other : constraints) {
            const bool horizontal = std::get_if<cad::parametric::HorizontalConstraint>(&other)
                && std::get<cad::parametric::HorizontalConstraint>(other).entityId == angle->entityId;
            const bool vertical = std::get_if<cad::parametric::VerticalConstraint>(&other)
                && std::get<cad::parametric::VerticalConstraint>(other).entityId == angle->entityId;
            const double normalized = std::remainder(angle->radians, twoPi);
            if (horizontal && std::abs(std::sin(normalized)) > tolerance) {
                result.status = SolveStatus::Failed; result.error = "Angle conflicts with Horizontal constraint"; return result;
            }
            if (vertical && std::abs(std::cos(normalized)) > tolerance) {
                result.status = SolveStatus::Failed; result.error = "Angle conflicts with Vertical constraint"; return result;
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
    std::unordered_map<std::string, double> distanceValues;
    std::unordered_map<std::string, double> radiusValues;
    for (const auto& constraint : constraints) {
        if (const auto* distance = std::get_if<cad::parametric::DistanceConstraint>(&constraint)) {
            if (!std::isfinite(distance->value) || distance->value <= tolerance) {
                result.status = SolveStatus::Failed;
                result.error = "Distance constraint value must be finite and positive";
                return result;
            }
            const auto [it, inserted] = distanceValues.emplace(distance->entityId, distance->value);
            if (!inserted && std::abs(it->second - distance->value) > tolerance) {
                result.status = SolveStatus::Failed;
                result.error = "Conflicting distance constraints";
                return result;
            }
        } else if (const auto* radius = std::get_if<cad::parametric::RadiusConstraint>(&constraint)) {
            if (!std::isfinite(radius->value) || radius->value <= tolerance) {
                result.status = SolveStatus::Failed;
                result.error = "Radius constraint value must be finite and positive";
                return result;
            }
            const auto [it, inserted] = radiusValues.emplace(radius->entityId, radius->value);
            if (!inserted && std::abs(it->second - radius->value) > tolerance) {
                result.status = SolveStatus::Failed;
                result.error = "Conflicting radius constraints";
                return result;
            }
        }
    }

    // A fillet Arc is governed by two tangent constraints and one radius
    // constraint.  Solve that coupled geometry as a unit before the regular
    // constraint pass.  This is important when several fillets share a line:
    // solving each Tangent constraint independently would move the same line
    // endpoint back and forth and prevent the solver from converging.
    const auto isFilletArc = [&](const std::string& entityId) {
        bool hasRadius = false;
        int tangentCount = 0;
        for (const auto& constraint : constraints) {
            if (const auto* radius = std::get_if<cad::parametric::RadiusConstraint>(&constraint)) {
                hasRadius = hasRadius || radius->entityId == entityId;
            } else if (const auto* tangent = std::get_if<cad::parametric::TangentConstraint>(&constraint)) {
                tangentCount += tangent->secondEntityId == entityId ? 1 : 0;
            }
        }
        return hasRadius && tangentCount >= 2;
    };

    bool preSolvedFillets = false;
    for (const auto& constraint : constraints) {
        const auto* radius = std::get_if<cad::parametric::RadiusConstraint>(&constraint);
        if (!radius || !isFilletArc(radius->entityId)) continue;
        auto* entity = findEntity(result.entities, radius->entityId);
        auto* arc = entity ? std::get_if<cad::parametric::SketchArc>(entity) : nullptr;
        if (!arc || !applyFilletRadius(result.entities, constraints, *arc, radius->value, preSolvedFillets)) {
            result.status = SolveStatus::Failed;
            result.error = "Cannot solve fillet radius constraint";
            return result;
        }
    }

    for (int iteration = 0; iteration < 40; ++iteration) {
        bool changed = preSolvedFillets;
        preSolvedFillets = false;
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
            } else if (const auto* coincident = std::get_if<cad::parametric::CoincidentConstraint>(&constraint)) {
                auto* master = findEntity(result.entities, coincident->a.entityId);
                auto* slave = findEntity(result.entities, coincident->b.entityId);
                const auto masterPoint = master ? pointValue(*master, coincident->a.role) : std::nullopt;
                const auto slavePoint = slave ? pointValue(*slave, coincident->b.role) : std::nullopt;
                if (!masterPoint || !slavePoint || !slave
                    || !setPoint(*slave, coincident->b.role, *masterPoint)) {
                    result.status = SolveStatus::Failed; result.error = "Invalid Coincident point reference"; return result;
                }
                if (!samePoint(*masterPoint, *slavePoint)) changed = true;
            } else if (const auto* distance = std::get_if<cad::parametric::DistanceConstraint>(&constraint)) {
                auto* entity = findEntity(result.entities, distance->entityId);
                auto* line = entity ? std::get_if<cad::parametric::SketchLine>(entity) : nullptr;
                if (!line) {
                    result.status = SolveStatus::Failed;
                    result.error = "Distance constraint requires a Line";
                    return result;
                }
                const gp_Pnt2d anchor = distance->anchorStart ? line->start : line->end;
                const gp_Pnt2d moving = distance->anchorStart ? line->end : line->start;
                gp_Vec2d direction(anchor, moving);
                if (direction.SquareMagnitude() <= tolerance * tolerance) {
                    result.status = SolveStatus::Failed;
                    result.error = "Cannot solve zero-length line distance";
                    return result;
                }
                direction.Normalize();
                const gp_Pnt2d solved(anchor.X() + direction.X() * distance->value,
                    anchor.Y() + direction.Y() * distance->value);
                if (distance->anchorStart) {
                    if (!samePoint(line->end, solved)) { line->end = solved; changed = true; }
                } else if (!samePoint(line->start, solved)) { line->start = solved; changed = true; }
            } else if (const auto* horizontalDistance = std::get_if<cad::parametric::HorizontalDistanceConstraint>(&constraint)) {
                auto* first = findEntity(result.entities, horizontalDistance->first.entityId);
                auto* second = findEntity(result.entities, horizontalDistance->second.entityId);
                const auto firstPoint = first ? pointValue(*first, horizontalDistance->first.role) : std::nullopt;
                const auto secondPoint = second ? pointValue(*second, horizontalDistance->second.role) : std::nullopt;
                if (!first || !second || !firstPoint || !secondPoint
                    || !setAxisCoordinate(*second, horizontalDistance->second.role,
                        {firstPoint->X() + horizontalDistance->value, secondPoint->Y()}, true)) {
                    result.status = SolveStatus::Failed;
                    result.error = "Invalid HorizontalDistance point reference";
                    return result;
                }
                if (std::abs(secondPoint->X() - firstPoint->X() - horizontalDistance->value) > tolerance) changed = true;
            } else if (const auto* verticalDistance = std::get_if<cad::parametric::VerticalDistanceConstraint>(&constraint)) {
                auto* first = findEntity(result.entities, verticalDistance->first.entityId);
                auto* second = findEntity(result.entities, verticalDistance->second.entityId);
                const auto firstPoint = first ? pointValue(*first, verticalDistance->first.role) : std::nullopt;
                const auto secondPoint = second ? pointValue(*second, verticalDistance->second.role) : std::nullopt;
                if (!first || !second || !firstPoint || !secondPoint
                    || !setAxisCoordinate(*second, verticalDistance->second.role,
                        {secondPoint->X(), firstPoint->Y() + verticalDistance->value}, false)) {
                    result.status = SolveStatus::Failed;
                    result.error = "Invalid VerticalDistance point reference";
                    return result;
                }
                if (std::abs(secondPoint->Y() - firstPoint->Y() - verticalDistance->value) > tolerance) changed = true;
            } else if (const auto* angle = std::get_if<cad::parametric::AngleConstraint>(&constraint)) {
                auto* entity = findEntity(result.entities, angle->entityId);
                auto* line = entity ? std::get_if<cad::parametric::SketchLine>(entity) : nullptr;
                if (!line) { result.status = SolveStatus::Failed; result.error = "Angle constraint requires a Line"; return result; }
                const gp_Pnt2d anchor = angle->anchorStart ? line->start : line->end;
                const gp_Pnt2d moving = angle->anchorStart ? line->end : line->start;
                const double length = anchor.Distance(moving);
                if (length <= tolerance) { result.status = SolveStatus::Failed; result.error = "Cannot solve angle for zero-length Line"; return result; }
                const gp_Pnt2d solved(anchor.X() + length * std::cos(angle->radians),
                    anchor.Y() + length * std::sin(angle->radians));
                if (angle->anchorStart) {
                    if (!samePoint(line->end, solved)) { line->end = solved; changed = true; }
                } else if (!samePoint(line->start, solved)) { line->start = solved; changed = true; }
            } else if (const auto* parallel = std::get_if<cad::parametric::ParallelConstraint>(&constraint)) {
                auto* first = lineById(result.entities, parallel->firstLineId);
                auto* second = lineById(result.entities, parallel->secondLineId);
                if (!first || !second) { result.status = SolveStatus::Failed; result.error = "Parallel constraint requires two Lines"; return result; }
                if (!applyLineOrientation(*second, *first, false, parallel->anchorStart, tolerance, changed)) {
                    result.status = SolveStatus::Failed; result.error = "Cannot solve Parallel constraint for zero-length Line"; return result;
                }
            } else if (const auto* perpendicular = std::get_if<cad::parametric::PerpendicularConstraint>(&constraint)) {
                auto* first = lineById(result.entities, perpendicular->firstLineId);
                auto* second = lineById(result.entities, perpendicular->secondLineId);
                if (!first || !second) { result.status = SolveStatus::Failed; result.error = "Perpendicular constraint requires two Lines"; return result; }
                if (!applyLineOrientation(*second, *first, true, perpendicular->anchorStart, tolerance, changed)) {
                    result.status = SolveStatus::Failed; result.error = "Cannot solve Perpendicular constraint for zero-length Line"; return result;
                }
            } else if (const auto* angleBetween = std::get_if<cad::parametric::AngleBetweenLinesConstraint>(&constraint)) {
                auto* reference = lineById(result.entities, angleBetween->referenceLineId);
                auto* dependent = lineById(result.entities, angleBetween->dependentLineId);
                if (!reference || !dependent) {
                    result.status = SolveStatus::Failed;
                    result.error = "AngleBetweenLines constraint requires two Lines";
                    return result;
                }
                if (!applyAngleBetweenLines(*dependent, *reference, angleBetween->angleRadians,
                    angleBetween->anchorStart, tolerance, changed)) {
                    result.status = SolveStatus::Failed;
                    result.error = "Cannot solve AngleBetweenLines for zero-length Line";
                    return result;
                }
            } else if (const auto* tangent = std::get_if<cad::parametric::TangentConstraint>(&constraint)) {
                auto* first = findEntity(result.entities, tangent->firstEntityId);
                auto* second = findEntity(result.entities, tangent->secondEntityId);
                auto* line = first ? std::get_if<cad::parametric::SketchLine>(first) : nullptr;
                const auto* circle = second ? std::get_if<cad::parametric::SketchCircle>(second) : nullptr;
                auto* arc = second ? std::get_if<cad::parametric::SketchArc>(second) : nullptr;
                if (!line || (!circle && !arc)) { result.status = SolveStatus::Failed; result.error = "Tangent currently supports Line to Circle or Arc"; return result; }
                // applyFilletRadius already solves both tangent points for a
                // fillet Arc.  Running the generic one-sided tangent solver
                // afterwards would undo the shared-line geometry.
                const bool solved = arc && isFilletArc(arc->id)
                    ? true
                    : arc
                        ? applyLineArcTangent(*line, *arc, changed)
                        : applyTangent(*line, circle->center, circle->radius, nullptr, changed);
                if (!solved) {
                    result.status = SolveStatus::Failed;
                    result.error = arc
                        ? "Tangent cannot be satisfied on current Arc (line="
                            + tangent->firstEntityId + ", arc=" + tangent->secondEntityId
                            + ", lineLength=" + std::to_string(line ? line->start.Distance(line->end) : 0.0)
                            + ", radius=" + std::to_string(arc ? arc->radius : 0.0) + ")"
                        : "Tangent cannot be satisfied";
                    return result;
                }
            } else if (const auto* equal = std::get_if<cad::parametric::EqualConstraint>(&constraint)) {
                auto* first = findEntity(result.entities, equal->referenceEntityId);
                auto* second = findEntity(result.entities, equal->dependentEntityId);
                if (auto* firstLine = first ? std::get_if<cad::parametric::SketchLine>(first) : nullptr) {
                    auto* secondLine = second ? std::get_if<cad::parametric::SketchLine>(second) : nullptr;
                    if (!secondLine) { result.status = SolveStatus::Failed; result.error = "Equal constraint requires compatible entity types"; return result; }
                    gp_Vec2d direction(secondLine->start, secondLine->end);
                    const double length = firstLine->start.Distance(firstLine->end);
                    if (direction.SquareMagnitude() <= tolerance * tolerance || length <= tolerance) { result.status = SolveStatus::Failed; result.error = "Equal cannot solve zero-length Line"; return result; }
                    direction.Normalize();
                    const gp_Pnt2d solved(secondLine->start.X() + direction.X() * length, secondLine->start.Y() + direction.Y() * length);
                    if (!samePoint(secondLine->end, solved)) { secondLine->end = solved; changed = true; }
                } else {
                    const double radius = std::visit([](const auto& item) {
                        using T = std::decay_t<decltype(item)>;
                        if constexpr (std::is_same_v<T, cad::parametric::SketchCircle>
                            || std::is_same_v<T, cad::parametric::SketchArc>) return item.radius;
                        return 0.0;
                    }, *first);
                    if (auto* circle = second ? std::get_if<cad::parametric::SketchCircle>(second) : nullptr) {
                        if (std::abs(circle->radius - radius) > tolerance) { circle->radius = radius; changed = true; }
                    } else if (auto* arc = second ? std::get_if<cad::parametric::SketchArc>(second) : nullptr) {
                        if (std::abs(arc->radius - radius) > tolerance) { arc->radius = radius; changed = true; }
                    }
                }
            } else {
                const auto& radius = std::get<cad::parametric::RadiusConstraint>(constraint);
                auto* entity = findEntity(result.entities, radius.entityId);
                if (!entity) {
                    result.status = SolveStatus::Failed;
                    result.error = "Radius constraint target entity is missing";
                    return result;
                }
                if (auto* circle = std::get_if<cad::parametric::SketchCircle>(entity)) {
                    if (std::abs(circle->radius - radius.value) > tolerance) {
                        circle->radius = radius.value; changed = true;
                    }
                } else if (auto* arc = std::get_if<cad::parametric::SketchArc>(entity)) {
                    if (!applyFilletRadius(result.entities, constraints, *arc, radius.value, changed)
                        && std::abs(arc->radius - radius.value) > tolerance) {
                        arc->radius = radius.value;
                        changed = true;
                    }
                } else {
                    result.status = SolveStatus::Failed;
                    result.error = "Radius constraint requires a Circle or Arc";
                    return result;
                }
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
            } else if constexpr (std::is_same_v<T, cad::parametric::SketchCircle>) {
                const double distance = point.Distance(value.center);
                if (distance <= best) {
                    best = distance;
                    result = {{value.id, cad::parametric::SketchPointRole::CircleCenter}};
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

std::optional<cad::parametric::SketchEntityId> SketchConstraintSolver::circleOrArcAt(
    const std::vector<cad::parametric::SketchEntity>& entities,
    const gp_Pnt2d& point, const double hitTolerance)
{
    double best = hitTolerance;
    std::optional<cad::parametric::SketchEntityId> result;
    for (const auto& entity : entities) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, cad::parametric::SketchCircle>) {
                const double distance = std::abs(value.center.Distance(point) - value.radius);
                if (distance <= best) { best = distance; result = value.id; }
            } else if constexpr (std::is_same_v<T, cad::parametric::SketchArc>) {
                const double distance = std::abs(value.center.Distance(point) - value.radius);
                if (distance <= best) { best = distance; result = value.id; }
            }
        }, entity);
    }
    return result;
}

} // namespace cad::operations
