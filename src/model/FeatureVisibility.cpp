#include "model/FeatureVisibility.h"
#include "model/Body.h"

namespace cad::parametric {
std::vector<std::string> hiddenFeatureIds(const Body& body)
{
    return hiddenFeatureIds(body, {});
}

std::vector<std::string> hiddenFeatureIds(
    const Body& body, const std::set<std::string>& isolatedFeatureIds)
{
    std::vector<std::string> hidden;
    for (const auto& feature : body.features()) {
        const bool modelVisible = feature->state() == FeatureState::UpToDate
            && !feature->shape().IsNull();
        const bool isolatedIn = !isolatedFeatureIds.empty()
            && isolatedFeatureIds.contains(feature->id());
        const bool isolatedOut = !isolatedFeatureIds.empty()
            && !isolatedIn;
        if ((!feature->userVisible() && !isolatedIn) || !modelVisible || isolatedOut) {
            hidden.push_back(feature->id());
        }
        if (!modelVisible) continue;
        const auto dependencies = feature->hiddenDependencyIds();
        hidden.insert(hidden.end(), dependencies.begin(), dependencies.end());
    }
    return hidden;
}
}
