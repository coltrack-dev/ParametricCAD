#include "operations/SketchFilletService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace cad::operations {
namespace {
constexpr double tolerance = 1.0e-7;
constexpr double minimumLength = 1.0e-6;
constexpr double pi = 3.1415926535897932384626433832795;

struct LineInfo
{
    std::size_t index{0};
    cad::parametric::SketchLine line;
};

std::optional<LineInfo> findLine(const cad::parametric::SketchFeature& sketch,
                                 const cad::parametric::SketchEntityId& id)
{
    for (std::size_t index = 0; index < sketch.entities().size(); ++index) {
        if (const auto* line = std::get_if<cad::parametric::SketchLine>(&sketch.entities()[index]);
            line && line->id == id) return LineInfo{index, *line};
    }
    return std::nullopt;
}

double cross(const gp_Vec2d& a, const gp_Vec2d& b)
{
    return a.X() * b.Y() - a.Y() * b.X();
}

void replaceLine(cad::parametric::SketchLine& line, const gp_Pnt2d& vertex,
                 const gp_Pnt2d& tangent)
{
    if (line.start.Distance(vertex) <= tolerance) line.start = tangent;
    else line.end = tangent;
}

std::optional<std::pair<gp_Pnt2d, gp_Pnt2d>> sharedVertex(
    const cad::parametric::SketchLine& first,
    const cad::parametric::SketchLine& second)
{
    const std::array<std::pair<gp_Pnt2d, gp_Pnt2d>, 4> pairs{
        std::make_pair(first.start, second.start),
        std::make_pair(first.start, second.end),
        std::make_pair(first.end, second.start),
        std::make_pair(first.end, second.end)};
    for (const auto& [a, b] : pairs) {
        if (a.Distance(b) <= tolerance) return std::make_pair(a, b);
    }
    return std::nullopt;
}

bool referencesVertex(const cad::parametric::SketchPointRef& ref,
                      const cad::parametric::SketchEntityId& firstId,
                      const cad::parametric::SketchEntityId& secondId,
                      const cad::parametric::SketchPointRole firstRole,
                      const cad::parametric::SketchPointRole secondRole)
{
    return (ref.entityId == firstId && ref.role == firstRole)
        || (ref.entityId == secondId && ref.role == secondRole);
}

void reject(SketchFilletPlan& plan, const SketchFilletError error,
            std::string message,
            std::vector<cad::parametric::SketchEntityId> affected = {},
            const std::optional<double> maximum = std::nullopt)
{
    plan.validation.valid = false;
    plan.validation.error = error;
    plan.validation.message = std::move(message);
    plan.validation.affectedEntities = std::move(affected);
    plan.validation.maxAllowedRadius = maximum;
    plan.error = plan.validation.message;
}
}

SketchFilletPlan SketchFilletService::analyze(
    const cad::parametric::SketchFeature& sketch,
    const cad::parametric::SketchEntityId& firstLineId,
    const cad::parametric::SketchEntityId& secondLineId,
    const double radius)
{
    SketchFilletPlan result;
    result.firstLineId = firstLineId;
    result.secondLineId = secondLineId;
    result.radius = radius;
    if (firstLineId == secondLineId) {
        reject(result, SketchFilletError::NoCornerSelected,
            "Sketch Fillet requires two different Lines", {firstLineId});
        return result;
    }
    if (!std::isfinite(radius) || radius <= tolerance) {
        reject(result, SketchFilletError::InvalidRadius,
            "Sketch Fillet radius must be finite and positive",
            {firstLineId, secondLineId});
        return result;
    }
    const auto first = findLine(sketch, firstLineId);
    const auto second = findLine(sketch, secondLineId);
    if (!first || !second) {
        reject(result, SketchFilletError::NoCornerSelected,
            "Sketch Fillet requires two existing Lines", {firstLineId, secondLineId});
        return result;
    }
    const auto vertexPair = sharedVertex(first->line, second->line);
    if (!vertexPair) {
        reject(result, SketchFilletError::NoCornerSelected,
            "Sketch Fillet requires connected Line endpoints",
            {firstLineId, secondLineId});
        return result;
    }
    const gp_Pnt2d vertex = vertexPair->first;
    const bool firstAtStart = first->line.start.Distance(vertex) <= tolerance;
    const bool secondAtStart = second->line.start.Distance(vertex) <= tolerance;
    const gp_Pnt2d firstOuter = firstAtStart ? first->line.end : first->line.start;
    const gp_Pnt2d secondOuter = secondAtStart ? second->line.end : second->line.start;
    const double firstLength = vertex.Distance(firstOuter);
    const double secondLength = vertex.Distance(secondOuter);
    if (firstLength <= minimumLength || secondLength <= minimumLength) {
        reject(result, SketchFilletError::DegenerateLine,
            "Sketch Fillet cannot use a zero-length Line",
            {firstLineId, secondLineId});
        return result;
    }
    gp_Vec2d firstRay(vertex, firstOuter);
    gp_Vec2d secondRay(vertex, secondOuter);
    firstRay.Normalize();
    secondRay.Normalize();
    const double dot = std::clamp(firstRay.Dot(secondRay), -1.0, 1.0);
    const double theta = std::acos(dot);
    if (!std::isfinite(theta) || theta <= 1.0e-5 || std::abs(pi - theta) <= 1.0e-5) {
        reject(result, SketchFilletError::ParallelLines,
            "Sketch Fillet cannot be constructed for parallel or collinear Lines",
            {firstLineId, secondLineId});
        return result;
    }
    result.maximumRadius = std::min(firstLength, secondLength) * std::tan(theta / 2.0);
    const double tangentDistance = radius / std::tan(theta / 2.0);
    if (!std::isfinite(tangentDistance) || tangentDistance <= minimumLength
        || tangentDistance >= firstLength - minimumLength
        || tangentDistance >= secondLength - minimumLength) {
        const double maximum = std::max(0.0, result.maximumRadius);
        reject(result, SketchFilletError::RadiusTooLarge,
            "Fillet R" + std::to_string(radius) + " cannot be applied: maximum radius is R"
                + std::to_string(maximum) + ".",
            {firstLineId, secondLineId}, maximum);
        return result;
    }
    gp_Vec2d bisector = firstRay + secondRay;
    if (bisector.SquareMagnitude() <= tolerance * tolerance) {
        reject(result, SketchFilletError::ParallelLines,
            "Sketch Fillet has no stable angle bisector",
            {firstLineId, secondLineId});
        return result;
    }
    bisector.Normalize();
    const double centerDistance = radius / std::sin(theta / 2.0);
    const gp_Pnt2d center(vertex.X() + bisector.X() * centerDistance,
                          vertex.Y() + bisector.Y() * centerDistance);
    const gp_Pnt2d firstTangent(vertex.X() + firstRay.X() * tangentDistance,
                                vertex.Y() + firstRay.Y() * tangentDistance);
    const gp_Pnt2d secondTangent(vertex.X() + secondRay.X() * tangentDistance,
                                 vertex.Y() + secondRay.Y() * tangentDistance);
    if (firstTangent.Distance(secondTangent) <= minimumLength) {
        reject(result, SketchFilletError::OverlappingFillets,
            "Sketch Fillet produced coincident tangent points",
            {firstLineId, secondLineId});
        return result;
    }

    auto firstLine = first->line;
    auto secondLine = second->line;
    replaceLine(firstLine, vertex, firstTangent);
    replaceLine(secondLine, vertex, secondTangent);
    const double startAngle = std::atan2(firstTangent.Y() - center.Y(), firstTangent.X() - center.X());
    const double endAngle = std::atan2(secondTangent.Y() - center.Y(), secondTangent.X() - center.X());
    const bool clockwise = cross(
        gp_Vec2d(center, firstTangent), gp_Vec2d(center, secondTangent)) < 0.0;
    cad::parametric::SketchArc arc{center, radius, startAngle, endAngle, clockwise, {}, false};

    result.entities = sketch.entities();
    result.entities[first->index] = firstLine;
    result.entities[second->index] = secondLine;
    result.entities.push_back(arc);
    result.arcId.clear();

    const auto firstVertexRole = firstAtStart
        ? cad::parametric::SketchPointRole::LineStart
        : cad::parametric::SketchPointRole::LineEnd;
    const auto secondVertexRole = secondAtStart
        ? cad::parametric::SketchPointRole::LineStart
        : cad::parametric::SketchPointRole::LineEnd;
    for (const auto& constraint : sketch.constraints()) {
        if (const auto* coincident = std::get_if<cad::parametric::CoincidentConstraint>(&constraint);
            coincident && (referencesVertex(coincident->a, firstLineId, secondLineId,
                firstVertexRole, secondVertexRole)
                || referencesVertex(coincident->b, firstLineId, secondLineId,
                    firstVertexRole, secondVertexRole))) continue;
        result.constraints.push_back(constraint);
    }
    result.changed = true;
    result.validation.valid = true;
    result.validation.error = SketchFilletError::None;
    result.validation.message = "Sketch Fillet geometry is valid";
    result.validation.affectedEntities = {firstLineId, secondLineId};
    result.validation.maxAllowedRadius = result.maximumRadius;
    return result;
}

std::optional<std::pair<cad::parametric::SketchEntityId,
                        cad::parametric::SketchEntityId>> SketchFilletService::cornerAt(
    const std::vector<cad::parametric::SketchEntity>& entities,
    const gp_Pnt2d& point, const double hitTolerance)
{
    double bestDistance = hitTolerance;
    std::optional<std::pair<cad::parametric::SketchEntityId,
                            cad::parametric::SketchEntityId>> result;
    for (std::size_t first = 0; first < entities.size(); ++first) {
        const auto* firstLine = std::get_if<cad::parametric::SketchLine>(&entities[first]);
        if (!firstLine || firstLine->construction) continue;
        for (std::size_t second = first + 1; second < entities.size(); ++second) {
            const auto* secondLine = std::get_if<cad::parametric::SketchLine>(&entities[second]);
            if (!secondLine || secondLine->construction) continue;
            const auto vertex = sharedVertex(*firstLine, *secondLine);
            if (!vertex) continue;
            const double distance = point.Distance(vertex->first);
            if (distance < bestDistance) {
                bestDistance = distance;
                result = std::make_pair(firstLine->id, secondLine->id);
            }
        }
    }
    return result;
}

} // namespace cad::operations
