#include "operations/SketchPathBuilder.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>

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

struct PathEdge
{
    cad::parametric::SketchEntityId id;
    TopoDS_Edge edge;
    gp_Pnt start;
    gp_Pnt end;
    int startNode{-1};
    int endNode{-1};
};

int findNode(std::vector<gp_Pnt>& nodes, const gp_Pnt& point)
{
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].Distance(point) <= endpointTolerance)
            return static_cast<int>(index);
    }
    nodes.push_back(point);
    return static_cast<int>(nodes.size() - 1);
}

PathEdge makePathEdge(
    const cad::parametric::SketchFrame& frame,
    const cad::parametric::SketchEntity& entity)
{
    return std::visit([&](const auto& value) -> PathEdge {
        using Entity = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Entity, cad::parametric::SketchLine>) {
            const auto start = worldPoint(frame, value.start);
            const auto end = worldPoint(frame, value.end);
            if (start.Distance(end) <= endpointTolerance)
                throw std::runtime_error("Sweep failed: path is zero length");
            BRepBuilderAPI_MakeEdge builder(start, end);
            if (!builder.IsDone())
                throw std::runtime_error("Sweep failed: path Line construction failed");
            return {value.id, builder.Edge(), start, end, -1, -1};
        } else if constexpr (std::is_same_v<Entity, cad::parametric::SketchArc>) {
            if (!std::isfinite(value.radius) || value.radius <= endpointTolerance
                || !std::isfinite(value.startAngle) || !std::isfinite(value.endAngle)) {
                throw std::runtime_error("Sweep failed: path Arc has invalid geometry");
            }
            const auto sweep = value.signedSweep();
            if (!std::isfinite(sweep) || std::abs(sweep) <= endpointTolerance
                || std::abs(sweep) >= 2.0 * std::acos(-1.0) - endpointTolerance) {
                throw std::runtime_error("Sweep failed: path Arc has invalid sweep");
            }
            const auto center = worldPoint(frame, value.center);
            const gp_Circ circle(gp_Ax2(center, frame.normal, frame.xDirection), value.radius);
            BRepBuilderAPI_MakeEdge builder;
            if (sweep > 0.0) {
                builder = BRepBuilderAPI_MakeEdge(
                    circle, value.startAngle, value.startAngle + sweep);
            } else {
                builder = BRepBuilderAPI_MakeEdge(
                    circle, value.startAngle + sweep, value.startAngle);
            }
            if (!builder.IsDone())
                throw std::runtime_error("Sweep failed: path Arc construction failed");
            TopoDS_Edge edge = builder.Edge();
            if (sweep < 0.0) edge = TopoDS::Edge(edge.Reversed());
            const auto start = worldPoint(frame, value.startPoint());
            const auto end = worldPoint(frame, value.endPoint());
            if (start.Distance(end) <= endpointTolerance)
                throw std::runtime_error("Sweep failed: path is zero length");
            return {value.id, edge, start, end, -1, -1};
        } else {
            throw std::runtime_error("Sweep failed: path supports Line and Arc only");
        }
    }, entity);
}

} // namespace

SketchPath SketchPathBuilder::build(
    const cad::parametric::SketchFeature& sketch,
    const std::vector<cad::parametric::SketchEntityId>& requestedIds)
{
    std::vector<const cad::parametric::SketchEntity*> selected;
    std::vector<cad::parametric::SketchEntityId> ids;
    for (const auto& entity : sketch.entities()) {
        const auto id = std::visit([](const auto& value) { return value.id; }, entity);
        const bool requested = requestedIds.empty()
            || std::find(requestedIds.begin(), requestedIds.end(), id) != requestedIds.end();
        if (!requested) continue;
        const bool construction = std::visit([](const auto& value) { return value.construction; }, entity);
        if (construction) {
            if (!requestedIds.empty())
                throw std::runtime_error("Sweep failed: construction geometry cannot be a path");
            continue;
        }
        const bool supported = std::holds_alternative<cad::parametric::SketchLine>(entity)
            || std::holds_alternative<cad::parametric::SketchArc>(entity);
        if (!supported)
            throw std::runtime_error("Sweep failed: path supports Line and Arc only");
        selected.push_back(&entity);
        ids.push_back(id);
    }
    if (!requestedIds.empty() && selected.size() != requestedIds.size())
        throw std::runtime_error("Sweep failed: path entity no longer exists");
    if (selected.empty())
        throw std::runtime_error("Sweep failed: Sketch path contains no Line or Arc");

    std::vector<PathEdge> edges;
    edges.reserve(selected.size());
    std::vector<gp_Pnt> nodes;
    for (const auto* entity : selected) {
        auto edge = makePathEdge(sketch.currentFrame(), *entity);
        edge.startNode = findNode(nodes, edge.start);
        edge.endNode = findNode(nodes, edge.end);
        edges.push_back(std::move(edge));
    }

    std::vector<std::vector<std::size_t>> incident(nodes.size());
    for (std::size_t index = 0; index < edges.size(); ++index) {
        if (edges[index].startNode == edges[index].endNode)
            throw std::runtime_error("Sweep failed: path contains a zero-length loop");
        incident[edges[index].startNode].push_back(index);
        incident[edges[index].endNode].push_back(index);
    }
    std::vector<int> endpoints;
    for (std::size_t node = 0; node < incident.size(); ++node) {
        if (incident[node].size() == 1) endpoints.push_back(static_cast<int>(node));
        else if (incident[node].size() != 2)
            throw std::runtime_error("Sweep failed: Sketch path contains a branch");
    }
    if (endpoints.size() != 2)
        throw std::runtime_error(endpoints.empty()
            ? "Sweep failed: Sketch path must be open"
            : "Sweep failed: Sketch path is disconnected");

    std::size_t startEdge = incident[endpoints[0]].front();
    const auto otherEndpoint = endpoints[1];
    if (edges[startEdge].endNode == endpoints[0]) {
        std::swap(edges[startEdge].startNode, edges[startEdge].endNode);
        std::swap(edges[startEdge].start, edges[startEdge].end);
        edges[startEdge].edge.Reverse();
    }
    std::vector<bool> used(edges.size(), false);
    std::vector<TopoDS_Edge> ordered;
    ordered.reserve(edges.size());
    int current = endpoints[0];
    for (std::size_t count = 0; count < edges.size(); ++count) {
        const auto candidates = incident[current];
        auto found = std::find_if(candidates.begin(), candidates.end(),
            [&](const auto index) { return !used[index]; });
        if (found == candidates.end())
            throw std::runtime_error("Sweep failed: Sketch path is disconnected");
        const auto index = *found;
        auto& edge = edges[index];
        if (edge.startNode != current) {
            std::swap(edge.startNode, edge.endNode);
            std::swap(edge.start, edge.end);
            edge.edge.Reverse();
        }
        used[index] = true;
        ordered.push_back(edge.edge);
        current = edge.endNode;
    }
    if (current != otherEndpoint)
        throw std::runtime_error("Sweep failed: Sketch path is disconnected");

    BRepBuilderAPI_MakeWire builder;
    for (const auto& edge : ordered) builder.Add(edge);
    if (!builder.IsDone() || builder.Wire().IsNull()
        || !BRepCheck_Analyzer(builder.Wire()).IsValid())
        throw std::runtime_error("Sweep failed: Sketch path wire is invalid");
    return {builder.Wire(), std::move(ids)};
}

} // namespace cad::operations
