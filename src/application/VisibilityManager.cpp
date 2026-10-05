#include "application/VisibilityManager.h"

#include "model/Body.h"
#include "model/FeatureVisibility.h"
#include "model/ParametricFeature.h"

#include <BRepBndLib.hxx>
#include <algorithm>
#include <cctype>
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

namespace {
std::string normalizedName(const std::string& name)
{
    std::string result;
    result.reserve(name.size());
    for (const unsigned char character : name)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}
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

void VisibilityManager::setSpatialRule(const SpatialVisibilityRule& rule)
{
    spatialRule_ = rule;
}

void VisibilityManager::clearSpatialRule()
{
    spatialRule_ = {};
}

const SpatialVisibilityRule& VisibilityManager::spatialRule() const noexcept
{
    return spatialRule_;
}

void VisibilityManager::updateBoundingBoxes(const cad::parametric::Body& body)
{
    std::unordered_map<std::string, Bnd_Box> replacement;
    replacement.reserve(body.features().size());
    for (const auto& feature : body.features()) {
        if (feature->shape().IsNull()) continue;
        Bnd_Box bounds;
        BRepBndLib::Add(feature->shape(), bounds);
        replacement.emplace(feature->id(), bounds);
    }
    boundingBoxes_ = std::move(replacement);
}

std::optional<std::pair<gp_Pnt, gp_Pnt>> VisibilityManager::spatialBounds(
    const cad::parametric::Body& body,
    const std::vector<std::string>& featureIds) const
{
    std::unordered_set<std::string> requested(featureIds.begin(), featureIds.end());
    Bnd_Box bounds;
    for (const auto& feature : body.features()) {
        if (!requested.empty() && !requested.contains(feature->id())) continue;
        const auto found = boundingBoxes_.find(feature->id());
        if (found != boundingBoxes_.end()) bounds.Add(found->second);
    }
    if (bounds.IsVoid()) return {};
    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    return std::pair{gp_Pnt(xmin, ymin, zmin), gp_Pnt(xmax, ymax, zmax)};
}

void VisibilityManager::clear()
{
    clearIsolation();
    clearGhosting();
    groups_.clear();
    clearFilters();
    presets_.clear();
    savedViews_.clear();
    nextGroupSequence_ = 1;
    nextPresetSequence_ = 1;
    nextSavedViewSequence_ = 1;
    effectiveModes_.clear();
    boundingBoxes_.clear();
    spatialRule_ = {};
    lastVisibilityEvaluationCount_ = 0;
    lastVisibilityChangeCount_ = 0;
    lastSpatialEvaluationCount_ = 0;
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

VisibilityConfiguration VisibilityManager::captureConfiguration(
    const cad::parametric::Body& body) const
{
    VisibilityConfiguration configuration;
    for (const auto& [id, group] : groups_)
        configuration.groupModes.emplace(id, group.mode);
    configuration.filters = filters_;
    for (const auto& feature : body.features())
        configuration.featureVisibility.emplace(feature->id(), feature->userVisible());
    return configuration;
}

bool VisibilityManager::applyConfiguration(
    const VisibilityConfiguration& configuration,
    cad::parametric::Body& body,
    std::string& error)
{
    error.clear();
    if (!replaceFilters(configuration.filters, error)) return false;
    for (auto& [id, group] : groups_) {
        const auto found = configuration.groupModes.find(id);
        group.mode = found == configuration.groupModes.end()
            ? VisibilityMode::Visible : found->second;
    }
    for (const auto& feature : body.features()) {
        const auto found = configuration.featureVisibility.find(feature->id());
        feature->setUserVisible(found == configuration.featureVisibility.end()
            ? true : found->second);
    }
    return true;
}

bool VisibilityManager::saveCurrentAsPreset(
    const std::string& name, const cad::parametric::Body& body, std::string& error)
{
    error.clear();
    if (name.empty()) {
        error = "Visibility preset name cannot be empty";
        return false;
    }
    const auto normalized = normalizedName(name);
    for (const auto& [id, preset] : presets_) {
        if (normalizedName(preset.name) == normalized) {
            error = "A visibility preset with that name already exists";
            return false;
        }
    }
    std::string id;
    do {
        id = "preset-" + std::to_string(nextPresetSequence_++);
    } while (presets_.contains(id));
    presets_.emplace(id, VisibilityPreset{id, name, captureConfiguration(body)});
    return true;
}

std::optional<std::string> VisibilityManager::createPreset(
    const std::string& name, const cad::parametric::Body& body)
{
    std::string error;
    if (!saveCurrentAsPreset(name, body, error)) return {};
    for (const auto& [id, preset] : presets_) {
        if (preset.name == name) return id;
    }
    return {};
}

bool VisibilityManager::updatePreset(
    const std::string& presetId, const cad::parametric::Body& body)
{
    const auto found = presets_.find(presetId);
    if (found == presets_.end()) return false;
    found->second.configuration = captureConfiguration(body);
    return true;
}

bool VisibilityManager::renamePreset(
    const std::string& presetId, const std::string& name, std::string& error)
{
    error.clear();
    if (name.empty()) {
        error = "Visibility preset name cannot be empty";
        return false;
    }
    const auto found = presets_.find(presetId);
    if (found == presets_.end()) return false;
    const auto normalized = normalizedName(name);
    for (const auto& [id, preset] : presets_) {
        if (id != presetId && normalizedName(preset.name) == normalized) {
            error = "A visibility preset with that name already exists";
            return false;
        }
    }
    found->second.name = name;
    return true;
}

bool VisibilityManager::deletePreset(const std::string& presetId)
{
    return presets_.erase(presetId) != 0;
}

bool VisibilityManager::applyPreset(
    const std::string& presetId, cad::parametric::Body& body, std::string& error)
{
    const auto found = presets_.find(presetId);
    if (found == presets_.end()) {
        error = "Visibility preset does not exist";
        return false;
    }
    clearIsolation();
    clearGhosting();
    return applyConfiguration(found->second.configuration, body, error);
}

std::vector<VisibilityPreset> VisibilityManager::presets() const
{
    std::vector<VisibilityPreset> result;
    result.reserve(presets_.size());
    for (const auto& [id, preset] : presets_) result.push_back(preset);
    std::sort(result.begin(), result.end(),
        [](const auto& first, const auto& second) { return first.id < second.id; });
    return result;
}

std::vector<std::string> VisibilityManager::isolatedFeatureIds() const
{
    return {isolatedFeatureIds_.begin(), isolatedFeatureIds_.end()};
}

std::vector<std::string> VisibilityManager::ghostedSelectionIds() const
{
    return {ghostedSelectionIds_.begin(), ghostedSelectionIds_.end()};
}

std::optional<std::string> VisibilityManager::createSavedView(SavedView view)
{
    if (view.name.empty()) return {};
    const auto normalized = normalizedName(view.name);
    for (const auto& [id, candidate] : savedViews_) {
        if (normalizedName(candidate.name) == normalized) return {};
    }
    if (view.id.empty()) {
        do {
            view.id = "view-" + std::to_string(nextSavedViewSequence_++);
        } while (savedViews_.contains(view.id));
    } else if (savedViews_.contains(view.id)) {
        return {};
    }
    const auto id = view.id;
    savedViews_.emplace(id, std::move(view));
    return id;
}

bool VisibilityManager::renameSavedView(
    const std::string& viewId, const std::string& name, std::string& error)
{
    error.clear();
    if (name.empty()) { error = "Saved view name cannot be empty"; return false; }
    const auto found = savedViews_.find(viewId);
    if (found == savedViews_.end()) return false;
    const auto normalized = normalizedName(name);
    for (const auto& [id, view] : savedViews_) {
        if (id != viewId && normalizedName(view.name) == normalized) {
            error = "A saved view with that name already exists";
            return false;
        }
    }
    found->second.name = name;
    return true;
}

bool VisibilityManager::deleteSavedView(const std::string& viewId)
{
    return savedViews_.erase(viewId) != 0;
}

std::vector<SavedView> VisibilityManager::savedViews() const
{
    std::vector<SavedView> result;
    result.reserve(savedViews_.size());
    for (const auto& [id, view] : savedViews_) result.push_back(view);
    std::sort(result.begin(), result.end(),
        [](const auto& first, const auto& second) { return first.id < second.id; });
    return result;
}

bool VisibilityManager::replaceSavedViews(
    std::vector<SavedView> views, std::string& error)
{
    error.clear();
    std::unordered_map<std::string, SavedView> replacement;
    std::set<std::string> names;
    for (auto& view : views) {
        const auto normalized = normalizedName(view.name);
        if (view.id.empty() || view.name.empty() || replacement.contains(view.id)
            || !names.insert(normalized).second) {
            error = "Invalid, duplicate, or empty saved view";
            return false;
        }
        replacement.emplace(view.id, std::move(view));
    }
    savedViews_ = std::move(replacement);
    nextSavedViewSequence_ = 1;
    return true;
}

bool VisibilityManager::applySavedView(
    const SavedView& view, cad::parametric::Body& body, std::string& error)
{
    if (!applyConfiguration(view.visibility, body, error)) return false;
    setIsolatedFeatures({view.isolatedFeatureIds.begin(), view.isolatedFeatureIds.end()});
    ghostOthers({view.ghostedSelectionIds.begin(), view.ghostedSelectionIds.end()});
    setSpatialRule(view.spatialRule);
    return true;
}

bool VisibilityManager::replacePresets(
    std::vector<VisibilityPreset> presets, std::string& error)
{
    error.clear();
    std::unordered_map<std::string, VisibilityPreset> replacement;
    std::set<std::string> names;
    for (auto& preset : presets) {
        const auto normalized = normalizedName(preset.name);
        if (preset.id.empty() || preset.name.empty() || replacement.contains(preset.id)
            || !names.insert(normalized).second) {
            error = "Invalid, duplicate, or empty visibility preset";
            return false;
        }
        for (const auto& [typeId, mode] : preset.configuration.filters.typeModes) {
            if (typeId.empty()) {
                error = "Visibility preset contains an empty type filter";
                return false;
            }
        }
        replacement.emplace(preset.id, std::move(preset));
    }
    presets_ = std::move(replacement);
    nextPresetSequence_ = 1;
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
    if (spatialRule_.enabled) {
        ++lastSpatialEvaluationCount_;
        if (!insideSpatialRule(feature)) {
            mode = moreRestrictive(mode, spatialRule_.outsideMode);
            if (mode != VisibilityMode::Visible) return mode;
        }
    }
    if (!ghostedSelectionIds_.empty()
        && !ghostedSelectionIds_.contains(feature.id())) {
        return VisibilityMode::Ghosted;
    }
    return VisibilityMode::Visible;
}

bool VisibilityManager::insideSpatialRule(
    const cad::parametric::ParametricFeature& feature) const
{
    if (!spatialRule_.enabled) return true;
    Bnd_Box region;
    region.Update(spatialRule_.min.X(), spatialRule_.min.Y(), spatialRule_.min.Z(),
                  spatialRule_.max.X(), spatialRule_.max.Y(), spatialRule_.max.Z());
    auto found = boundingBoxes_.find(feature.id());
    if (found == boundingBoxes_.end()) {
        if (feature.shape().IsNull()) return false;
        Bnd_Box bounds;
        BRepBndLib::Add(feature.shape(), bounds);
        return !bounds.IsOut(region);
    }
    return !found->second.IsOut(region);
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

VisibilityUpdate VisibilityManager::evaluate(const cad::parametric::Body& body)
{
    VisibilityUpdate update;
    lastSpatialEvaluationCount_ = 0;
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

    std::unordered_map<std::string, VisibilityMode> currentModes;
    currentModes.reserve(body.features().size());
    for (const auto& feature : body.features()) {
        const auto mode = modeForFeature(
            *feature, hiddenFeatureSet, groupModes, filters_);
        currentModes.emplace(feature->id(), mode);
        ++update.evaluatedFeatureCount;

        const auto previous = effectiveModes_.find(feature->id());
        const auto oldMode = previous == effectiveModes_.end()
            ? VisibilityMode::Visible : previous->second;
        if (oldMode != mode) {
            update.changes.push_back({feature->id(), oldMode, mode});
        }
    }
    effectiveModes_ = std::move(currentModes);
    lastVisibilityEvaluationCount_ = update.evaluatedFeatureCount;
    lastVisibilityChangeCount_ = update.changes.size();
    return update;
}

std::size_t VisibilityManager::lastVisibilityEvaluationCount() const noexcept
{
    return lastVisibilityEvaluationCount_;
}

std::size_t VisibilityManager::lastVisibilityChangeCount() const noexcept
{
    return lastVisibilityChangeCount_;
}

std::size_t VisibilityManager::lastSpatialEvaluationCount() const noexcept
{
    return lastSpatialEvaluationCount_;
}

} // namespace cad::application
