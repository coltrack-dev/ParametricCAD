#include "application/VisibilityManager.h"

#include "model/Body.h"
#include "model/FeatureVisibility.h"
#include "model/ParametricFeature.h"

namespace cad::application {

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
}

VisibilityMode VisibilityManager::effectiveMode(
    const cad::parametric::ParametricFeature& feature,
    const cad::parametric::Body& body) const
{
    const auto hiddenIds = cad::parametric::hiddenFeatureIds(body, isolatedFeatureIds_);
    return modeForFeature(feature, {hiddenIds.begin(), hiddenIds.end()});
}

VisibilityMode VisibilityManager::modeForFeature(
    const cad::parametric::ParametricFeature& feature,
    const std::set<std::string>& hiddenFeatureIds) const
{
    if (hiddenFeatureIds.contains(feature.id())) {
        return VisibilityMode::Hidden;
    }
    if (!ghostedSelectionIds_.empty()
        && !ghostedSelectionIds_.contains(feature.id())) {
        return VisibilityMode::Ghosted;
    }
    return VisibilityMode::Visible;
}

VisibilityProjection VisibilityManager::projection(
    const cad::parametric::Body& body) const
{
    VisibilityProjection result;
    result.reserve(body.features().size());
    const auto hiddenIds = cad::parametric::hiddenFeatureIds(body, isolatedFeatureIds_);
    const std::set<std::string> hiddenFeatureSet(hiddenIds.begin(), hiddenIds.end());
    for (const auto& feature : body.features()) {
        result.push_back({feature->id(), modeForFeature(*feature, hiddenFeatureSet)});
    }
    return result;
}

} // namespace cad::application
