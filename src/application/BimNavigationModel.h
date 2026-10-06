#pragma once

#include <string>
#include <vector>

namespace cad::parametric { class Body; }

namespace cad::application {

struct BimFeatureEntry
{
    std::string featureId;
    std::string name;
    std::string globalId;
    std::string entityType;
    std::string building;
    std::string storey;
    std::string category;
};

struct BimCategoryEntry
{
    std::string name;
    std::vector<BimFeatureEntry> features;
};

struct BimStoreyEntry
{
    std::string name;
    std::vector<BimCategoryEntry> categories;
};

struct BimBuildingEntry
{
    std::string name;
    std::vector<BimStoreyEntry> storeys;
};

class BimNavigationModel final
{
public:
    void rebuild(const cad::parametric::Body& body);

    const std::vector<BimBuildingEntry>& buildings() const noexcept;
    std::vector<std::string> featureIdsForStorey(const std::string& building,
                                                  const std::string& storey) const;
    std::vector<std::string> featureIdsForCategory(
        const std::string& building, const std::string& storey,
        const std::string& category) const;
    std::vector<std::string> featureIdsForBuilding(const std::string& building) const;

    static std::string friendlyCategory(const std::string& entityType);

private:
    std::vector<BimBuildingEntry> buildings_;
};

} // namespace cad::application
