#include "application/VisibilityManager.h"

#include "model/Body.h"
#include "model/FeatureVisibility.h"
#include "model/ParametricFeature.h"

#include <algorithm>
#include <utility>

namespace cad::application {

std::optional<VisibilityCategory> visibilityCategoryFor(
    const cad::parametric::ParametricFeature& feature)
{
    using cad::parametric::FeatureRole;
    if (feature.role() == FeatureRole::Sketch) return VisibilityCategory::Sketches;
    if (feature.role() == FeatureRole::Face) return VisibilityCategory::Profiles;
    const std::string type = feature.typeId();
    if (type == "Box" || type == "Cylinder" || type == "Cone"
        || type == "Sphere" || type == "Torus" || type == "Hexagon") {
        return VisibilityCategory::Primitives;
    }
    if (type == "Extrude" || type == "Pocket" || type == "PushPull"
        || type == "Revolve" || type == "Boolean" || type == "Fillet"
        || type == "Chamfer" || type == "Shell" || type == "Offset"
        || type == "Loft" || type == "Sweep") {
        return VisibilityCategory::Operations;
    }
    if (type == "LinearPattern" || type == "PathPattern") {
        return VisibilityCategory::Patterns;
    }
    return {};
}

const char* visibilityCategoryId(const VisibilityCategory category) noexcept
{
    switch (category) {
    case VisibilityCategory::Sketches: return "sketches";
    case VisibilityCategory::Profiles: return "profiles";
    case VisibilityCategory::Primitives: return "primitives";
    case VisibilityCategory::Operations: return "operations";
    case VisibilityCategory::Patterns: return "patterns";
    }
    return "unknown";
}

const char* visibilityCategoryName(const VisibilityCategory category) noexcept
{
    switch (category) {
    case VisibilityCategory::Sketches: return "Sketches";
    case VisibilityCategory::Profiles: return "Profiles / Faces";
    case VisibilityCategory::Primitives: return "Primitives";
    case VisibilityCategory::Operations: return "Operations";
    case VisibilityCategory::Patterns: return "Patterns";
    }
    return "Unknown";
}

std::vector<VisibilityCategory> visibilityCategories()
{
    return {VisibilityCategory::Sketches, VisibilityCategory::Profiles,
            VisibilityCategory::Primitives, VisibilityCategory::Operations,
            VisibilityCategory::Patterns};
}

std::vector<std::string> knownVisibilityTypeIds()
{
    return {"Sketch", "Face", "Box", "Cylinder", "Cone", "Sphere", "Torus",
            "Hexagon", "Extrude", "Pocket", "PushPull", "Revolve", "Boolean",
            "Fillet", "Chamfer", "Shell", "Offset", "Loft", "Sweep",
            "LinearPattern", "PathPattern"};
}

void VisibilityManager::setIsolatedFeatures(const std::vector<std::string>& featureIds)
{
    isolatedFeatureIds_.clear();
    isolatedFeatureIds_.insert(featureIds.begin(), featureIds.end());
}

void VisibilityManager::clearIsolation()
{
    isolatedFeatureIds_.clear();
}

bool VisibilityManager::isolationActive() const noexcept
{
    return !isolatedFeatureIds_.empty();
}

void VisibilityManager::ghostOthers(const std::vector<std::string>& selectedIds)
{
    ghostedSelectionIds_.clear();
    ghostedSelectionIds_.insert(selectedIds.begin(), selectedIds.end());
}

void VisibilityManager::clearGhosting()
{
    ghostedSelectionIds_.clear();
}

void VisibilityManager::clear()
{
    clearIsolation();
    clearGhosting();
    groups_.clear();
    clearFilters();
    nextGroupSequence_ = 1;
}

std::optional<std::string> VisibilityManager::createGroup(
    const std::string& name, std::optional<std::string> parentId)
{
    if (name.empty() || (parentId && !groups_.contains(*parentId))) return {};
    std::string id;
    do {
        id = "group-" + std::to_string(nextGroupSequence_++);
    } while (groups_.contains(id));
    groups_.emplace(id, VisibilityGroup{id, name, std::move(parentId), {},
                                        VisibilityMode::Visible});
    return id;
}

bool VisibilityManager::removeGroup(const std::string& groupId)
{
    if (!groups_.erase(groupId)) return false;
    for (auto& [id, group] : groups_) {
        if (group.parentId && *group.parentId == groupId) group.parentId.reset();
    }
    return true;
}

bool VisibilityManager::renameGroup(const std::string& groupId, const std::string& name)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end() || name.empty()) return false;
    found->second.name = name;
    return true;
}

bool VisibilityManager::setGroupParent(
    const std::string& groupId, std::optional<std::string> parentId)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end() || (parentId && !groups_.contains(*parentId))) return false;
    if (parentId && *parentId == groupId) return false;
    std::set<std::string> visited;
    for (auto current = parentId; current;) {
        if (!visited.insert(*current).second || *current == groupId) return false;
        const auto parent = groups_.find(*current);
        if (parent == groups_.end()) return false;
        current = parent->second.parentId;
    }
    found->second.parentId = std::move(parentId);
    return true;
}

bool VisibilityManager::addFeaturesToGroup(
    const std::string& groupId, const std::vector<std::string>& featureIds)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) return false;
    found->second.memberFeatureIds.insert(featureIds.begin(), featureIds.end());
    return true;
}

bool VisibilityManager::removeFeaturesFromGroup(
    const std::string& groupId, const std::vector<std::string>& featureIds)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) return false;
    for (const auto& featureId : featureIds) found->second.memberFeatureIds.erase(featureId);
    return true;
}

bool VisibilityManager::setGroupVisibility(
    const std::string& groupId, const VisibilityMode mode)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) return false;
    found->second.mode = mode;
    return true;
}

void VisibilityManager::resetGroupVisibility()
{
    for (auto& [id, group] : groups_) group.mode = VisibilityMode::Visible;
}

std::vector<VisibilityGroup> VisibilityManager::groups() const
{
    std::vector<VisibilityGroup> result;
    result.reserve(groups_.size());
    for (const auto& [id, group] : groups_) result.push_back(group);
    std::sort(result.begin(), result.end(),
        [](const auto& first, const auto& second) { return first.id < second.id; });
    return result;
}

bool VisibilityManager::replaceGroups(
    std::vector<VisibilityGroup> groups, std::string& error)
{
    error.clear();
    std::unordered_map<std::string, VisibilityGroup> replacement;
    for (auto& group : groups) {
        if (group.id.empty() || group.name.empty() || replacement.contains(group.id)) {
            error = "Invalid or duplicate visibility group";
            return false;
        }
        replacement.emplace(group.id, std::move(group));
    }
    for (const auto& [id, group] : replacement) {
        if (group.parentId && !replacement.contains(*group.parentId)) {
            error = "Visibility group references a missing parent";
            return false;
        }
        std::set<std::string> visiting;
        for (auto current = group.parentId; current;) {
            if (!visiting.insert(*current).second || *current == id) {
                error = "Visibility group hierarchy contains a cycle";
                return false;
            }
            current = replacement.at(*current).parentId;
        }
    }
    groups_ = std::move(replacement);
    nextGroupSequence_ = 1;
    return true;
}

bool VisibilityManager::setTypeFilter(
    const std::string& typeId, const VisibilityMode mode)
{
    if (typeId.empty()) return false;
    filters_.typeModes[typeId] = mode;
    return true;
}

bool VisibilityManager::clearTypeFilter(const std::string& typeId)
{
    return filters_.typeModes.erase(typeId) != 0;
}

bool VisibilityManager::setRoleFilter(
    const cad::parametric::FeatureRole role, const VisibilityMode mode)
{
    filters_.roleModes[role] = mode;
    return true;
}

bool VisibilityManager::clearRoleFilter(const cad::parametric::FeatureRole role)
{
    return filters_.roleModes.erase(role) != 0;
}

bool VisibilityManager::setCategoryFilter(
    const VisibilityCategory category, const VisibilityMode mode)
{
    filters_.categoryModes[category] = mode;
    return true;
}

bool VisibilityManager::clearCategoryFilter(const VisibilityCategory category)
{
    return filters_.categoryModes.erase(category) != 0;
}

void VisibilityManager::clearFilters()
{
    filters_ = {};
}

const VisibilityFilterState& VisibilityManager::filters() const noexcept
{
    return filters_;
}

bool VisibilityManager::replaceFilters(
    VisibilityFilterState filters, std::string& error)
{
    error.clear();
    for (const auto& [typeId, mode] : filters.typeModes) {
        if (typeId.empty()) {
            error = "Visibility type filter has an empty type ID";
            return false;
        }
    }
    filters_ = std::move(filters);
    return true;
}

VisibilityMode VisibilityManager::moreRestrictive(
    const VisibilityMode first, const VisibilityMode second)
{
    if (first == VisibilityMode::Hidden || second == VisibilityMode::Hidden)
        return VisibilityMode::Hidden;
    if (first == VisibilityMode::Ghosted || second == VisibilityMode::Ghosted)
        return VisibilityMode::Ghosted;
    return VisibilityMode::Visible;
}

VisibilityMode VisibilityManager::groupEffectiveMode(
    const std::string& groupId,
    std::unordered_map<std::string, VisibilityMode>& effective,
    std::set<std::string>& visiting) const
{
    if (const auto cached = effective.find(groupId); cached != effective.end())
        return cached->second;
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) return VisibilityMode::Visible;
    if (!visiting.insert(groupId).second) return VisibilityMode::Hidden;
    auto mode = found->second.mode;
    if (found->second.parentId) {
        mode = moreRestrictive(mode,
            groupEffectiveMode(*found->second.parentId, effective, visiting));
    }
    visiting.erase(groupId);
    effective.emplace(groupId, mode);
    return mode;
}

VisibilityMode VisibilityManager::effectiveMode(
    const cad::parametric::ParametricFeature& feature,
    const cad::parametric::Body& body) const
{
    const auto hiddenIds = cad::parametric::hiddenFeatureIds(body, isolatedFeatureIds_);
    const std::set<std::string> hiddenFeatureSet(hiddenIds.begin(), hiddenIds.end());
    std::unordered_map<std::string, VisibilityMode> groupModes;
    std::unordered_map<std::string, VisibilityMode> effectiveGroups;
    for (const auto& [id, group] : groups_) {
        std::set<std::string> visiting;
        const auto mode = groupEffectiveMode(id, effectiveGroups, visiting);
        for (const auto& featureId : group.memberFeatureIds) {
            const auto found = groupModes.find(featureId);
            groupModes[featureId] = found == groupModes.end()
                ? mode : moreRestrictive(found->second, mode);
        }
    }
    return modeForFeature(feature, hiddenFeatureSet, groupModes, filters_);
}

VisibilityMode VisibilityManager::modeForFeature(
    const cad::parametric::ParametricFeature& feature,
    const std::set<std::string>& hiddenFeatureIds,
    const std::unordered_map<std::string, VisibilityMode>& groupModes,
    const VisibilityFilterState& filters) const
{
    if (hiddenFeatureIds.contains(feature.id())) {
        return VisibilityMode::Hidden;
    }
    auto mode = VisibilityMode::Visible;
    const auto group = groupModes.find(feature.id());
    if (group != groupModes.end()) mode = moreRestrictive(mode, group->second);
    const auto filterMode = filterModeForFeature(feature, filters);
    mode = moreRestrictive(mode, filterMode);
    if (mode != VisibilityMode::Visible) return mode;
    if (!ghostedSelectionIds_.empty()
        && !ghostedSelectionIds_.contains(feature.id())) {
        return VisibilityMode::Ghosted;
    }
    return VisibilityMode::Visible;
}

VisibilityMode VisibilityManager::filterModeForFeature(
    const cad::parametric::ParametricFeature& feature,
    const VisibilityFilterState& filters) const
{
    auto mode = VisibilityMode::Visible;
    const auto type = filters.typeModes.find(feature.typeId());
    if (type != filters.typeModes.end()) mode = moreRestrictive(mode, type->second);
    const auto role = filters.roleModes.find(feature.role());
    if (role != filters.roleModes.end()) mode = moreRestrictive(mode, role->second);
    const auto category = visibilityCategoryFor(feature);
    if (category) {
        const auto found = filters.categoryModes.find(*category);
        if (found != filters.categoryModes.end())
            mode = moreRestrictive(mode, found->second);
    }
    return mode;
}

VisibilityProjection VisibilityManager::projection(
    const cad::parametric::Body& body) const
{
    VisibilityProjection result;
    result.reserve(body.features().size());
    const auto hiddenIds = cad::parametric::hiddenFeatureIds(body, isolatedFeatureIds_);
    const std::set<std::string> hiddenFeatureSet(hiddenIds.begin(), hiddenIds.end());
    std::unordered_map<std::string, VisibilityMode> effectiveGroups;
    std::unordered_map<std::string, VisibilityMode> groupModes;
    for (const auto& [id, group] : groups_) {
        std::set<std::string> visiting;
        const auto mode = groupEffectiveMode(id, effectiveGroups, visiting);
        for (const auto& featureId : group.memberFeatureIds) {
            const auto found = groupModes.find(featureId);
            groupModes[featureId] = found == groupModes.end()
                ? mode : moreRestrictive(found->second, mode);
        }
    }
    for (const auto& feature : body.features()) {
        result.push_back({feature->id(),
            modeForFeature(*feature, hiddenFeatureSet, groupModes, filters_)});
    }
    return result;
}

} // namespace cad::application
