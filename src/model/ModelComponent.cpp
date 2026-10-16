#include "model/ModelComponent.h"

#include "model/Body.h"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopAbs_ShapeEnum.hxx>

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

std::optional<ComponentResolution> ComponentResolver::resolve(
    const Body& body, const std::string& componentId, std::string& error,
    double tolerance)
{
    error.clear();
    for (const auto& component : body.components()) {
        if (component.id != componentId) continue;
        const auto feature = body.findFeature(component.sourceFeatureId);
        if (!feature || feature->shape().IsNull()) {
            error = "Component source feature is missing or has no shape";
            return {};
        }
        std::vector<TopoDS_Shape> candidates;
        if (feature->shape().ShapeType() == TopAbs_SOLID) candidates.push_back(feature->shape());
        else for (TopExp_Explorer explorer(feature->shape(), TopAbs_SOLID);
                  explorer.More(); explorer.Next()) candidates.push_back(explorer.Current());
        std::vector<TopoDS_Shape> matchesFound;
        for (const auto& candidate : candidates)
            if (matches(component, metrics(candidate), tolerance)) matchesFound.push_back(candidate);
        if (matchesFound.size() != 1) {
            error = matchesFound.empty()
                ? "Component geometry could not be resolved"
                : "Component reference is ambiguous";
            return {};
        }
        return ComponentResolution{component, matchesFound.front()};
    }
    // Legacy projects have no metadata.  Accept the deterministic signature
    // produced by enumerate(), but never silently choose an unrelated solid.
    for (const auto& feature : body.features()) {
        for (const auto& component : enumerate(body, feature->id())) {
            if (component.id != componentId) continue;
            // Legacy projects do not store the component row; resolve the
            // deterministic signature directly against the current feature.
            const auto featureShape = feature->shape();
            if (featureShape.ShapeType() == TopAbs_SOLID
                && matches(component, metrics(featureShape), tolerance))
                return ComponentResolution{component, featureShape};
            for (TopExp_Explorer explorer(featureShape, TopAbs_SOLID);
                 explorer.More(); explorer.Next())
                if (matches(component, metrics(explorer.Current()), tolerance))
                    return ComponentResolution{component, explorer.Current()};
        }
    }
    error = "Component not found";
    return {};
}
}
