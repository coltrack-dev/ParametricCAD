#include "model/Document.h"
#include "model/Feature.h"

#include <stdexcept>
#include <utility>

Feature& Document::addFeature(std::unique_ptr<Feature> feature)
{
    if (!feature) {
        throw std::invalid_argument("Feature must not be null");
    }

    feature->recompute();
    Feature& reference = *feature;
    features_.push_back(std::move(feature));
    return reference;
}

void Document::clear()
{
    features_.clear();
}

const std::vector<std::unique_ptr<Feature>>& Document::features() const noexcept
{
    return features_;
}
