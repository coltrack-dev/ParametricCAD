#pragma once

#include "application/VisibilityMode.h"

#include <Bnd_Box.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

#include <set>
#include <optional>
#include <string>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>

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

enum class SpatialRelation
{
    Intersects
};

struct SpatialVisibilityRule
{
    bool enabled{false};
    gp_Pnt min;
    gp_Pnt max;
    SpatialRelation relation{SpatialRelation::Intersects};
    VisibilityMode outsideMode{VisibilityMode::Hidden};
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

struct SavedView
{
    std::string id;
    std::string name;
    VisibilityConfiguration visibility;
    std::set<std::string> isolatedFeatureIds;
    std::set<std::string> ghostedSelectionIds;
    SpatialVisibilityRule spatialRule;
    bool sectionActive{false};
    int sectionAxis{2};
    bool sectionFlipped{false};
    gp_Pnt sectionOrigin;
    bool cameraValid{false};
    gp_Pnt cameraEye;
    gp_Pnt cameraCenter;
    gp_Dir cameraUp{0, 0, 1};
    double cameraScale{1.0};
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
    void setSpatialRule(const SpatialVisibilityRule& rule);
    void clearSpatialRule();
    const SpatialVisibilityRule& spatialRule() const noexcept;
    void updateBoundingBoxes(const cad::parametric::Body& body);
    std::optional<std::pair<gp_Pnt, gp_Pnt>> spatialBounds(
        const cad::parametric::Body& body,
        const std::vector<std::string>& featureIds) const;
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
    std::vector<std::string> isolatedFeatureIds() const;
    std::vector<std::string> ghostedSelectionIds() const;
    std::optional<std::string> createSavedView(SavedView view);
    bool renameSavedView(const std::string& viewId, const std::string& name,
                         std::string& error);
    bool deleteSavedView(const std::string& viewId);
    std::vector<SavedView> savedViews() const;
    bool replaceSavedViews(std::vector<SavedView> views, std::string& error);
    bool applySavedView(const SavedView& view, cad::parametric::Body& body,
                        std::string& error);
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
    std::size_t lastSpatialEvaluationCount() const noexcept;

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
    bool insideSpatialRule(
        const cad::parametric::ParametricFeature& feature) const;

    std::set<std::string> isolatedFeatureIds_;
    std::set<std::string> ghostedSelectionIds_;
    std::unordered_map<std::string, VisibilityGroup> groups_;
    VisibilityFilterState filters_;
    std::unordered_map<std::string, VisibilityPreset> presets_;
    std::unordered_map<std::string, SavedView> savedViews_;
    std::uint64_t nextPresetSequence_{1};
    std::uint64_t nextGroupSequence_{1};
    std::uint64_t nextSavedViewSequence_{1};
    std::unordered_map<std::string, VisibilityMode> effectiveModes_;
    SpatialVisibilityRule spatialRule_;
    std::unordered_map<std::string, Bnd_Box> boundingBoxes_;
    std::size_t lastVisibilityEvaluationCount_{0};
    std::size_t lastVisibilityChangeCount_{0};
    mutable std::size_t lastSpatialEvaluationCount_{0};
};

} // namespace cad::application
