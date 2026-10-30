#include "model/ModelComponent.h"

#include "model/Body.h"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopAbs_ShapeEnum.hxx>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace {
struct Metrics {
    double minX{}, minY{}, minZ{}, maxX{}, maxY{}, maxZ{}, volume{};
};

Metrics metrics(const TopoDS_Shape& shape)
{
    Metrics result;
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    if (!bounds.IsVoid()) bounds.Get(result.minX, result.minY, result.minZ,
                                     result.maxX, result.maxY, result.maxZ);
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    result.volume = props.Mass();
    return result;
}

std::string idFor(const std::string& featureId, const Metrics& value)
{
    std::ostringstream stream;
    stream << featureId << ":solid:" << std::fixed << std::setprecision(6)
           << value.minX << ':' << value.minY << ':' << value.minZ << ':'
           << value.maxX << ':' << value.maxY << ':' << value.maxZ << ':'
           << value.volume;
    return stream.str();
}

bool close(double left, double right, double tolerance)
{
    return std::abs(left - right) <= tolerance * std::max({1.0, std::abs(left), std::abs(right)});
}

bool matches(const cad::parametric::ModelComponent& component,
             const Metrics& value, double tolerance)
{
    return close(component.minX, value.minX, tolerance)
        && close(component.minY, value.minY, tolerance)
        && close(component.minZ, value.minZ, tolerance)
        && close(component.maxX, value.maxX, tolerance)
        && close(component.maxY, value.maxY, tolerance)
        && close(component.maxZ, value.maxZ, tolerance)
        && close(component.volume, value.volume, tolerance);
}
}

namespace cad::parametric {

std::vector<ModelComponent> ComponentResolver::enumerate(const Body& body,
                                                         const std::string& featureId)
{
    std::vector<ModelComponent> persisted;
    for (const auto& component : body.components())
        if (component.sourceFeatureId == featureId) persisted.push_back(component);
    if (!persisted.empty()) return persisted;
    const auto feature = body.findFeature(featureId);
    if (!feature || feature->shape().IsNull()) return {};
    std::vector<TopoDS_Shape> solids;
    if (feature->shape().ShapeType() == TopAbs_SOLID) {
        solids.push_back(feature->shape());
    } else {
        for (TopExp_Explorer explorer(feature->shape(), TopAbs_SOLID);
             explorer.More(); explorer.Next()) solids.push_back(explorer.Current());
    }
    std::vector<ModelComponent> result;
    for (const auto& solid : solids) {
        const auto value = metrics(solid);
        ModelComponent component;
        component.id = idFor(featureId, value);
        component.name = feature->name() + " component";
        component.sourceFeatureId = featureId;
        component.minX = value.minX; component.minY = value.minY; component.minZ = value.minZ;
        component.maxX = value.maxX; component.maxY = value.maxY; component.maxZ = value.maxZ;
        component.volume = value.volume;
        result.push_back(std::move(component));
    }
    return result;
}

std::vector<ComponentResolution> ComponentResolver::resolveFeature(
    const Body& body, const std::string& featureId, const double tolerance)
{
    const auto feature = body.findFeature(featureId);
    if (!feature || feature->shape().IsNull()) return {};

    std::vector<TopoDS_Shape> solids;
    if (feature->shape().ShapeType() == TopAbs_SOLID) {
        solids.push_back(feature->shape());
    } else {
        for (TopExp_Explorer explorer(feature->shape(), TopAbs_SOLID);
             explorer.More(); explorer.Next()) {
            solids.push_back(explorer.Current());
        }
    }

    struct Candidate {
        TopoDS_Shape shape;
        Metrics metrics;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(solids.size());
    for (const auto& solid : solids) candidates.push_back({solid, metrics(solid)});

    std::vector<ModelComponent> components;
    for (const auto& component : body.components()) {
        if (component.sourceFeatureId == featureId) components.push_back(component);
    }
    if (components.empty()) {
        components.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            ModelComponent component;
            component.id = idFor(featureId, candidate.metrics);
            component.name = feature->name() + " component";
            component.sourceFeatureId = featureId;
            component.minX = candidate.metrics.minX;
            component.minY = candidate.metrics.minY;
            component.minZ = candidate.metrics.minZ;
            component.maxX = candidate.metrics.maxX;
            component.maxY = candidate.metrics.maxY;
            component.maxZ = candidate.metrics.maxZ;
            component.volume = candidate.metrics.volume;
            components.push_back(std::move(component));
        }
    }

    std::vector<ComponentResolution> result;
    for (const auto& component : components) {
        const auto match = std::find_if(candidates.begin(), candidates.end(),
            [&component, tolerance](const Candidate& candidate) {
                return matches(component, candidate.metrics, tolerance);
            });
        if (match == candidates.end()) continue;
        const auto duplicate = std::find_if(std::next(match), candidates.end(),
            [&component, tolerance](const Candidate& candidate) {
                return matches(component, candidate.metrics, tolerance);
            });
        if (duplicate != candidates.end()) continue;
        result.push_back({component, match->shape});
    }
    return result;
}

std::optional<ComponentResolution> ComponentResolver::resolve(
    const Body& body, const std::string& componentId, std::string& error,
    double tolerance)
{
    error.clear();
    for (const auto& component : body.components()) {
        if (component.id != componentId) continue;
        const auto resolved = resolveFeature(body, component.sourceFeatureId, tolerance);
        for (const auto& candidate : resolved) {
            if (candidate.component.id == componentId) return candidate;
        }
        error = "Component geometry could not be resolved";
        return {};
    }
    // Legacy projects have no metadata.  Accept the deterministic signature
    // produced by enumerate(), but never silently choose an unrelated solid.
    for (const auto& feature : body.features()) {
        for (const auto& component : resolveFeature(body, feature->id(), tolerance)) {
            if (component.component.id == componentId) return component;
        }
    }
    error = "Component not found";
    return {};
}
}
