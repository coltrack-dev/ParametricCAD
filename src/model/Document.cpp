#include "model/Document.h"
#include "model/Feature.h"

#include <stdexcept>
#include <utility>

Document::~Document() = default;

Feature& Document::addFeature(std::unique_ptr<Feature> feature)
{
    return insertFeature(features_.size(), std::move(feature));
}

Feature& Document::insertFeature(std::size_t position, std::unique_ptr<Feature> feature)
{
    if (!feature) throw std::invalid_argument("Feature must not be null");
    if (position > features_.size()) throw std::out_of_range("Invalid feature position");
    if (feature->shape().IsNull()) feature->recompute();
    Feature& reference = *feature;
    features_.insert(features_.begin() + static_cast<std::ptrdiff_t>(position), std::move(feature));
    return reference;
}

std::unique_ptr<Feature> Document::takeFeature(std::size_t position)
{
    if (position >= features_.size()) throw std::out_of_range("Invalid feature position");
    auto feature = std::move(features_[position]);
    features_.erase(features_.begin() + static_cast<std::ptrdiff_t>(position));
    return feature;
}

void Document::clear()
{
    features_.clear();
}

const std::vector<std::unique_ptr<Feature>>&
Document::features() const noexcept
{
    return features_;
}
