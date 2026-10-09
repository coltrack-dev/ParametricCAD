#include "operations/SketchChamferService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace cad::operations {
namespace {
constexpr double tolerance = 1.0e-7;
constexpr double minimumLength = 1.0e-6;
constexpr double pi = 3.1415926535897932384626433832795;

struct LineInfo { std::size_t index; cad::parametric::SketchLine line; };

std::optional<LineInfo> findLine(const cad::parametric::SketchFeature& sketch,
                                 const cad::parametric::SketchEntityId& id)
{
    for (std::size_t index = 0; index < sketch.entities().size(); ++index) {
        if (const auto* line = std::get_if<cad::parametric::SketchLine>(&sketch.entities()[index]);
            line && line->id == id) return LineInfo{index, *line};
    }
    return std::nullopt;
}

std::optional<gp_Pnt2d> sharedVertex(const cad::parametric::SketchLine& first,
                                     const cad::parametric::SketchLine& second)
{
    for (const auto& pair : std::array<std::pair<gp_Pnt2d, gp_Pnt2d>, 4>{
        std::make_pair(first.start, second.start), std::make_pair(first.start, second.end),
        std::make_pair(first.end, second.start), std::make_pair(first.end, second.end)})
        if (pair.first.Distance(pair.second) <= tolerance) return pair.first;
    return std::nullopt;
}

double cross(const gp_Vec2d& a, const gp_Vec2d& b)
{
    return a.X() * b.Y() - a.Y() * b.X();
}

void reject(SketchChamferPlan& plan, SketchChamferError error, std::string message,
            std::vector<cad::parametric::SketchEntityId> affected = {},
            std::optional<double> maximum = std::nullopt)
{
    plan.validation = {false, error, std::move(message), std::move(affected), maximum};
    plan.error = plan.validation.message;
}

void replaceVertex(cad::parametric::SketchLine& line, const gp_Pnt2d& vertex,
                  const gp_Pnt2d& point)
{
    if (line.start.Distance(vertex) <= tolerance) line.start = point;
    else line.end = point;
}

bool validPositive(const double value)
{
    return std::isfinite(value) && value > minimumLength;
}
}

SketchChamferPlan SketchChamferService::analyze(
    const cad::parametric::SketchFeature& sketch,
    const cad::parametric::SketchEntityId& firstLineId,
    const cad::parametric::SketchEntityId& secondLineId,
    const cad::parametric::SketchChamferMode mode,
    const double firstDistance, const double secondDistance,
    const double angleRadians)
{
    SketchChamferPlan result;
    result.firstLineId = firstLineId;
    result.secondLineId = secondLineId;
    result.mode = mode;
    result.firstDistance = firstDistance;
    result.secondDistance = secondDistance;
    result.angleRadians = angleRadians;
    const std::vector<cad::parametric::SketchEntityId> affected{firstLineId, secondLineId};
    if (firstLineId == secondLineId) {
        reject(result, SketchChamferError::NoCornerSelected,
            "Sketch Chamfer requires two different Lines", affected);
        return result;
    }
    if (!validPositive(firstDistance)
        || (mode == cad::parametric::SketchChamferMode::TwoDistances && !validPositive(secondDistance))) {
        reject(result, SketchChamferError::InvalidDistance,
            "Sketch Chamfer distances must be finite and positive", affected);
        return result;
    }
    if (mode == cad::parametric::SketchChamferMode::DistanceAngle
        && (!std::isfinite(angleRadians) || angleRadians <= tolerance || angleRadians >= pi - tolerance)) {
        reject(result, SketchChamferError::InvalidAngle,
            "Sketch Chamfer angle must be between 0 and 180 degrees", affected);
        return result;
    }
    const auto first = findLine(sketch, firstLineId);
    const auto second = findLine(sketch, secondLineId);
    if (!first || !second) {
        reject(result, SketchChamferError::NoCornerSelected,
            "Sketch Chamfer requires two existing Lines", affected);
        return result;
    }
    const auto vertex = sharedVertex(first->line, second->line);
    if (!vertex) {
        reject(result, SketchChamferError::NoCornerSelected,
            "Sketch Chamfer requires connected Line endpoints", affected);
        return result;
    }
    const gp_Pnt2d firstOuter = vertex->Distance(first->line.start) <= tolerance
        ? first->line.end : first->line.start;
    const gp_Pnt2d secondOuter = vertex->Distance(second->line.start) <= tolerance
        ? second->line.end : second->line.start;
    const double firstLength = vertex->Distance(firstOuter);
    const double secondLength = vertex->Distance(secondOuter);
    if (firstLength <= minimumLength || secondLength <= minimumLength) {
        reject(result, SketchChamferError::DegenerateLine,
            "Sketch Chamfer cannot use a zero-length Line", affected);
        return result;
    }
    gp_Vec2d firstRay(*vertex, firstOuter);
    gp_Vec2d secondRay(*vertex, secondOuter);
    firstRay.Normalize();
    secondRay.Normalize();
    double d1 = firstDistance;
    double d2 = mode == cad::parametric::SketchChamferMode::EqualDistance
        ? firstDistance : secondDistance;
    gp_Pnt2d firstCut(vertex->X() + firstRay.X() * d1, vertex->Y() + firstRay.Y() * d1);
    gp_Pnt2d secondCut;
    if (mode == cad::parametric::SketchChamferMode::DistanceAngle) {
        const gp_Vec2d fromFirst(vertex->X() - firstCut.X(), vertex->Y() - firstCut.Y());
        const std::array<double, 2> signs{1.0, -1.0};
        bool found = false;
        for (const double sign : signs) {
            const double chamferTurn = pi - angleRadians;
            const double c = std::cos(sign * chamferTurn);
            const double s = std::sin(sign * chamferTurn);
            const gp_Vec2d direction(firstRay.X() * c - firstRay.Y() * s,
                                      firstRay.X() * s + firstRay.Y() * c);
            const double denominator = cross(direction, secondRay);
            if (std::abs(denominator) <= tolerance) continue;
            const double alongChamfer = cross(gp_Vec2d(firstCut, *vertex), secondRay) / denominator;
            const double alongSecond = cross(fromFirst, direction) / denominator;
            if (alongChamfer > minimumLength && alongSecond > minimumLength) {
                secondCut = {firstCut.X() + direction.X() * alongChamfer,
                             firstCut.Y() + direction.Y() * alongChamfer};
                d2 = alongSecond;
                found = true;
                break;
            }
        }
        if (!found) {
            reject(result, SketchChamferError::InvalidAngle,
                "Sketch Chamfer angle does not intersect the second Line in its valid direction", affected);
            return result;
        }
    } else {
        secondCut = {vertex->X() + secondRay.X() * d2, vertex->Y() + secondRay.Y() * d2};
    }
    if (d1 >= firstLength - minimumLength || d2 >= secondLength - minimumLength) {
        reject(result, SketchChamferError::DistanceTooLarge,
            "Sketch Chamfer distance exceeds the available Line length", affected,
            std::min(firstLength - minimumLength, secondLength - minimumLength));
        return result;
    }
    if (firstCut.Distance(secondCut) <= minimumLength) {
        reject(result, SketchChamferError::OverlappingChamfers,
            "Sketch Chamfer produced a zero-length chamfer Line", affected);
        return result;
    }

    auto firstLine = first->line;
    auto secondLine = second->line;
    replaceVertex(firstLine, *vertex, firstCut);
    replaceVertex(secondLine, *vertex, secondCut);
    cad::parametric::SketchLine chamfer{firstCut, secondCut, "chamfer-preview", false};
    result.entities = sketch.entities();
    result.entities[first->index] = firstLine;
    result.entities[second->index] = secondLine;
    result.entities.push_back(chamfer);
    result.chamferLineId = chamfer.id;
    const auto firstRole = first->line.start.Distance(*vertex) <= tolerance
        ? cad::parametric::SketchPointRole::LineStart : cad::parametric::SketchPointRole::LineEnd;
    const auto secondRole = second->line.start.Distance(*vertex) <= tolerance
        ? cad::parametric::SketchPointRole::LineStart : cad::parametric::SketchPointRole::LineEnd;
    for (const auto& constraint : sketch.constraints()) {
        if (const auto* coincident = std::get_if<cad::parametric::CoincidentConstraint>(&constraint);
            coincident && ((coincident->a.entityId == firstLineId && coincident->a.role == firstRole)
                || (coincident->a.entityId == secondLineId && coincident->a.role == secondRole)
                || (coincident->b.entityId == firstLineId && coincident->b.role == firstRole)
                || (coincident->b.entityId == secondLineId && coincident->b.role == secondRole))) continue;
        result.constraints.push_back(constraint);
    }
    result.changed = true;
    result.validation = {true, SketchChamferError::None, "Sketch Chamfer geometry is valid", affected, std::nullopt};
    return result;
}

std::optional<std::pair<cad::parametric::SketchEntityId,
                        cad::parametric::SketchEntityId>> SketchChamferService::cornerAt(
    const std::vector<cad::parametric::SketchEntity>& entities,
    const gp_Pnt2d& point, const double hitTolerance)
{
    double best = hitTolerance;
    std::optional<std::pair<cad::parametric::SketchEntityId,
                             cad::parametric::SketchEntityId>> result;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto* first = std::get_if<cad::parametric::SketchLine>(&entities[i]);
        if (!first || first->construction) continue;
        for (std::size_t j = i + 1; j < entities.size(); ++j) {
            const auto* second = std::get_if<cad::parametric::SketchLine>(&entities[j]);
            if (!second || second->construction) continue;
            const auto vertex = sharedVertex(*first, *second);
            if (!vertex) continue;
            const double distance = point.Distance(*vertex);
            if (distance < best) {
                best = distance;
                result = std::make_pair(first->id, second->id);
            }
        }
    }
    return result;
}

} // namespace cad::operations
