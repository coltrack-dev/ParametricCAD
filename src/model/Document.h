#pragma once

#include <memory>
#include <vector>

class Feature;

class Document
{
public:
    Document() = default;
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    Document(Document&&) noexcept = default;
    Document& operator=(Document&&) noexcept = default;

    Feature& addFeature(std::unique_ptr<Feature> feature);
    void clear();

    [[nodiscard]]
    const std::vector<std::unique_ptr<Feature>>& features() const noexcept;

private:
    std::vector<std::unique_ptr<Feature>> features_;
};
