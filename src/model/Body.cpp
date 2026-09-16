#include "model/Body.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace cad::parametric {

void Body::addFeature(const FeaturePtr& feature)
{
    insertFeature(features_.size(), feature);
}

void Body::insertFeature(std::size_t position, const FeaturePtr& feature)
{
    if (!feature) throw std::invalid_argument("Body feature must not be null");
    if (position > features_.size()) throw std::out_of_range("Invalid feature position");
    if (findFeature(feature->id())) {
        throw std::invalid_argument("Feature with id '" + feature->id() + "' already exists");
    }
    const auto insertion = features_.begin() + static_cast<std::ptrdiff_t>(position);
    for (const auto& weak : feature->dependencies()) {
        const auto dependency = weak.lock();
        if (!dependency || std::find(features_.begin(), insertion, dependency) == insertion) {
            throw std::invalid_argument("Dependencies must precede feature '" + feature->id() + "'");
        }
    }
    features_.insert(insertion, feature);
}

bool Body::canRemoveFeature(const std::string& featureId) const
{
    const auto source = findFeature(featureId);
    if (!source) return false;
    for (const auto& feature : features_) {
        for (const auto& dependency : feature->dependencies()) {
            if (dependency.lock() == source) return false;
        }
    }
    return true;
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

    if (!canRemoveFeature(featureId)) {
        throw std::invalid_argument("Remove dependent features before deleting '" + featureId + "'");
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
    if (!findFeature(featureId)) return;
    std::unordered_set<std::string> affected{featureId};
    // Body insertion order is dependency order; unrelated branches stay clean.
    for (const auto& feature : features_) {
        for (const auto& dependency : feature->dependencies()) {
            const auto source = dependency.lock();
            if (source && affected.contains(source->id())) affected.insert(feature->id());
        }
        if (affected.contains(feature->id())) feature->markDirty();
    }
}

const std::string& Body::lastError() const noexcept
{
    return lastError_;
}

} // namespace cad::parametric
