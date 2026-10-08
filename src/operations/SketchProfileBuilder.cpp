#include "operations/SketchProfileBuilder.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cad::operations {
namespace {

constexpr double endpointTolerance = 1.0e-6;

gp_Pnt worldPoint(const cad::parametric::SketchFrame& frame, const gp_Pnt2d& point)
{
    gp_Pnt result = frame.origin;
    result.Translate(gp_Vec(frame.xDirection) * point.X()
        + gp_Vec(frame.yDirection) * point.Y());
    return result;
}

struct EdgeItem
{
    TopoDS_Edge edge;
    gp_Pnt start;
    gp_Pnt end;
};

bool closeEnough(const gp_Pnt& left, const gp_Pnt& right)
{
    return left.Distance(right) <= endpointTolerance;
}

bool closeEnough(const gp_Pnt2d& left, const gp_Pnt2d& right)
{
    const double dx = left.X() - right.X();
    const double dy = left.Y() - right.Y();
    return dx * dx + dy * dy <= endpointTolerance * endpointTolerance;
}

SketchProfile makeProfile(const TopoDS_Wire& wire)
{
    if (wire.IsNull() || !BRepCheck_Analyzer(wire).IsValid()) {
        throw std::runtime_error("Sketch profile wire is invalid");
    }
    BRepBuilderAPI_MakeFace faceBuilder(wire);
    if (!faceBuilder.IsDone() || faceBuilder.Face().IsNull()
        || !BRepCheck_Analyzer(faceBuilder.Face()).IsValid()) {
        throw std::runtime_error("Sketch profile face construction failed");
    }
    return {wire, faceBuilder.Face(), {faceBuilder.Face()}};
}

[[maybe_unused]] SketchProfile lineProfile(
    const cad::parametric::SketchFrame& frame,
    const std::vector<cad::parametric::SketchLine>& lines)
{
    if (lines.size() < 3) throw std::runtime_error("Sketch profile is open");

    std::vector<EdgeItem> items;
    items.reserve(lines.size());
    for (const auto& line : lines) {
        const gp_Pnt start = worldPoint(frame, line.start);
        const gp_Pnt end = worldPoint(frame, line.end);
        if (start.Distance(end) <= endpointTolerance) {
            throw std::runtime_error("Sketch profile contains a zero-length edge");
        }
        BRepBuilderAPI_MakeEdge builder(start, end);
        if (!builder.IsDone()) throw std::runtime_error("Sketch edge construction failed");
        items.push_back({builder.Edge(), start, end});
    }

    std::vector<bool> used(items.size(), false);
    std::vector<TopoDS_Edge> ordered;
    ordered.reserve(items.size());
    std::size_t current = 0;
    used[current] = true;
    ordered.push_back(items[current].edge);
    gp_Pnt first = items[current].start;
    gp_Pnt tail = items[current].end;

    for (std::size_t count = 1; count < items.size(); ++count) {
        std::vector<std::pair<std::size_t, bool>> candidates;
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (used[index]) continue;
            if (closeEnough(items[index].start, tail)) candidates.emplace_back(index, false);
            if (closeEnough(items[index].end, tail)) candidates.emplace_back(index, true);
        }
        if (candidates.size() != 1) {
            throw std::runtime_error(candidates.empty()
                ? "Sketch profile is open or disconnected"
                : "Sketch profile has branching or ambiguous connectivity");
        }
        const auto [index, reverse] = candidates.front();
        used[index] = true;
        ordered.push_back(reverse ? TopoDS::Edge(items[index].edge.Reversed()) : items[index].edge);
        tail = reverse ? items[index].start : items[index].end;
    }

    if (!closeEnough(tail, first)) throw std::runtime_error("Sketch profile is open");

    BRepBuilderAPI_MakeWire wireBuilder;
    for (const auto& edge : ordered) wireBuilder.Add(edge);
    if (!wireBuilder.IsDone()) throw std::runtime_error("Sketch profile wire assembly failed");
    return makeProfile(wireBuilder.Wire());
}

constexpr double geometryTolerance = 1.0e-7;

double cross2d(const gp_Pnt2d& a, const gp_Pnt2d& b, const gp_Pnt2d& c)
{
    return (b.X() - a.X()) * (c.Y() - a.Y())
        - (b.Y() - a.Y()) * (c.X() - a.X());
}

double distanceSquared2d(const gp_Pnt2d& a, const gp_Pnt2d& b)
{
    const double dx = a.X() - b.X();
    const double dy = a.Y() - b.Y();
    return dx * dx + dy * dy;
}

bool pointOnSegment(const gp_Pnt2d& a, const gp_Pnt2d& b, const gp_Pnt2d& point)
{
    return std::abs(cross2d(a, b, point)) <= geometryTolerance
        && point.X() >= std::min(a.X(), b.X()) - endpointTolerance
        && point.X() <= std::max(a.X(), b.X()) + endpointTolerance
        && point.Y() >= std::min(a.Y(), b.Y()) - endpointTolerance
        && point.Y() <= std::max(a.Y(), b.Y()) + endpointTolerance;
}

bool segmentsIntersect(const gp_Pnt2d& a, const gp_Pnt2d& b,
                       const gp_Pnt2d& c, const gp_Pnt2d& d)
{
    const double abC = cross2d(a, b, c);
    const double abD = cross2d(a, b, d);
    const double cdA = cross2d(c, d, a);
    const double cdB = cross2d(c, d, b);
    const bool proper = ((abC > geometryTolerance && abD < -geometryTolerance)
        || (abC < -geometryTolerance && abD > geometryTolerance))
        && ((cdA > geometryTolerance && cdB < -geometryTolerance)
            || (cdA < -geometryTolerance && cdB > geometryTolerance));
    return proper || pointOnSegment(a, b, c) || pointOnSegment(a, b, d)
        || pointOnSegment(c, d, a) || pointOnSegment(c, d, b);
}

double signedArea(const std::vector<gp_Pnt2d>& points)
{
    double area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& current = points[i];
        const auto& next = points[(i + 1) % points.size()];
        area += current.X() * next.Y() - next.X() * current.Y();
    }
    return area * 0.5;
}

bool pointInPolygon(const std::vector<gp_Pnt2d>& polygon, const gp_Pnt2d& point)
{
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        if (pointOnSegment(polygon[j], polygon[i], point)) return false;
        if (((polygon[i].Y() > point.Y()) != (polygon[j].Y() > point.Y()))
            && point.X() < (polygon[j].X() - polygon[i].X())
                * (point.Y() - polygon[i].Y())
                / (polygon[j].Y() - polygon[i].Y()) + polygon[i].X()) {
            inside = !inside;
        }
    }
    return inside;
}

struct ProfileLoop
{
    TopoDS_Wire wire;
    std::vector<gp_Pnt2d> polygon;
    gp_Pnt2d center;
    double radius{0.0};
    bool circle{false};
    gp_Pnt2d representative;
};

struct SketchEdgeItem
{
    TopoDS_Edge edge;
    gp_Pnt2d start;
    gp_Pnt2d end;
    std::vector<gp_Pnt2d> samples;
};

TopoDS_Wire checkedWire(const std::vector<TopoDS_Edge>& edges)
{
    BRepBuilderAPI_MakeWire builder;
    for (const auto& edge : edges) builder.Add(edge);
    if (!builder.IsDone() || builder.Wire().IsNull()) {
        throw std::runtime_error("Sketch profile wire assembly failed");
    }
    return builder.Wire();
}

SketchEdgeItem arcEdgeItem(const cad::parametric::SketchFrame& frame,
                           const cad::parametric::SketchArc& arc)
{
    if (!std::isfinite(arc.radius) || arc.radius <= endpointTolerance
        || !std::isfinite(arc.startAngle) || !std::isfinite(arc.endAngle)) {
        throw std::runtime_error("Sketch arc has invalid radius or angles");
    }
    const double sweep = arc.signedSweep();
    if (!std::isfinite(sweep) || std::abs(sweep) <= endpointTolerance
        || std::abs(sweep) >= 6.283185307179586 - endpointTolerance) {
        throw std::runtime_error("Sketch arc has invalid sweep");
    }
    const gp_Circ circle(
        gp_Ax2(worldPoint(frame, arc.center), frame.normal), arc.radius);
    BRepBuilderAPI_MakeEdge edgeBuilder;
    if (sweep > 0.0) {
        edgeBuilder = BRepBuilderAPI_MakeEdge(
            circle, arc.startAngle, arc.startAngle + sweep);
    } else {
        edgeBuilder = BRepBuilderAPI_MakeEdge(
            circle, arc.startAngle + sweep, arc.startAngle);
    }
    if (!edgeBuilder.IsDone()) throw std::runtime_error("Sketch arc construction failed");
    TopoDS_Edge edge = edgeBuilder.Edge();
    if (sweep < 0.0) edge = TopoDS::Edge(edge.Reversed());

    const double tolerance = std::max(geometryTolerance, arc.radius * 1.0e-5);
    const double cosine = std::clamp(1.0 - tolerance / arc.radius, -1.0, 1.0);
    const double step = std::max(1.0e-3, 2.0 * std::acos(cosine));
    const int segmentCount = std::max(4, static_cast<int>(std::ceil(std::abs(sweep) / step)));
    std::vector<gp_Pnt2d> samples;
    samples.reserve(static_cast<std::size_t>(segmentCount) + 1);
    for (int index = 0; index <= segmentCount; ++index) {
        const double angle = arc.startAngle + sweep
            * static_cast<double>(index) / static_cast<double>(segmentCount);
        samples.emplace_back(
            arc.center.X() + arc.radius * std::cos(angle),
            arc.center.Y() + arc.radius * std::sin(angle));
    }
    samples.front() = arc.startPoint();
    samples.back() = arc.endPoint();
    return {edge, samples.front(), samples.back(), std::move(samples)};
}

gp_Pnt2d representativeForPolygon(const std::vector<gp_Pnt2d>& polygon)
{
    const auto& a = polygon[0];
    const auto& b = polygon[1];
    const double length = std::sqrt(distanceSquared2d(a, b));
    const double sign = signedArea(polygon) >= 0.0 ? 1.0 : -1.0;
    const double offset = std::max(geometryTolerance * 10.0,
        std::min(length * 0.05, endpointTolerance * 2.0));
    gp_Pnt2d result(
        (a.X() + b.X()) * 0.5 - sign * (b.Y() - a.Y()) / length * offset,
        (a.Y() + b.Y()) * 0.5 + sign * (b.X() - a.X()) / length * offset);
    if (!pointInPolygon(polygon, result)) {
        throw std::runtime_error("Unable to find an interior point for Sketch loop");
    }
    return result;
}

void validateSelfIntersection(const std::vector<gp_Pnt2d>& polygon)
{
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const std::size_t next = (i + 1) % polygon.size();
        for (std::size_t j = i + 1; j < polygon.size(); ++j) {
            const std::size_t otherNext = (j + 1) % polygon.size();
            if (i == j || next == j || otherNext == i) continue;
            if (segmentsIntersect(polygon[i], polygon[next],
                    polygon[j], polygon[otherNext])) {
                throw std::runtime_error("Sketch loop is self-intersecting");
            }
        }
    }
}

bool contains(const ProfileLoop& loop, const gp_Pnt2d& point)
{
    if (loop.circle) {
        return distanceSquared2d(loop.center, point)
            < (loop.radius - geometryTolerance) * (loop.radius - geometryTolerance);
    }
    return pointInPolygon(loop.polygon, point);
}

double pointSegmentDistanceSquared(const gp_Pnt2d& point,
                                   const gp_Pnt2d& start,
                                   const gp_Pnt2d& end)
{
    const double dx = end.X() - start.X();
    const double dy = end.Y() - start.Y();
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= geometryTolerance * geometryTolerance) {
        return distanceSquared2d(point, start);
    }
    const double t = std::clamp(
        ((point.X() - start.X()) * dx + (point.Y() - start.Y()) * dy)
            / lengthSquared, 0.0, 1.0);
    const gp_Pnt2d projection(start.X() + t * dx, start.Y() + t * dy);
    return distanceSquared2d(point, projection);
}

bool intersects(const ProfileLoop& left, const ProfileLoop& right)
{
    if (left.circle && right.circle) {
        const double distance = std::sqrt(distanceSquared2d(left.center, right.center));
        return distance <= left.radius + right.radius + endpointTolerance
            && distance + std::min(left.radius, right.radius)
                >= std::max(left.radius, right.radius) - endpointTolerance;
    }
    if (left.circle || right.circle) {
        const auto& circle = left.circle ? left : right;
        const auto& polygon = left.circle ? right : left;
        for (std::size_t i = 0; i < polygon.polygon.size(); ++i) {
            if (pointSegmentDistanceSquared(circle.center, polygon.polygon[i],
                    polygon.polygon[(i + 1) % polygon.polygon.size()])
                <= (circle.radius + endpointTolerance)
                    * (circle.radius + endpointTolerance)) return true;
        }
        return false;
    }
    for (std::size_t i = 0; i < left.polygon.size(); ++i) {
        for (std::size_t j = 0; j < right.polygon.size(); ++j) {
            if (segmentsIntersect(left.polygon[i], left.polygon[(i + 1) % left.polygon.size()],
                    right.polygon[j], right.polygon[(j + 1) % right.polygon.size()])) return true;
        }
    }
    return false;
}

SketchProfile buildProfiles(std::vector<ProfileLoop> loops)
{
    if (loops.empty()) throw std::runtime_error("Sketch profile contains no closed loops");
    for (std::size_t i = 0; i < loops.size(); ++i) {
        for (std::size_t j = i + 1; j < loops.size(); ++j) {
            if (intersects(loops[i], loops[j])) {
                throw std::runtime_error("Sketch profile loops intersect or touch");
            }
        }
    }

    std::vector<int> depth(loops.size(), 0);
    for (std::size_t i = 0; i < loops.size(); ++i) {
        for (std::size_t j = 0; j < loops.size(); ++j) {
            if (i != j && contains(loops[j], loops[i].representative)) ++depth[i];
        }
    }

    SketchProfile result;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        if (depth[i] % 2 != 0) continue;
        BRepBuilderAPI_MakeFace faceBuilder(loops[i].wire);
        for (std::size_t hole = 0; hole < loops.size(); ++hole) {
            if (depth[hole] == depth[i] + 1
                && contains(loops[i], loops[hole].representative)) {
                faceBuilder.Add(TopoDS::Wire(loops[hole].wire.Reversed()));
            }
        }
        if (!faceBuilder.IsDone() || faceBuilder.Face().IsNull()
            || !BRepCheck_Analyzer(faceBuilder.Face()).IsValid()) {
            throw std::runtime_error("Sketch profile face construction failed");
        }
        if (result.faces.empty()) result.wire = loops[i].wire;
        result.faces.push_back(faceBuilder.Face());
    }
    if (result.faces.empty()) throw std::runtime_error("Sketch profile nesting is invalid");
    result.face = result.faces.front();
    return result;
}

} // namespace

SketchProfile SketchProfileBuilder::build(const cad::parametric::SketchFeature& sketch)
{
    if (sketch.entities().empty()) {
        if (sketch.supportType() != cad::parametric::SketchSupportType::Face
            && sketch.shape().ShapeType() == TopAbs_WIRE) {
            return makeProfile(TopoDS::Wire(sketch.shape()));
        }
        throw std::runtime_error("Sketch profile contains no closed loops");
    }

    const auto frame = sketch.currentFrame();
    std::vector<SketchEdgeItem> lineItems;
    std::vector<cad::parametric::SketchCircle> circles;
    for (const auto& entity : sketch.entities()) {
        if (const auto* line = std::get_if<cad::parametric::SketchLine>(&entity)) {
            if (line->construction) continue;
            if (distanceSquared2d(line->start, line->end)
                <= endpointTolerance * endpointTolerance) {
                throw std::runtime_error("Sketch profile contains a zero-length edge");
            }
            BRepBuilderAPI_MakeEdge edgeBuilder(
                worldPoint(frame, line->start), worldPoint(frame, line->end));
            if (!edgeBuilder.IsDone()) throw std::runtime_error("Sketch edge construction failed");
            lineItems.push_back({edgeBuilder.Edge(), line->start, line->end,
                {line->start, line->end}});
        } else if (const auto* arc = std::get_if<cad::parametric::SketchArc>(&entity)) {
            if (arc->construction) continue;
            lineItems.push_back(arcEdgeItem(frame, *arc));
        } else {
            const auto circle = std::get<cad::parametric::SketchCircle>(entity);
            if (circle.construction) continue;
            if (!std::isfinite(circle.radius) || circle.radius <= endpointTolerance) {
                throw std::runtime_error("Sketch circle radius must be greater than zero");
            }
            circles.push_back(circle);
        }
    }

    std::vector<ProfileLoop> loops;
    std::vector<bool> consumed(lineItems.size(), false);
    for (std::size_t start = 0; start < lineItems.size(); ++start) {
        if (consumed[start]) continue;
        std::vector<std::size_t> component{start};
        consumed[start] = true;
        bool expanded = true;
        while (expanded) {
            expanded = false;
            for (std::size_t candidate = 0; candidate < lineItems.size(); ++candidate) {
                if (consumed[candidate]) continue;
                for (const auto index : component) {
                    if (closeEnough(lineItems[candidate].start, lineItems[index].start)
                        || closeEnough(lineItems[candidate].start, lineItems[index].end)
                        || closeEnough(lineItems[candidate].end, lineItems[index].start)
                        || closeEnough(lineItems[candidate].end, lineItems[index].end)) {
                        consumed[candidate] = true;
                        component.push_back(candidate);
                        expanded = true;
                        break;
                    }
                }
            }
        }

        if (component.size() < 3) throw std::runtime_error("Sketch profile is open");
        std::vector<bool> used(lineItems.size(), false);
        std::vector<TopoDS_Edge> edges;
        std::vector<gp_Pnt2d> polygon;
        const auto firstIndex = component.front();
        used[firstIndex] = true;
        edges.push_back(lineItems[firstIndex].edge);
        polygon.insert(polygon.end(), lineItems[firstIndex].samples.begin(),
            lineItems[firstIndex].samples.end() - 1);
        const gp_Pnt2d first = lineItems[firstIndex].start;
        gp_Pnt2d tail = lineItems[firstIndex].end;
        for (std::size_t count = 1; count < component.size(); ++count) {
            std::vector<std::pair<std::size_t, bool>> candidates;
            for (const auto index : component) {
                if (used[index]) continue;
                if (closeEnough(lineItems[index].start, tail)) candidates.emplace_back(index, false);
                if (closeEnough(lineItems[index].end, tail)) candidates.emplace_back(index, true);
            }
            if (candidates.size() != 1) {
                throw std::runtime_error(candidates.empty()
                    ? "Sketch profile is open or disconnected"
                    : "Sketch profile has branching or ambiguous connectivity");
            }
            const auto [index, reverse] = candidates.front();
            used[index] = true;
            edges.push_back(reverse
                ? TopoDS::Edge(lineItems[index].edge.Reversed()) : lineItems[index].edge);
            if (reverse) {
                for (auto sample = lineItems[index].samples.rbegin();
                     sample != lineItems[index].samples.rend(); ++sample) {
                    if (sample + 1 != lineItems[index].samples.rend()) polygon.push_back(*sample);
                }
            } else {
                polygon.insert(polygon.end(), lineItems[index].samples.begin(),
                    lineItems[index].samples.end() - 1);
            }
            tail = reverse ? lineItems[index].start : lineItems[index].end;
        }
        if (!closeEnough(tail, first)) throw std::runtime_error("Sketch profile is open");
        validateSelfIntersection(polygon);
        ProfileLoop loop;
        loop.wire = checkedWire(edges);
        loop.polygon = std::move(polygon);
        loop.representative = representativeForPolygon(loop.polygon);
        loops.push_back(std::move(loop));
    }

    for (const auto& circle : circles) {
        const gp_Pnt center = worldPoint(frame, circle.center);
        BRepBuilderAPI_MakeEdge edgeBuilder(
            gp_Circ(gp_Ax2(center, frame.normal), circle.radius));
        if (!edgeBuilder.IsDone()) throw std::runtime_error("Sketch circle construction failed");
        BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
        if (!wireBuilder.IsDone()) throw std::runtime_error("Sketch circle wire construction failed");
        ProfileLoop loop;
        loop.wire = wireBuilder.Wire();
        loop.center = circle.center;
        loop.radius = circle.radius;
        loop.circle = true;
        loop.representative = {circle.center.X() - circle.radius + endpointTolerance * 2.0,
            circle.center.Y()};
        loops.push_back(std::move(loop));
    }
    return buildProfiles(std::move(loops));
}

} // namespace cad::operations
