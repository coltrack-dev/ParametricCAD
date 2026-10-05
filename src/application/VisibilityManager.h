#pragma once

#include "application/VisibilityMode.h"

#include <set>
#include <string>
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

class VisibilityManager final
{
public:
    void setIsolatedFeatures(const std::vector<std::string>& featureIds);
    void clearIsolation();
    bool isolationActive() const noexcept;
    void ghostOthers(const std::vector<std::string>& selectedIds);
    void clearGhosting();
    void clear();

    VisibilityMode effectiveMode(
        const cad::parametric::ParametricFeature& feature,
        const cad::parametric::Body& body) const;
    VisibilityProjection projection(const cad::parametric::Body& body) const;

private:
    VisibilityMode modeForFeature(
        const cad::parametric::ParametricFeature& feature,
        const std::set<std::string>& hiddenFeatureIds) const;

    std::set<std::string> isolatedFeatureIds_;
    std::set<std::string> ghostedSelectionIds_;
};

} // namespace cad::application
