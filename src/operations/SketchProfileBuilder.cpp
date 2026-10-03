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
    return {wire, faceBuilder.Face()};
}

SketchProfile lineProfile(
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

} // namespace

SketchProfile SketchProfileBuilder::build(const cad::parametric::SketchFeature& sketch)
{
    if (sketch.entities().empty()) {
        if (sketch.supportType() != cad::parametric::SketchSupportType::Face
            && sketch.shape().ShapeType() == TopAbs_WIRE) {
            return makeProfile(TopoDS::Wire(sketch.shape()));
        }
        throw std::runtime_error("Sketch profile contains no closed entities");
    }

    const auto frame = sketch.currentFrame();
    std::vector<cad::parametric::SketchLine> lines;
    std::vector<cad::parametric::SketchCircle> circles;
    for (const auto& entity : sketch.entities()) {
        if (const auto* line = std::get_if<cad::parametric::SketchLine>(&entity)) lines.push_back(*line);
        else circles.push_back(std::get<cad::parametric::SketchCircle>(entity));
    }

    if (!circles.empty()) {
        if (!lines.empty() || circles.size() != 1 || circles.front().radius <= endpointTolerance) {
            throw std::runtime_error("Sketch contains multiple disconnected profiles");
        }
        const auto& circle = circles.front();
        const gp_Pnt center = worldPoint(frame, circle.center);
        BRepBuilderAPI_MakeEdge edgeBuilder(
            gp_Circ(gp_Ax2(center, frame.normal), circle.radius));
        if (!edgeBuilder.IsDone()) throw std::runtime_error("Sketch circle construction failed");
        BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
        if (!wireBuilder.IsDone()) throw std::runtime_error("Sketch circle wire construction failed");
        return makeProfile(wireBuilder.Wire());
    }

    return lineProfile(frame, lines);
}

} // namespace cad::operations
