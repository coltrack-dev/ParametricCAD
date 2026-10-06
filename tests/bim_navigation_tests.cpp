#include "application/BimNavigationModel.h"
#include "model/Body.h"
#include "operations/BasicFeatures.h"
#include "operations/ImportedFeature.h"

#include <QtTest/QtTest>

using cad::application::BimNavigationModel;
using cad::parametric::Body;
using cad::parametric::ImportedFeature;

class BimNavigationTests final : public QObject
{
    Q_OBJECT

private slots:
    void indexesMetadataAndUsesDeterministicCategories()
    {
        Body body;
        body.addFeature(std::make_shared<ImportedFeature>(
            "door-id", "Front Door", cad::modeling::BasicFeatures::box(1, 1, 1),
            "IFC", "building.ifc", "door-global", "IfcDoor", "",
            "Building A", "Ground Floor"));
        body.addFeature(std::make_shared<ImportedFeature>(
            "wall-id", "Wall", cad::modeling::BasicFeatures::box(2, 1, 1),
            "IFC", "building.ifc", "wall-global", "IfcWall", "",
            "Building A", "Ground Floor"));
        body.addFeature(std::make_shared<ImportedFeature>(
            "unknown-id", "Proxy", cad::modeling::BasicFeatures::box(1, 2, 1),
            "IFC", "building.ifc", "proxy-global", "IfcBuildingElementProxy", "",
            "", ""));

        BimNavigationModel navigation;
        navigation.rebuild(body);

        QCOMPARE(navigation.buildings().size(), std::size_t{2});
        QCOMPARE(navigation.buildings()[0].name, std::string("<No Building>"));
        QCOMPARE(navigation.buildings()[1].name, std::string("Building A"));

        const auto& building = navigation.buildings()[1];
        QCOMPARE(building.storeys.size(), std::size_t{1});
        QCOMPARE(building.storeys[0].name, std::string("Ground Floor"));
        QCOMPARE(building.storeys[0].categories.size(), std::size_t{2});
        QCOMPARE(building.storeys[0].categories[0].name, std::string("Doors"));
        QCOMPARE(building.storeys[0].categories[1].name, std::string("Walls"));

        const auto storeyIds = navigation.featureIdsForStorey("Building A", "Ground Floor");
        QCOMPARE(storeyIds, std::vector<std::string>({"door-id", "wall-id"}));
        const auto categoryIds = navigation.featureIdsForCategory(
            "Building A", "Ground Floor", "Doors");
        QCOMPARE(categoryIds, std::vector<std::string>({"door-id"}));

        QCOMPARE(navigation.buildings()[0].storeys[0].name,
                 std::string("<Unassigned Storey>"));
        QCOMPARE(navigation.buildings()[0].storeys[0].categories[0].name,
                 std::string("Building Element Proxies"));
    }
};

QTEST_MAIN(BimNavigationTests)
#include "bim_navigation_tests.moc"
