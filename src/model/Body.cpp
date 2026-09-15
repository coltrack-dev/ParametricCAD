#include "model/Body.h"

#include <algorithm>
#include <stdexcept>

namespace cad::parametric {

void Body::addFeature(const FeaturePtr& feature)
{
    if (!feature) {
        throw std::invalid_argument("Body feature must not be null");
    }

    if (findFeature(feature->id())) {
        throw std::invalid_argument(
            "Feature with id '" + feature->id() + "' already exists"
        );
    }

    features_.push_back(feature);
}

bool Body::removeFeature(const std::string& featureId)
{
    const auto iterator = std::find_if(
        features_.begin(),
        features_.end(),
        [&featureId](const FeaturePtr& feature) {
            return feature->id() == featureId;
        }
    );

    if (iterator == features_.end()) {
        return false;
    }

    features_.erase(iterator);
    resultShape_.Nullify();
    return true;
}

Body::FeaturePtr Body::findFeature(const std::string& featureId) const
{
    const auto iterator = std::find_if(
        features_.begin(),
        features_.end(),
        [&featureId](const FeaturePtr& feature) {
            return feature->id() == featureId;
        }
    );

    return iterator == features_.end()
        ? FeaturePtr{}
        : *iterator;
}

const std::vector<Body::FeaturePtr>& Body::features() const noexcept
{
    return features_;
}

const TopoDS_Shape& Body::shape() const noexcept
{
    return resultShape_;
}

bool Body::recompute()
{
    lastError_.clear();
    resultShape_.Nullify();

    for (const FeaturePtr& feature : features_) {
        if (feature->isDirty() && !feature->recompute()) {
            lastError_ =
                "Feature '" + feature->name() + "' failed: " + feature->error();
            return false;
        }

        if (feature->state() == FeatureState::Failed) {
            lastError_ =
                "Feature '" + feature->name() + "' is in failed state: "
                + feature->error();
            return false;
        }

        if (!feature->shape().IsNull()) {
            resultShape_ = feature->shape();
        }
    }

    return !resultShape_.IsNull() || features_.empty();
}

void Body::markDirtyFrom(const std::string& featureId)
{
    bool found = false;

    for (const FeaturePtr& feature : features_) {
        if (feature->id() == featureId) {
            found = true;
        }

        if (found) {
            feature->markDirty();
        }
    }
}

const std::string& Body::lastError() const noexcept
{
    return lastError_;
}

} // namespace cad::parametric
