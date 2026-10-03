#include "operations/SketchTrimService.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cad::operations {
namespace {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;
constexpr double tolerance = 1.0e-7;
constexpr double minimumLength = 1.0e-6;

double clamp01(double value) { return std::clamp(value, 0.0, 1.0); }
double cross(const gp_Vec2d& a, const gp_Vec2d& b) { return a.X() * b.Y() - a.Y() * b.X(); }
double dot(const gp_Vec2d& a, const gp_Vec2d& b) { return a.X() * b.X() + a.Y() * b.Y(); }

double positiveAngle(double angle)
{
    angle = std::fmod(angle, twoPi);
    if (angle < 0.0) angle += twoPi;
    return angle;
}

double arcSweep(const cad::parametric::SketchArc& arc)
{
    return arc.signedSweep();
}

bool arcContains(const cad::parametric::SketchArc& arc, const double angle)
{
    const double sweep = arcSweep(arc);
    double delta = angle - arc.startAngle;
    if (sweep > 0.0) {
        while (delta < 0.0) delta += twoPi;
        return delta <= sweep + tolerance;
    }
    while (delta > 0.0) delta -= twoPi;
    return delta >= sweep - tolerance;
}

double parameterOn(const cad::parametric::SketchLine& line, const gp_Pnt2d& point)
{
    const gp_Vec2d direction(line.start, line.end);
    const gp_Vec2d offset(line.start, point);
    const double length2 = dot(direction, direction);
    return length2 <= tolerance ? 0.0 : clamp01(dot(offset, direction) / length2);
}

double parameterOn(const cad::parametric::SketchArc& arc, const gp_Pnt2d& point)
{
    const double angle = std::atan2(point.Y() - arc.center.Y(), point.X() - arc.center.X());
    const double sweep = arcSweep(arc);
    double delta = angle - arc.startAngle;
    if (sweep > 0.0) while (delta < 0.0) delta += twoPi;
    else while (delta > 0.0) delta -= twoPi;
    return clamp01(std::abs(sweep) <= tolerance ? 0.0 : delta / sweep);
}

double circleParameter(const gp_Pnt2d& center, const gp_Pnt2d& point)
{
    return positiveAngle(std::atan2(point.Y() - center.Y(), point.X() - center.X())) / twoPi;
}

gp_Pnt2d pointAt(const cad::parametric::SketchLine& line, double t)
{
    return {line.start.X() + t * (line.end.X() - line.start.X()),
            line.start.Y() + t * (line.end.Y() - line.start.Y())};
}

gp_Pnt2d pointAt(const cad::parametric::SketchArc& arc, double t)
{
    const double angle = arc.startAngle + arcSweep(arc) * t;
    return {arc.center.X() + arc.radius * std::cos(angle),
            arc.center.Y() + arc.radius * std::sin(angle)};
}

void addPoint(std::vector<gp_Pnt2d>& points, const gp_Pnt2d& point)
{
    for (const auto& existing : points) if (existing.Distance(point) <= tolerance) return;
    points.push_back(point);
}

std::vector<gp_Pnt2d> circleCircle(const gp_Pnt2d& c1, double r1,
                                   const gp_Pnt2d& c2, double r2)
{
    const double dx = c2.X() - c1.X(), dy = c2.Y() - c1.Y();
    const double distance = std::hypot(dx, dy);
    if (distance <= tolerance || distance > r1 + r2 + tolerance
        || distance < std::abs(r1 - r2) - tolerance) return {};
    const double a = (r1 * r1 - r2 * r2 + distance * distance) / (2.0 * distance);
    const double h2 = r1 * r1 - a * a;
    if (h2 < -tolerance) return {};
    const double h = std::sqrt(std::max(0.0, h2));
    const double ux = dx / distance, uy = dy / distance;
    const gp_Pnt2d base(c1.X() + a * ux, c1.Y() + a * uy);
    std::vector<gp_Pnt2d> result{{base.X() - h * uy, base.Y() + h * ux}};
    if (h > tolerance) result.push_back({base.X() + h * uy, base.Y() - h * ux});
    return result;
}

std::vector<gp_Pnt2d> lineCircle(const cad::parametric::SketchLine& line,
                                 const gp_Pnt2d& center, double radius)
{
    const gp_Vec2d d(line.start, line.end), f(center, line.start);
    const double a = dot(d, d);
    if (a <= tolerance) return {};
    const double b = 2.0 * dot(f, d);
    const double c = dot(f, f) - radius * radius;
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < -tolerance) return {};
    const double root = std::sqrt(std::max(0.0, discriminant));
    std::vector<gp_Pnt2d> result;
    for (const double t : {(-b - root) / (2.0 * a), (-b + root) / (2.0 * a)}) {
        if (t >= -tolerance && t <= 1.0 + tolerance) addPoint(result, pointAt(line, clamp01(t)));
    }
    return result;
}

bool coincident(const cad::parametric::SketchEntity& a, const cad::parametric::SketchEntity& b)
{
    const auto circleData = [](const cad::parametric::SketchEntity& entity)
        -> std::optional<std::pair<gp_Pnt2d, double>> {
        if (const auto* circle = std::get_if<cad::parametric::SketchCircle>(&entity))
            return std::make_pair(circle->center, circle->radius);
        if (const auto* arc = std::get_if<cad::parametric::SketchArc>(&entity))
            return std::make_pair(arc->center, arc->radius);
        return std::nullopt;
    };
    if (const auto ca = circleData(a); ca) if (const auto cb = circleData(b); cb)
        return ca->first.Distance(cb->first) <= tolerance && std::abs(ca->second - cb->second) <= tolerance;
    const auto* la = std::get_if<cad::parametric::SketchLine>(&a);
    const auto* lb = std::get_if<cad::parametric::SketchLine>(&b);
    if (la && lb) {
        const gp_Vec2d d(la->start, la->end), offset(la->start, lb->start);
        return std::abs(cross(d, offset)) <= tolerance &&
            std::abs(cross(d, gp_Vec2d(la->start, lb->end))) <= tolerance;
    }
    return false;
}

std::vector<gp_Pnt2d> rawIntersections(const cad::parametric::SketchEntity& a,
                                       const cad::parametric::SketchEntity& b)
{
    const auto* la = std::get_if<cad::parametric::SketchLine>(&a);
    const auto* lb = std::get_if<cad::parametric::SketchLine>(&b);
    if (la && lb) {
        const gp_Vec2d r(la->start, la->end), s(lb->start, lb->end), qp(la->start, lb->start);
        const double denominator = cross(r, s);
        if (std::abs(denominator) <= tolerance) return {};
        const double t = cross(qp, s) / denominator, u = cross(qp, r) / denominator;
        if (t < -tolerance || t > 1.0 + tolerance || u < -tolerance || u > 1.0 + tolerance) return {};
        return {pointAt(*la, clamp01(t))};
    }
    const auto* ca = std::get_if<cad::parametric::SketchCircle>(&a);
    const auto* aa = std::get_if<cad::parametric::SketchArc>(&a);
    const auto* cb = std::get_if<cad::parametric::SketchCircle>(&b);
    const auto* ab = std::get_if<cad::parametric::SketchArc>(&b);
    std::vector<gp_Pnt2d> points;
    if (la && (cb || ab)) points = lineCircle(*la, cb ? cb->center : ab->center, cb ? cb->radius : ab->radius);
    else if (lb && (ca || aa)) points = lineCircle(*lb, ca ? ca->center : aa->center, ca ? ca->radius : aa->radius);
    else if ((ca || aa) && (cb || ab)) points = circleCircle(ca ? ca->center : aa->center,
        ca ? ca->radius : aa->radius, cb ? cb->center : ab->center, cb ? cb->radius : ab->radius);
    std::vector<gp_Pnt2d> filtered;
    for (const auto& point : points) {
        if (aa && !arcContains(*aa, std::atan2(point.Y() - aa->center.Y(), point.X() - aa->center.X()))) continue;
        if (ab && !arcContains(*ab, std::atan2(point.Y() - ab->center.Y(), point.X() - ab->center.X()))) continue;
        addPoint(filtered, point);
    }
    return filtered;
}

double distanceTo(const cad::parametric::SketchEntity& entity, const gp_Pnt2d& point, double& parameter)
{
    if (const auto* line = std::get_if<cad::parametric::SketchLine>(&entity)) {
        parameter = parameterOn(*line, point);
        return point.Distance(pointAt(*line, parameter));
    }
    if (const auto* arc = std::get_if<cad::parametric::SketchArc>(&entity)) {
        const double angle = std::atan2(point.Y() - arc->center.Y(), point.X() - arc->center.X());
        if (arcContains(*arc, angle)) {
            parameter = parameterOn(*arc, point);
            return point.Distance(pointAt(*arc, parameter));
        }
        const double toStart = point.Distance(arc->startPoint());
        const double toEnd = point.Distance(arc->endPoint());
        parameter = toStart <= toEnd ? 0.0 : 1.0;
        return std::min(toStart, toEnd);
    }
    const auto& circle = std::get<cad::parametric::SketchCircle>(entity);
    parameter = circleParameter(circle.center, point);
    return std::abs(circle.center.Distance(point) - circle.radius);
}

} // namespace

std::vector<SketchIntersection> SketchTrimService::intersections(
    const cad::parametric::SketchEntity& a, const cad::parametric::SketchEntity& b)
{
    std::vector<SketchIntersection> result;
    for (const auto& point : rawIntersections(a, b)) {
        auto parameter = [](const cad::parametric::SketchEntity& entity, const gp_Pnt2d& p) {
            if (const auto* line = std::get_if<cad::parametric::SketchLine>(&entity)) return parameterOn(*line, p);
            if (const auto* arc = std::get_if<cad::parametric::SketchArc>(&entity)) return parameterOn(*arc, p);
            const auto& circle = std::get<cad::parametric::SketchCircle>(entity);
            return circleParameter(circle.center, p);
        };
        const SketchIntersection candidate{point, parameter(a, point), parameter(b, point)};
        bool duplicate = false;
        for (const auto& existing : result) if (existing.point.Distance(point) <= tolerance) duplicate = true;
        if (!duplicate) result.push_back(candidate);
    }
    return result;
}

TrimPlan SketchTrimService::analyzeTrim(const cad::parametric::SketchFeature& sketch,
                                        const gp_Pnt2d& click, const double hitTolerance)
{
    TrimPlan result;
    double bestDistance = std::numeric_limits<double>::max(), clickParameter = 0.0;
    for (std::size_t i = 0; i < sketch.entities().size(); ++i) {
        double parameter = 0.0;
        const double distance = distanceTo(sketch.entities()[i], click, parameter);
        if (distance < bestDistance) { bestDistance = distance; result.entityIndex = i; clickParameter = parameter; }
    }
    if (bestDistance > hitTolerance || sketch.entities().empty()) { result.error = "No sketch entity at click"; return result; }
    const auto& target = sketch.entities()[result.entityIndex];
    std::vector<double> boundaries;
    for (std::size_t i = 0; i < sketch.entities().size(); ++i) {
        if (i == result.entityIndex) continue;
        if (coincident(target, sketch.entities()[i])) { result.error = "Coincident geometry cannot be trimmed yet"; return result; }
        for (const auto& hit : intersections(target, sketch.entities()[i])) boundaries.push_back(hit.parameterOnA);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end(), [](double a, double b) { return std::abs(a - b) <= tolerance; }), boundaries.end());
    const bool periodic = std::holds_alternative<cad::parametric::SketchCircle>(target);
    if ((!periodic && boundaries.empty()) || (periodic && boundaries.size() < 2)) { result.error = "No trim boundary found"; return result; }
    if (periodic) {
        const auto& circle = std::get<cad::parametric::SketchCircle>(target);
        std::sort(boundaries.begin(), boundaries.end());
        std::size_t interval = boundaries.size() - 1;
        for (std::size_t i = 0; i + 1 < boundaries.size(); ++i)
            if (clickParameter >= boundaries[i] - tolerance && clickParameter <= boundaries[i + 1] + tolerance) { interval = i; break; }
        const double spanStart = boundaries[interval], spanEnd = interval + 1 < boundaries.size() ? boundaries[interval + 1] : boundaries.front() + 1.0;
        const double clickWrapped = clickParameter < spanStart ? clickParameter + 1.0 : clickParameter;
        if (clickWrapped < spanStart - tolerance || clickWrapped > spanEnd + tolerance) { result.error = "Trim not possible at tangent contact"; return result; }
        result.preview = {result.entityIndex, spanStart, spanEnd};
        result.removedEntities.push_back(cad::parametric::SketchArc{
            circle.center, circle.radius, spanStart * twoPi,
            (spanEnd > 1.0 ? spanEnd - 1.0 : spanEnd) * twoPi, false});
        for (std::size_t i = 0; i < boundaries.size(); ++i) {
            const double start = boundaries[i], end = i + 1 < boundaries.size() ? boundaries[i + 1] : boundaries.front() + 1.0;
            if (i == interval) continue;
            const double normalizedEnd = end > 1.0 ? end - 1.0 : end;
            result.replacements.push_back(cad::parametric::SketchArc{circle.center, circle.radius,
                start * twoPi, normalizedEnd * twoPi, false});
        }
    } else {
        boundaries.insert(boundaries.begin(), 0.0); boundaries.push_back(1.0);
        std::size_t interval = boundaries.size() - 2;
        for (std::size_t i = 0; i + 1 < boundaries.size(); ++i)
            if (clickParameter >= boundaries[i] - tolerance && clickParameter <= boundaries[i + 1] + tolerance) { interval = i; break; }
        result.preview = {result.entityIndex, boundaries[interval], boundaries[interval + 1]};
        if (const auto* line = std::get_if<cad::parametric::SketchLine>(&target)) {
            const auto removedStart = pointAt(*line, boundaries[interval]);
            const auto removedEnd = pointAt(*line, boundaries[interval + 1]);
            if (removedStart.Distance(removedEnd) > minimumLength)
                result.removedEntities.push_back(cad::parametric::SketchLine{removedStart, removedEnd});
            for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) if (i != interval) {
                const auto a = pointAt(*line, boundaries[i]), b = pointAt(*line, boundaries[i + 1]);
                if (a.Distance(b) > minimumLength) result.replacements.push_back(cad::parametric::SketchLine{a, b});
            }
        } else {
            const auto& arc = std::get<cad::parametric::SketchArc>(target);
            const double removedSweep = arcSweep(arc)
                * (boundaries[interval + 1] - boundaries[interval]);
            if (std::abs(removedSweep) > tolerance) {
                result.removedEntities.push_back(cad::parametric::SketchArc{
                    arc.center, arc.radius,
                    arc.startAngle + arcSweep(arc) * boundaries[interval],
                    arc.startAngle + removedSweep, arc.clockwise});
            }
            for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) if (i != interval) {
                const double sweep = arcSweep(arc) * (boundaries[i + 1] - boundaries[i]);
                if (std::abs(sweep) > tolerance) result.replacements.push_back(cad::parametric::SketchArc{
                    arc.center, arc.radius, arc.startAngle + arcSweep(arc) * boundaries[i],
                    arc.startAngle + arcSweep(arc) * boundaries[i + 1], arc.clockwise});
            }
        }
    }
    result.changed = !result.removedEntities.empty();
    if (!result.changed) result.error = "Trim not possible at tangent contact";
    return result;
}

std::optional<TrimPlan> SketchTrimService::previewTrim(
    const cad::parametric::SketchFeature& sketch, const gp_Pnt2d& click,
    const double hitTolerance)
{
    auto plan = analyzeTrim(sketch, click, hitTolerance);
    if (!plan.changed || plan.removedEntities.empty()) return std::nullopt;
    return plan;
}

TrimPlan SketchTrimService::trim(const cad::parametric::SketchFeature& sketch,
                                 const gp_Pnt2d& click, const double hitTolerance)
{
    return analyzeTrim(sketch, click, hitTolerance);
}

} // namespace cad::operations
