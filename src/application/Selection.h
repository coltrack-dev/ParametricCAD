#pragma once

#include <optional>
#include <algorithm>
#include <string>
#include <vector>

namespace cad::application {

// Viewer-independent selection vocabulary used at the application boundary.
// subshapeIndex is transient: it refers to the current feature shape only.
enum class SelectionKind
{
    Unknown,
    Object,
    Face,
    Edge,
    Vertex
};

struct SelectionDescriptor
{
    std::string featureId;
    SelectionKind kind{SelectionKind::Object};
    std::optional<int> subshapeIndex;

    friend bool operator==(const SelectionDescriptor&, const SelectionDescriptor&) = default;
};

struct SelectionSnapshot
{
    std::vector<SelectionDescriptor> items;

    std::vector<std::string> featureIds() const
    {
        std::vector<std::string> result;
        for (const auto& item : items) {
            if (item.featureId.empty()) continue;
            if (std::find(result.begin(), result.end(), item.featureId) == result.end()) {
                result.push_back(item.featureId);
            }
        }
        return result;
    }

    std::vector<std::string> selectedObjectIds() const
    {
        std::vector<std::string> result;
        for (const auto& item : items) {
            if (item.kind != SelectionKind::Object || item.featureId.empty()) continue;
            if (std::find(result.begin(), result.end(), item.featureId) == result.end()) {
                result.push_back(item.featureId);
            }
        }
        return result;
    }
};

} // namespace cad::application
