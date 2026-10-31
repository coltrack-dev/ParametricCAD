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
#include <unordered_set>

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
    std::string error;
    return enumerate(body, featureId, error);
}

std::vector<ModelComponent> ComponentResolver::enumerate(
    const Body& body, const std::string& featureId, std::string& error)
{
    error.clear();
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
    std::unordered_set<std::string> generatedIds;
    for (const auto& solid : solids) {
        const auto value = metrics(solid);
        ModelComponent component;
        component.id = idFor(featureId, value);
        component.name = feature->name() + " component";
        component.sourceFeatureId = featureId;
        component.minX = value.minX; component.minY = value.minY; component.minZ = value.minZ;
        component.maxX = value.maxX; component.maxY = value.maxY; component.maxZ = value.maxZ;
        component.volume = value.volume;
        if (!generatedIds.insert(component.id).second) {
            error = "AMBIGUOUS_GENERATED_COMPONENT_ID: feature '" + featureId
                + "' has multiple solids with the same identity signature";
            return {};
        }
        result.push_back(std::move(component));
    }
    return result;
}

std::vector<ModelComponent> ComponentResolver::mergeFeatureMetadata(
    const Body& body, const std::string& featureId, std::string& error,
    const double tolerance)
{
    error.clear();
    const auto feature = body.findFeature(featureId);
    if (!feature || feature->shape().IsNull()) return {};

    std::vector<ModelComponent> persisted;
    for (const auto& component : body.components()) {
        if (component.sourceFeatureId == featureId) persisted.push_back(component);
    }
    std::vector<TopoDS_Shape> solids;
    if (feature->shape().ShapeType() == TopAbs_SOLID) {
        solids.push_back(feature->shape());
    } else {
        for (TopExp_Explorer explorer(feature->shape(), TopAbs_SOLID);
             explorer.More(); explorer.Next()) solids.push_back(explorer.Current());
    }

    std::vector<std::pair<ModelComponent, Metrics>> current;
    current.reserve(solids.size());
    for (const auto& solid : solids) {
        const auto value = metrics(solid);
        ModelComponent component;
        component.id = idFor(featureId, value);
        component.name = feature->name() + " component";
        component.sourceFeatureId = featureId;
        component.minX = value.minX; component.minY = value.minY; component.minZ = value.minZ;
        component.maxX = value.maxX; component.maxY = value.maxY; component.maxZ = value.maxZ;
        component.volume = value.volume;
        current.emplace_back(std::move(component), value);
    }

    std::unordered_set<std::string> generatedIds;
    for (const auto& [component, value] : current) {
        (void)value;
        if (!generatedIds.insert(component.id).second) {
            error = "Ambiguous generated component ID for feature '" + featureId + "'";
            return {};
        }
    }

    std::vector<ModelComponent> result = persisted;
    if (persisted.empty()) {
        for (const auto& [component, value] : current) {
            (void)value;
            result.push_back(component);
        }
        return result;
    }
    const bool hasActivePersisted = std::any_of(persisted.begin(), persisted.end(),
        [](const ModelComponent& component) {
            return !component.technicalHidden && component.replacementComponentIds.empty();
        });
    if (!hasActivePersisted) return result;

    std::vector<bool> matched(persisted.size(), false);
    for (const auto& [generatedComponent, generatedMetrics] : current) {
        std::vector<std::size_t> matches;
        for (std::size_t index = 0; index < persisted.size(); ++index) {
            const auto& component = persisted[index];
            if (component.technicalHidden || !component.replacementComponentIds.empty()) continue;
            if (::matches(component, generatedMetrics, tolerance)) {
                matches.push_back(index);
            }
        }
        if (matches.size() > 1) {
            error = "AMBIGUOUS_PERSISTED_COMPONENT_MATCH: feature '" + featureId + "'";
            return {};
        }
        if (matches.size() == 1) {
            if (matched[matches.front()]) {
                error = "AMBIGUOUS_PERSISTED_COMPONENT_REUSE: feature '"
                    + featureId + "'";
                return {};
            }
            matched[matches.front()] = true;
            continue;
        }
        result.push_back(generatedComponent);
    }

    for (std::size_t index = 0; index < persisted.size(); ++index) {
        const auto& component = persisted[index];
        if (!matched[index] && !component.technicalHidden
            && component.replacementComponentIds.empty()) {
            error = "UNRESOLVED_PERSISTED_COMPONENT_GEOMETRY: feature '"
                + featureId + "'";
            return {};
        }
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

    std::string metadataError;
    const auto components = mergeFeatureMetadata(body, featureId, metadataError, tolerance);
    if (!metadataError.empty()) return {};

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
        std::string enumerationError;
        ComponentResolver::enumerate(body, feature->id(), enumerationError);
        if (!enumerationError.empty()) {
            error = enumerationError;
            return {};
        }
        for (const auto& component : resolveFeature(body, feature->id(), tolerance)) {
            if (component.component.id == componentId) return component;
        }
    }
    error = "Component not found";
    return {};
}
}
