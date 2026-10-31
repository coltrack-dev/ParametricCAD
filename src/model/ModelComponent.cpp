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
#include <functional>
#include <sstream>
#include <unordered_map>
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

const char* componentLifecycleStatusName(const ComponentLifecycleStatus status) noexcept
{
    switch (status) {
    case ComponentLifecycleStatus::Current: return "CURRENT";
    case ComponentLifecycleStatus::Replaced: return "REPLACED";
    case ComponentLifecycleStatus::MissingSource: return "MISSING_SOURCE";
    case ComponentLifecycleStatus::Ambiguous: return "AMBIGUOUS";
    case ComponentLifecycleStatus::InvalidReplacementChain:
        return "INVALID_REPLACEMENT_CHAIN";
    }
    return "AMBIGUOUS";
}

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
        result.push_back({component, match->shape, ComponentLifecycleStatus::Current, {}, {}});
    }
    return result;
}

namespace {

void setResolutionDiagnostic(ComponentResolution& result,
                             const ComponentLifecycleStatus status,
                             std::string code, std::string message)
{
    result.lifecycleStatus = status;
    result.diagnosticCode = std::move(code);
    result.diagnosticMessage = std::move(message);
}

ComponentResolution canonicalize(const Body& body, ModelComponent component,
                                  const std::unordered_map<std::string,
                                      std::vector<std::string>>& replacementMap,
                                 const std::unordered_map<std::string,
                                      std::optional<std::string>>& parentMap,
                                  const std::unordered_map<std::string,
                                      std::string>& featureErrors,
                                  const double tolerance)
{
    ComponentResolution result{std::move(component), TopoDS_Shape{},
        ComponentLifecycleStatus::Current, {}, {}};
    const auto source = body.findFeature(result.component.sourceFeatureId);
    if (!source) {
        setResolutionDiagnostic(result, ComponentLifecycleStatus::MissingSource,
            "MISSING_SOURCE_FEATURE", "Component source feature is missing");
        return result;
    }
    const auto featureError = featureErrors.find(result.component.sourceFeatureId);
    if (featureError != featureErrors.end()) {
        setResolutionDiagnostic(result, ComponentLifecycleStatus::Ambiguous,
            "AMBIGUOUS_COMPONENT_RESOLUTION", featureError->second);
        return result;
    }

    std::unordered_map<std::string, int> colors;
    std::function<bool(const std::string&)> visit = [&](const std::string& id) {
        colors[id] = 1;
        const auto found = replacementMap.find(id);
        if (found != replacementMap.end()) {
            for (const auto& replacementId : found->second) {
                if (!replacementMap.contains(replacementId)) return false;
                if (colors[replacementId] == 1) return false;
                if (colors[replacementId] == 0 && !visit(replacementId)) return false;
            }
        }
        colors[id] = 2;
        return true;
    };
    if (!visit(result.component.id)) {
        setResolutionDiagnostic(result, ComponentLifecycleStatus::InvalidReplacementChain,
            "INVALID_REPLACEMENT_CHAIN", "Replacement references contain a cycle or dangling ID");
        return result;
    }

    for (const auto& replacementId : result.component.replacementComponentIds) {
        const auto child = parentMap.find(replacementId);
        if (child == parentMap.end()) {
            setResolutionDiagnostic(result, ComponentLifecycleStatus::InvalidReplacementChain,
                "DANGLING_REPLACEMENT_REFERENCE", "Replacement component ID does not exist");
            return result;
        }
        if (!child->second || *child->second != result.component.id) {
            setResolutionDiagnostic(result, ComponentLifecycleStatus::InvalidReplacementChain,
                "INVALID_PARENT_COMPONENT_REFERENCE", "Replacement child does not point to its parent");
            return result;
        }
    }
    if (result.component.parentComponentId) {
        const auto parent = replacementMap.find(*result.component.parentComponentId);
        if (parent == replacementMap.end()
            || std::find(parent->second.begin(), parent->second.end(), result.component.id)
                == parent->second.end()) {
            setResolutionDiagnostic(result, ComponentLifecycleStatus::InvalidReplacementChain,
                "INVALID_PARENT_COMPONENT_REFERENCE", "Component parent link is not reciprocal");
            return result;
        }
    }
    if (!result.component.replacementComponentIds.empty()) {
        setResolutionDiagnostic(result, ComponentLifecycleStatus::Replaced,
            "COMPONENT_REPLACED", "Component has current replacement components");
    }

    const auto resolved = ComponentResolver::resolveFeature(
        body, result.component.sourceFeatureId, tolerance);
    const auto geometry = std::find_if(resolved.begin(), resolved.end(),
        [&result](const ComponentResolution& candidate) {
            return candidate.component.id == result.component.id;
        });
    if (geometry != resolved.end()) result.shape = geometry->shape;
    if (result.shape.IsNull() && result.lifecycleStatus == ComponentLifecycleStatus::Current) {
        setResolutionDiagnostic(result, ComponentLifecycleStatus::Ambiguous,
            "AMBIGUOUS_COMPONENT_RESOLUTION", "Component geometry is not uniquely resolvable");
    }
    return result;
}

} // namespace

std::vector<ComponentResolution> ComponentResolver::resolveAll(
    const Body& body, const double tolerance)
{
    std::unordered_map<std::string, std::vector<std::string>> replacementMap;
    std::unordered_map<std::string, std::optional<std::string>> parentMap;
    for (const auto& component : body.components())
    {
        replacementMap[component.id] = component.replacementComponentIds;
        parentMap[component.id] = component.parentComponentId;
    }

    std::unordered_map<std::string, std::string> featureErrors;
    std::vector<ModelComponent> components;
    for (const auto& feature : body.features()) {
        std::string error;
        const auto merged = mergeFeatureMetadata(body, feature->id(), error, tolerance);
        if (!error.empty()) featureErrors[feature->id()] = error;
        components.insert(components.end(), merged.begin(), merged.end());
        if (!error.empty()) {
            for (const auto& persisted : body.components()) {
                if (persisted.sourceFeatureId == feature->id()) components.push_back(persisted);
            }
        }
    }
    for (const auto& component : body.components()) {
        if (!body.findFeature(component.sourceFeatureId)) components.push_back(component);
    }

    std::vector<ComponentResolution> result;
    result.reserve(components.size());
    for (auto& component : components) {
        result.push_back(canonicalize(body, std::move(component), replacementMap,
                                      parentMap, featureErrors, tolerance));
    }
    return result;
}

std::optional<ComponentResolution> ComponentResolver::resolveCanonical(
    const Body& body, const std::string& componentId, std::string& error,
    const double tolerance)
{
    error.clear();
    const auto all = resolveAll(body, tolerance);
    const auto found = std::find_if(all.begin(), all.end(),
        [&componentId](const ComponentResolution& value) {
            return value.component.id == componentId;
        });
    if (found == all.end()) {
        error = "COMPONENT_NOT_FOUND: component '" + componentId + "'";
        return {};
    }
    if (found->lifecycleStatus != ComponentLifecycleStatus::Current) {
        error = found->diagnosticCode + ": " + found->diagnosticMessage;
    }
    return *found;
}

std::optional<ComponentResolution> ComponentResolver::resolve(
    const Body& body, const std::string& componentId, std::string& error,
    double tolerance)
{
    const auto canonical = resolveCanonical(body, componentId, error, tolerance);
    if (!canonical) {
        if (error.rfind("COMPONENT_NOT_FOUND:", 0) == 0) error = "Component not found";
        return {};
    }
    if (canonical->shape.IsNull()) {
        if (error.empty()) error = "Component geometry could not be resolved";
        return {};
    }
    // Keep the historical API usable for inspection and compatibility.  New
    // modeling operations must use resolveCanonical() and require CURRENT.
    return canonical;
}
}
