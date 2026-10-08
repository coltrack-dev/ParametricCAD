#include "operations/SketchExtendService.h"

#include "operations/SketchTrimService.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cad::operations {
namespace {
constexpr double twoPi = 6.283185307179586476925286766559;
constexpr double geometryTolerance = 1.0e-7;
constexpr double minimumExtension = 1.0e-6;
constexpr double rayLength = 1.0e9;

double positiveAngle(double angle)
{
    angle = std::fmod(angle, twoPi);
    if (angle < 0.0) angle += twoPi;
    return angle;
}

double traversalDelta(const double from, const double to, const bool clockwise)
{
    double delta = clockwise ? from - to : to - from;
    return positiveAngle(delta);
}

bool isExtendable(const cad::parametric::SketchEntity& entity)
{
    return std::holds_alternative<cad::parametric::SketchLine>(entity)
        || std::holds_alternative<cad::parametric::SketchArc>(entity);
}

struct Candidate
{
    gp_Pnt2d point;
    double distance{0.0};
};

std::optional<Candidate> nearestLineCandidate(
    const cad::parametric::SketchFeature& sketch,
    const std::size_t targetIndex,
    const cad::parametric::SketchLine& line,
    const ExtendEndpoint endpoint)
{
    const gp_Pnt2d origin = endpoint == ExtendEndpoint::End ? line.end : line.start;
    gp_Vec2d direction(origin, endpoint == ExtendEndpoint::End ? line.start : line.end);
    direction.Reverse();
    const double magnitude = std::sqrt(direction.SquareMagnitude());
    if (magnitude <= geometryTolerance) return std::nullopt;
    direction /= magnitude;
    const cad::parametric::SketchLine ray{
        origin, {origin.X() + direction.X() * rayLength,
                 origin.Y() + direction.Y() * rayLength}};
    std::optional<Candidate> best;
    for (std::size_t i = 0; i < sketch.entities().size(); ++i) {
        if (i == targetIndex) continue;
        for (const auto& hit : SketchTrimService::intersections(ray, sketch.entities()[i])) {
            const auto point = hit.point;
            const gp_Vec2d offset(origin, point);
            const double distance = offset.Dot(direction);
            if (distance <= geometryTolerance) continue;
            if (!best || distance < best->distance) best = Candidate{point, distance};
        }
    }
    return best;
}

std::optional<Candidate> nearestArcCandidate(
    const cad::parametric::SketchFeature& sketch,
    const std::size_t targetIndex,
    const cad::parametric::SketchArc& arc,
    const ExtendEndpoint endpoint)
{
    const double sweep = arc.signedSweep();
    const double sign = sweep < 0.0 ? -1.0 : 1.0;
    const double endpointAngle = endpoint == ExtendEndpoint::End ? arc.endAngle : arc.startAngle;
    std::optional<Candidate> best;
    const cad::parametric::SketchCircle underlying{arc.center, arc.radius};
    for (std::size_t i = 0; i < sketch.entities().size(); ++i) {
        if (i == targetIndex) continue;
        for (const auto& hit : SketchTrimService::intersections(underlying, sketch.entities()[i])) {
            const double angle = std::atan2(hit.point.Y() - arc.center.Y(),
                                            hit.point.X() - arc.center.X());
            const double travel = endpoint == ExtendEndpoint::End
                ? traversalDelta(endpointAngle, angle, arc.clockwise)
                : traversalDelta(angle, endpointAngle, arc.clockwise);
            if (travel <= geometryTolerance) continue;
            if (std::abs(sweep) + travel >= twoPi - geometryTolerance) continue;
            const double distance = arc.radius * travel;
            if (!best || distance < best->distance) best = Candidate{hit.point, distance};
        }
    }
    (void)sign;
    return best;
}

} // namespace

ExtendPlan SketchExtendService::analyzeExtend(
    const cad::parametric::SketchFeature& sketch,
    const gp_Pnt2d& click, const double endpointTolerance)
{
    ExtendPlan result;
    double bestDistance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < sketch.entities().size(); ++i) {
        const auto& entity = sketch.entities()[i];
        if (!isExtendable(entity)) continue;
        const auto endpointDistance = [&](const gp_Pnt2d& point, const ExtendEndpoint endpoint) {
            const double distance = click.Distance(point);
            if (distance < bestDistance) {
                bestDistance = distance;
                result.entityIndex = i;
                result.endpoint = endpoint;
            }
        };
        if (const auto* line = std::get_if<cad::parametric::SketchLine>(&entity)) {
            endpointDistance(line->start, ExtendEndpoint::Start);
            endpointDistance(line->end, ExtendEndpoint::End);
        } else {
            const auto& arc = std::get<cad::parametric::SketchArc>(entity);
            endpointDistance(arc.startPoint(), ExtendEndpoint::Start);
            endpointDistance(arc.endPoint(), ExtendEndpoint::End);
        }
    }
    if (bestDistance > endpointTolerance || bestDistance == std::numeric_limits<double>::max()) {
        result.error = "No extendable endpoint at click";
        return result;
    }
    result.originalEntity = sketch.entities()[result.entityIndex];
    std::optional<Candidate> candidate;
    if (const auto* line = std::get_if<cad::parametric::SketchLine>(&result.originalEntity)) {
        candidate = nearestLineCandidate(sketch, result.entityIndex, *line, result.endpoint);
        if (!candidate) { result.error = "No Extend boundary found"; return result; }
        const gp_Pnt2d point = candidate->point;
        if (result.endpoint == ExtendEndpoint::End)
            result.extendedEntity = cad::parametric::SketchLine{
                line->start, point, line->id, line->construction};
        else result.extendedEntity = cad::parametric::SketchLine{
            point, line->end, line->id, line->construction};
        const gp_Pnt2d endpoint = result.endpoint == ExtendEndpoint::End ? line->end : line->start;
        result.extensionSpan.push_back(cad::parametric::SketchLine{endpoint, point});
    } else {
        const auto* arc = std::get_if<cad::parametric::SketchArc>(&result.originalEntity);
        candidate = nearestArcCandidate(sketch, result.entityIndex, *arc, result.endpoint);
        if (!candidate) { result.error = "No Extend boundary found"; return result; }
        const double angle = std::atan2(candidate->point.Y() - arc->center.Y(),
                                        candidate->point.X() - arc->center.X());
        const double travel = candidate->distance / arc->radius;
        const double signedTravel = arc->clockwise ? -travel : travel;
        if (result.endpoint == ExtendEndpoint::End) {
            result.extendedEntity = cad::parametric::SketchArc{
                arc->center, arc->radius, arc->startAngle,
                arc->endAngle + signedTravel, arc->clockwise, arc->id, arc->construction};
            result.extensionSpan.push_back(cad::parametric::SketchArc{
                arc->center, arc->radius, arc->endAngle, angle, arc->clockwise});
        } else {
            result.extendedEntity = cad::parametric::SketchArc{
                arc->center, arc->radius, arc->startAngle - signedTravel,
                arc->endAngle, arc->clockwise, arc->id, arc->construction};
            result.extensionSpan.push_back(cad::parametric::SketchArc{
                arc->center, arc->radius, angle, arc->startAngle, arc->clockwise});
        }
    }
    result.changed = !result.extensionSpan.empty();
    return result;
}

std::optional<ExtendPlan> SketchExtendService::previewExtend(
    const cad::parametric::SketchFeature& sketch, const gp_Pnt2d& click,
    const double endpointTolerance)
{
    auto plan = analyzeExtend(sketch, click, endpointTolerance);
    if (!plan.changed) return std::nullopt;
    return plan;
}

ExtendPlan SketchExtendService::extend(const cad::parametric::SketchFeature& sketch,
                                       const gp_Pnt2d& click, const double endpointTolerance)
{
    return analyzeExtend(sketch, click, endpointTolerance);
}

} // namespace cad::operations
