#include "model/FeatureVisibility.h"
#include "model/Body.h"

namespace cad::parametric {
std::vector<std::string> hiddenFeatureIds(const Body& body)
{
    std::vector<std::string> hidden;
    for (const auto& feature : body.features()) {
        if (feature->state() != FeatureState::UpToDate || feature->shape().IsNull()) {
            hidden.push_back(feature->id());
            continue;
        }
        const auto dependencies = feature->hiddenDependencyIds();
        hidden.insert(hidden.end(), dependencies.begin(), dependencies.end());
    }
    return hidden;
}
}
