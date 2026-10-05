#pragma once

#include "application/VisibilityMode.h"

#include <set>
#include <optional>
#include <string>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cad::parametric {
class Body;
class ParametricFeature;
enum class FeatureRole;
}

namespace cad::application {

struct FeatureVisibilityState
{
    std::string featureId;
    VisibilityMode mode{VisibilityMode::Visible};
};

using VisibilityProjection = std::vector<FeatureVisibilityState>;

struct VisibilityChange
{
    std::string featureId;
    VisibilityMode oldMode{VisibilityMode::Visible};
    VisibilityMode newMode{VisibilityMode::Visible};
};

struct VisibilityUpdate
{
    std::vector<VisibilityChange> changes;
    std::size_t evaluatedFeatureCount{0};
};

struct VisibilityGroup
{
    std::string id;
    std::string name;
    std::optional<std::string> parentId;
    std::unordered_set<std::string> memberFeatureIds;
    VisibilityMode mode{VisibilityMode::Visible};
};

enum class VisibilityCategory
{
    Sketches,
    Profiles,
    Primitives,
    Operations,
    Patterns
};

struct VisibilityFilterState
{
    std::unordered_map<std::string, VisibilityMode> typeModes;
    std::map<cad::parametric::FeatureRole, VisibilityMode> roleModes;
    std::map<VisibilityCategory, VisibilityMode> categoryModes;

    bool empty() const noexcept
    {
        return typeModes.empty() && roleModes.empty() && categoryModes.empty();
    }

    bool operator==(const VisibilityFilterState&) const = default;
};

struct VisibilityConfiguration
{
    std::unordered_map<std::string, VisibilityMode> groupModes;
    VisibilityFilterState filters;
    std::unordered_map<std::string, bool> featureVisibility;

    bool operator==(const VisibilityConfiguration&) const = default;
};

struct VisibilityPreset
{
    std::string id;
    std::string name;
    VisibilityConfiguration configuration;

    bool operator==(const VisibilityPreset&) const = default;
};

std::optional<VisibilityCategory> visibilityCategoryFor(
    const cad::parametric::ParametricFeature& feature);
const char* visibilityCategoryId(VisibilityCategory category) noexcept;
const char* visibilityCategoryName(VisibilityCategory category) noexcept;
std::vector<VisibilityCategory> visibilityCategories();
std::vector<std::string> knownVisibilityTypeIds();

class VisibilityManager final
{
public:
    void setIsolatedFeatures(const std::vector<std::string>& featureIds);
    void clearIsolation();
    bool isolationActive() const noexcept;
    void ghostOthers(const std::vector<std::string>& selectedIds);
    void clearGhosting();
    void clear();

    std::optional<std::string> createGroup(
        const std::string& name, std::optional<std::string> parentId = {});
    bool removeGroup(const std::string& groupId);
    bool renameGroup(const std::string& groupId, const std::string& name);
    bool setGroupParent(const std::string& groupId,
                        std::optional<std::string> parentId);
    bool addFeaturesToGroup(const std::string& groupId,
                            const std::vector<std::string>& featureIds);
    bool removeFeaturesFromGroup(const std::string& groupId,
                                 const std::vector<std::string>& featureIds);
    bool setGroupVisibility(const std::string& groupId, VisibilityMode mode);
    void resetGroupVisibility();
    std::vector<VisibilityGroup> groups() const;
    bool replaceGroups(std::vector<VisibilityGroup> groups, std::string& error);

    bool setTypeFilter(const std::string& typeId, VisibilityMode mode);
    bool clearTypeFilter(const std::string& typeId);
    bool setRoleFilter(cad::parametric::FeatureRole role, VisibilityMode mode);
    bool clearRoleFilter(cad::parametric::FeatureRole role);
    bool setCategoryFilter(VisibilityCategory category, VisibilityMode mode);
    bool clearCategoryFilter(VisibilityCategory category);
    void clearFilters();
    const VisibilityFilterState& filters() const noexcept;
    bool replaceFilters(VisibilityFilterState filters, std::string& error);

    std::optional<std::string> createPreset(
        const std::string& name, const cad::parametric::Body& body);
    bool saveCurrentAsPreset(const std::string& name,
                             const cad::parametric::Body& body,
                             std::string& error);
    bool updatePreset(const std::string& presetId,
                      const cad::parametric::Body& body);
    bool renamePreset(const std::string& presetId, const std::string& name,
                      std::string& error);
    bool deletePreset(const std::string& presetId);
    bool applyPreset(const std::string& presetId,
                     cad::parametric::Body& body,
                     std::string& error);
    std::vector<VisibilityPreset> presets() const;
    bool replacePresets(std::vector<VisibilityPreset> presets, std::string& error);
    VisibilityConfiguration captureConfiguration(
        const cad::parametric::Body& body) const;
    bool applyConfiguration(const VisibilityConfiguration& configuration,
                            cad::parametric::Body& body,
                            std::string& error);

    VisibilityMode effectiveMode(
        const cad::parametric::ParametricFeature& feature,
        const cad::parametric::Body& body) const;
    VisibilityProjection projection(const cad::parametric::Body& body) const;
    VisibilityUpdate evaluate(const cad::parametric::Body& body);
    std::size_t lastVisibilityEvaluationCount() const noexcept;
    std::size_t lastVisibilityChangeCount() const noexcept;

private:
    static VisibilityMode moreRestrictive(VisibilityMode first, VisibilityMode second);
    VisibilityMode groupEffectiveMode(
        const std::string& groupId,
        std::unordered_map<std::string, VisibilityMode>& effective,
        std::set<std::string>& visiting) const;
    VisibilityMode modeForFeature(
        const cad::parametric::ParametricFeature& feature,
        const std::set<std::string>& hiddenFeatureIds,
        const std::unordered_map<std::string, VisibilityMode>& groupModes,
        const VisibilityFilterState& filters) const;
    VisibilityMode filterModeForFeature(
        const cad::parametric::ParametricFeature& feature,
        const VisibilityFilterState& filters) const;

    std::set<std::string> isolatedFeatureIds_;
    std::set<std::string> ghostedSelectionIds_;
    std::unordered_map<std::string, VisibilityGroup> groups_;
    VisibilityFilterState filters_;
    std::unordered_map<std::string, VisibilityPreset> presets_;
    std::uint64_t nextPresetSequence_{1};
    std::uint64_t nextGroupSequence_{1};
    std::unordered_map<std::string, VisibilityMode> effectiveModes_;
    std::size_t lastVisibilityEvaluationCount_{0};
    std::size_t lastVisibilityChangeCount_{0};
};

} // namespace cad::application
