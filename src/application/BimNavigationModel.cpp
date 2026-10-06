#include "application/BimNavigationModel.h"

#include "model/Body.h"
#include "operations/ImportedFeature.h"

#include <algorithm>
#include <map>

namespace cad::application {
namespace {

std::string valueOr(const QString& value, const char* fallback)
{
    const auto text = value.trimmed().toStdString();
    return text.empty() ? fallback : text;
}

template <typename Node>
void sortFeatures(Node& node)
{
    std::sort(node.features.begin(), node.features.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.featureId < b.featureId;
    });
}

} // namespace

std::string BimNavigationModel::friendlyCategory(const std::string& type)
{
    static const std::map<std::string, std::string> names{
        {"IfcWall", "Walls"}, {"IfcWallStandardCase", "Walls"},
        {"IfcDoor", "Doors"}, {"IfcWindow", "Windows"},
        {"IfcSlab", "Slabs"}, {"IfcColumn", "Columns"},
        {"IfcBeam", "Beams"}, {"IfcStair", "Stairs"},
        {"IfcRoof", "Roofs"}, {"IfcRailing", "Railings"},
        {"IfcCovering", "Coverings"},
        {"IfcFurnishingElement", "Furniture"},
        {"IfcBuildingElementProxy", "Building Element Proxies"}
    };
    const auto found = names.find(type);
    return found == names.end() ? type : found->second;
}

void BimNavigationModel::rebuild(const cad::parametric::Body& body)
{
    std::map<std::string, std::map<std::string, std::map<std::string,
        std::vector<BimFeatureEntry>>>> index;

    for (const auto& feature : body.features()) {
        if (!feature || std::string(feature->typeId()) != "IfcImported") continue;
        const auto& imported = static_cast<const cad::parametric::ImportedFeature&>(*feature);
        BimFeatureEntry entry{
            feature->id(), feature->name().empty() ? imported.ifcEntityType().toStdString()
                                                    : feature->name(),
            imported.ifcGlobalId().toStdString(), imported.ifcEntityType().toStdString(),
            valueOr(imported.ifcBuilding(), "<No Building>"),
            valueOr(imported.ifcStorey(), "<Unassigned Storey>"),
            friendlyCategory(imported.ifcEntityType().toStdString())};
        index[entry.building][entry.storey][entry.category].push_back(std::move(entry));
    }

    buildings_.clear();
    for (auto& [buildingName, storeys] : index) {
        BimBuildingEntry building{buildingName, {}};
        for (auto& [storeyName, categories] : storeys) {
            BimStoreyEntry storey{storeyName, {}};
            for (auto& [categoryName, features] : categories) {
                BimCategoryEntry category{categoryName, std::move(features)};
                sortFeatures(category);
                storey.categories.push_back(std::move(category));
            }
            storey.categories.shrink_to_fit();
            building.storeys.push_back(std::move(storey));
        }
        buildings_.push_back(std::move(building));
    }
}

const std::vector<BimBuildingEntry>& BimNavigationModel::buildings() const noexcept
{
    return buildings_;
}

std::vector<std::string> BimNavigationModel::featureIdsForStorey(
    const std::string& buildingName, const std::string& storeyName) const
{
    std::vector<std::string> ids;
    for (const auto& building : buildings_) if (building.name == buildingName)
        for (const auto& storey : building.storeys) if (storey.name == storeyName)
            for (const auto& category : storey.categories)
                for (const auto& feature : category.features) ids.push_back(feature.featureId);
    return ids;
}

std::vector<std::string> BimNavigationModel::featureIdsForCategory(
    const std::string& buildingName, const std::string& storeyName,
    const std::string& categoryName) const
{
    std::vector<std::string> ids;
    for (const auto& building : buildings_) if (building.name == buildingName)
        for (const auto& storey : building.storeys) if (storey.name == storeyName)
            for (const auto& category : storey.categories) if (category.name == categoryName)
                for (const auto& feature : category.features) ids.push_back(feature.featureId);
    return ids;
}

std::vector<std::string> BimNavigationModel::featureIdsForBuilding(
    const std::string& buildingName) const
{
    std::vector<std::string> ids;
    for (const auto& building : buildings_) if (building.name == buildingName)
        for (const auto& storey : building.storeys)
            for (const auto& category : storey.categories)
                for (const auto& feature : category.features) ids.push_back(feature.featureId);
    return ids;
}

} // namespace cad::application
