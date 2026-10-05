#pragma once

#include "application/VisibilityMode.h"

#include <set>
#include <optional>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cad::parametric {
class Body;
class ParametricFeature;
}

namespace cad::application {

struct FeatureVisibilityState
{
    std::string featureId;
    VisibilityMode mode{VisibilityMode::Visible};
};

using VisibilityProjection = std::vector<FeatureVisibilityState>;

struct VisibilityGroup
{
    std::string id;
    std::string name;
    std::optional<std::string> parentId;
    std::unordered_set<std::string> memberFeatureIds;
    VisibilityMode mode{VisibilityMode::Visible};
};

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

    VisibilityMode effectiveMode(
        const cad::parametric::ParametricFeature& feature,
        const cad::parametric::Body& body) const;
    VisibilityProjection projection(const cad::parametric::Body& body) const;

private:
    static VisibilityMode moreRestrictive(VisibilityMode first, VisibilityMode second);
    VisibilityMode groupEffectiveMode(
        const std::string& groupId,
        std::unordered_map<std::string, VisibilityMode>& effective,
        std::set<std::string>& visiting) const;
    VisibilityMode modeForFeature(
        const cad::parametric::ParametricFeature& feature,
        const std::set<std::string>& hiddenFeatureIds,
        const std::unordered_map<std::string, VisibilityMode>& groupModes) const;

    std::set<std::string> isolatedFeatureIds_;
    std::set<std::string> ghostedSelectionIds_;
    std::unordered_map<std::string, VisibilityGroup> groups_;
    std::uint64_t nextGroupSequence_{1};
};

} // namespace cad::application
