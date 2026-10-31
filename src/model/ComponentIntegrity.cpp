#include "model/ComponentIntegrity.h"

#include "model/ModelComponent.h"

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace cad::parametric {

namespace {

void issue(std::vector<ComponentIntegrityIssue>& issues, const char* code,
           const std::string& featureId, const std::string& componentId,
           const char* description, const ComponentIntegritySeverity severity)
{
    issues.push_back({code, featureId, componentId, description, severity});
}

std::size_t solidCount(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return 0;
    if (shape.ShapeType() == TopAbs_SOLID) return 1;
    std::size_t count = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_SOLID);
         explorer.More(); explorer.Next()) ++count;
    return count;
}

} // namespace

std::vector<ComponentIntegrityIssue> inspectComponentIntegrity(
    const Body& body, const double tolerance)
{
    std::vector<ComponentIntegrityIssue> issues;
    std::unordered_map<std::string, const ModelComponent*> byId;
    std::unordered_map<std::string, std::vector<const ModelComponent*>> byFeature;

    for (const auto& component : body.components()) {
        const auto [iterator, inserted] = byId.emplace(component.id, &component);
        if (!inserted) {
            issue(issues, "DUPLICATE_COMPONENT_ID", component.sourceFeatureId,
                component.id, "Multiple persisted components use the same ID",
                ComponentIntegritySeverity::Error);
        }
        byFeature[component.sourceFeatureId].push_back(&component);
        if (!body.findFeature(component.sourceFeatureId)) {
            issue(issues, "MISSING_SOURCE_FEATURE", component.sourceFeatureId,
                component.id, "Component source feature is missing",
                ComponentIntegritySeverity::Error);
        }
    }

    for (const auto& component : body.components()) {
        for (const auto& replacementId : component.replacementComponentIds) {
            if (!byId.contains(replacementId)) {
                issue(issues, "DANGLING_REPLACEMENT_REFERENCE", component.sourceFeatureId,
                    component.id, "Replacement component ID does not exist",
                    ComponentIntegritySeverity::Error);
            }
        }
        if (!component.replacementComponentIds.empty() && !component.technicalHidden) {
            issue(issues, "REPLACED_COMPONENT_ACTIVE", component.sourceFeatureId,
                component.id, "Component has replacements but is not technically hidden",
                ComponentIntegritySeverity::Error);
        }
    }

    std::unordered_map<std::string, int> colors;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        colors[id] = 1;
        const auto found = byId.find(id);
        if (found != byId.end()) {
            for (const auto& replacementId : found->second->replacementComponentIds) {
                const auto color = colors[replacementId];
                if (color == 1) {
                    issue(issues, "REPLACEMENT_CYCLE", found->second->sourceFeatureId,
                        found->second->id, "Replacement references contain a cycle",
                        ComponentIntegritySeverity::Error);
                } else if (color == 0) {
                    visit(replacementId);
                }
            }
        }
        colors[id] = 2;
    };
    for (const auto& [id, component] : byId) {
        (void)component;
        if (colors[id] == 0) visit(id);
    }

    for (const auto& feature : body.features()) {
        const auto persisted = byFeature.find(feature->id());
        const auto persistedCount = persisted == byFeature.end() ? 0u : persisted->second.size();
        const auto currentSolids = solidCount(feature->shape());
        std::size_t currentPersisted = 0;
        bool hasHistorical = false;
        if (persisted != byFeature.end()) {
            for (const auto* component : persisted->second) {
                hasHistorical = hasHistorical || component->technicalHidden
                    || !component->replacementComponentIds.empty();
                if (!component->technicalHidden && component->replacementComponentIds.empty())
                    ++currentPersisted;
            }
        }
        if (persistedCount > 0 && !hasHistorical && currentSolids > currentPersisted
            && currentPersisted > 0) {
            issue(issues, "PARTIAL_COMPONENT_METADATA", feature->id(), {},
                "Persisted metadata covers only part of the current feature solids",
                ComponentIntegritySeverity::Warning);
        }

        std::string enumerationError;
        ComponentResolver::enumerate(body, feature->id(), enumerationError);
        if (!enumerationError.empty()) {
            issue(issues, "AMBIGUOUS_GENERATED_COMPONENT_ID", feature->id(), {},
                enumerationError.c_str(), ComponentIntegritySeverity::Error);
        }
    }

    for (const auto& component : body.components()) {
        if (!body.findFeature(component.sourceFeatureId)) continue;
        std::string error;
        const auto resolved = ComponentResolver::resolve(body, component.id, error, tolerance);
        if (!resolved) {
            issue(issues, "UNRESOLVED_COMPONENT_GEOMETRY", component.sourceFeatureId,
                component.id, error.c_str(), ComponentIntegritySeverity::Error);
        } else if (!component.replacementComponentIds.empty()) {
            issue(issues, "REPLACED_COMPONENT_RESOLVABLE", component.sourceFeatureId,
                component.id, "Resolver still returns a replaced component",
                ComponentIntegritySeverity::Warning);
        }
    }

    return issues;
}

} // namespace cad::parametric
