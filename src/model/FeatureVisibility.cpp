#include "model/FeatureVisibility.h"
#include "model/Body.h"
#include "operations/ParametricFeatures.h"

namespace cad::parametric {
std::vector<std::string> hiddenFeatureIds(const Body& body)
{
    std::vector<std::string> hidden;
    for (const auto& feature : body.features()) {
        if (feature->state() != FeatureState::UpToDate || feature->shape().IsNull()) {
            hidden.push_back(feature->id());
            continue;
        }
        const auto cut = std::dynamic_pointer_cast<BooleanFeature>(feature);
        if (cut && cut->operation() == BooleanOperation::Cut) {
            hidden.push_back(cut->left()->id());
            hidden.push_back(cut->right()->id());
        }
    }
    return hidden;
}
}
