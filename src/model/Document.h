#pragma once

#include <memory>
#include <vector>

class Feature;

class Document
{
public:
    Feature& addFeature(std::unique_ptr<Feature> feature);
    void clear();

    [[nodiscard]] const std::vector<std::unique_ptr<Feature>>& features() const noexcept;

private:
    std::vector<std::unique_ptr<Feature>> features_;
};
