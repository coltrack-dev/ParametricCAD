#include "application/IfcImporter.h"
#include "application/BimNavigationModel.h"
#include "model/Body.h"
#include "model/Document.h"
#include "model/Feature.h"
#include "model/ProjectFile.h"
#include "model/ShapePayload.h"
#include "operations/ImportedFeature.h"

#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopExp_Explorer.hxx>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <cmath>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

#ifndef PARAMETRIC_CAD_SOURCE_DIR
#define PARAMETRIC_CAD_SOURCE_DIR "."
#endif

namespace {
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

double width(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    return xmax - xmin;
}

struct Bounds
{
    double xmin{0};
    double ymin{0};
    double zmin{0};
    double xmax{0};
    double ymax{0};
    double zmax{0};
    bool valid{false};
};

Bounds bounds(const cad::parametric::Body& body)
{
    Bnd_Box box;
    bool found = false;
    for (const auto& feature : body.features()) {
        if (feature->shape().IsNull()) continue;
        Bnd_Box current;
        BRepBndLib::Add(feature->shape(), current);
        box.Add(current);
        found = true;
    }
    Bounds result;
    if (found) {
        box.Get(result.xmin, result.ymin, result.zmin,
                result.xmax, result.ymax, result.zmax);
        result.valid = true;
    }
    return result;
}

std::string shapeKind(const TopoDS_Shape& shape)
{
    switch (shape.ShapeType()) {
    case TopAbs_SOLID: return "solid";
    case TopAbs_COMPOUND: return "compound";
    case TopAbs_SHELL: return "shell";
    case TopAbs_FACE: return "face";
    default: return "other";
    }
}
}

int main()
{
    try {
        const auto sourceShape = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
        const auto payload = cad::persistence::encodeBRep(sourceShape);
        const auto restoredShape = cad::persistence::decodeBRep(payload);
        check(!restoredShape.IsNull() && restoredShape.ShapeType() == sourceShape.ShapeType(),
              "B-Rep payload shape");
        check(std::abs(width(restoredShape) - 10.0) < 1.0e-5, "B-Rep payload bounds");

        QTemporaryDir directory;
        check(directory.isValid(), "temporary directory");
        cad::parametric::Body body;
        auto imported = std::make_shared<cad::parametric::ImportedFeature>(
            "ifc-test", "Imported box", sourceShape, "IFC", "source.ifc",
            "global-id", "IfcWall", "description", "Building", "Storey");
        body.addFeature(imported);
        Document document;
        QString error;
        const auto path = directory.filePath("imported.pcad");
        check(ProjectFile::save(path, document, body, error), "save imported feature");
        body = {};
        Document loadedDocument;
        cad::parametric::Body loadedBody;
        check(ProjectFile::load(path, loadedDocument, loadedBody, error), "load imported feature");
        const auto loadedBase = loadedBody.findFeature("ifc-test");
        check(loadedBase && std::string(loadedBase->typeId()) == "IfcImported",
              "import feature type roundtrip");
        const auto loaded = std::static_pointer_cast<cad::parametric::ImportedFeature>(loadedBase);
        check(loaded->ifcGlobalId() == "global-id", "import metadata roundtrip");
        check(std::abs(width(loaded->shape()) - 10.0) < 1.0e-5,
              "import geometry roundtrip");

        cad::application::IfcImporter importer;
        cad::parametric::Body unavailableBody;
        const auto result = importer.importIntoBody(
            QStringLiteral(PARAMETRIC_CAD_SOURCE_DIR) + "/examples/ifc/Duplex_A_20110505.ifc",
            unavailableBody);
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
        check(result.statistics.schema == "IFC2X3", "Duplex schema");
        check(result.statistics.importedCount > 0 && !unavailableBody.features().empty(),
              "Duplex import");
        std::cout << "Duplex IFC import\n"
                  << "  schema: " << result.statistics.schema.toStdString() << '\n'
                  << "  considered: " << result.statistics.consideredCount << '\n'
                  << "  with geometry: " << result.statistics.productsWithGeometry << '\n'
                  << "  imported: " << result.statistics.importedCount << '\n'
                  << "  skipped: " << result.statistics.skippedCount << '\n'
                  << "  failed: " << result.statistics.failedCount << '\n'
                  << "  geometry ms: " << result.statistics.geometryMilliseconds << '\n'
                  << "  total ms: " << result.statistics.totalMilliseconds << '\n';
        for (const auto& [type, count] : result.statistics.importedByEntity)
            std::cout << "  imported " << type.toStdString() << ": " << count << '\n';

        const auto importedBounds = bounds(unavailableBody);
        check(importedBounds.valid, "Duplex bounds");
        std::cout << "  bounds mm: [" << importedBounds.xmin << ", "
                  << importedBounds.ymin << ", " << importedBounds.zmin << "] - ["
                  << importedBounds.xmax << ", " << importedBounds.ymax << ", "
                  << importedBounds.zmax << "]\n";
        check(importedBounds.xmax - importedBounds.xmin > 5000.0
                  && importedBounds.xmax - importedBounds.xmin < 100000.0,
              "Duplex unit normalization");

        std::map<std::string, int> topologyCounts;
        std::map<std::string, std::shared_ptr<cad::parametric::ImportedFeature>> representatives;
        int nonNull = 0;
        int spatialMetadata = 0;
        for (const auto& feature : unavailableBody.features()) {
            auto importedFeature = std::static_pointer_cast<cad::parametric::ImportedFeature>(feature);
            if (importedFeature->shape().IsNull()) continue;
            ++nonNull;
            if (!importedFeature->ifcBuilding().isEmpty()
                && !importedFeature->ifcStorey().isEmpty()) ++spatialMetadata;
            ++topologyCounts[shapeKind(importedFeature->shape())];
            const auto type = importedFeature->ifcEntityType().toStdString();
            if (!representatives.contains(type)) representatives[type] = importedFeature;
        }
        check(spatialMetadata > 0, "Duplex spatial containment metadata");
        std::cout << "  products with building/storey metadata: " << spatialMetadata << '\n';
        for (const auto& type : {std::string("IfcWall"), std::string("IfcSlab"),
                                 std::string("IfcDoor"), std::string("IfcWindow")}) {
            const auto& feature = representatives.at(type);
            std::cout << "  spatial " << type << " "
                      << feature->ifcGlobalId().toStdString() << " building='"
                      << feature->ifcBuilding().toStdString() << "' storey='"
                      << feature->ifcStorey().toStdString() << "'\n";
            check(!feature->ifcBuilding().isEmpty() && !feature->ifcStorey().isEmpty(),
                  "representative spatial metadata");
        }
        cad::application::BimNavigationModel navigation;
        navigation.rebuild(unavailableBody);
        check(!navigation.buildings().empty(), "Duplex BIM buildings");
        std::cout << "  BIM hierarchy:\n";
        for (const auto& building : navigation.buildings()) {
            std::cout << "    Building: " << building.name << '\n';
            for (const auto& storey : building.storeys) {
                std::cout << "      Storey: " << storey.name << '\n';
                for (const auto& category : storey.categories)
                    std::cout << "        " << category.name << ": "
                              << category.features.size() << '\n';
            }
        }
        std::cout << "  non-null shapes: " << nonNull << '\n';
        for (const auto& [kind, count] : topologyCounts)
            std::cout << "  TopoDS " << kind << ": " << count << '\n';
        for (const auto& type : {std::string("IfcWall"), std::string("IfcSlab"),
                                 std::string("IfcDoor"), std::string("IfcWindow")}) {
            const auto it = representatives.find(type);
            check(it != representatives.end(), "representative IFC class");
            const auto& shape = it->second->shape();
            Bnd_Box box;
            BRepBndLib::Add(shape, box);
            double xmin, ymin, zmin, xmax, ymax, zmax;
            box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
            std::cout << "  representative " << type << " "
                      << it->second->ifcGlobalId().toStdString() << " "
                      << shapeKind(shape) << " bounds [" << xmin << ", " << ymin << ", "
                      << zmin << "] - [" << xmax << ", " << ymax << ", " << zmax << "]\n";
            check(it->second->placement().Form() == gp_Identity,
                  "ImportedFeature placement must be identity");
        }

        std::vector<const cad::import::IfcImportProduct*> mappedProducts;
        int deepestPlacement = 0;
        std::vector<const cad::import::IfcImportProduct*> deepestProducts;
        for (const auto& product : result.products) {
            if (product.representationTypes.contains("IfcMappedItem"))
                mappedProducts.push_back(&product);
            if (product.placementDepth > deepestPlacement) {
                deepestPlacement = product.placementDepth;
                deepestProducts.clear();
            }
            if (product.placementDepth == deepestPlacement)
                deepestProducts.push_back(&product);
        }
        check(mappedProducts.size() >= 2, "Duplex mapped products");
        auto importedShape = [&unavailableBody](const auto* product) {
            const auto feature = unavailableBody.findFeature(
                (QStringLiteral("ifc-") + product->globalId).toStdString());
            check(feature && !feature->shape().IsNull(), "Duplex product shape lookup");
            return feature->shape();
        };
        Bnd_Box mappedBoxA;
        Bnd_Box mappedBoxB;
        BRepBndLib::Add(importedShape(mappedProducts[0]), mappedBoxA);
        BRepBndLib::Add(importedShape(mappedProducts[1]), mappedBoxB);
        double ax, ay, az, ax2, ay2, az2, bx, by, bz, bx2, by2, bz2;
        mappedBoxA.Get(ax, ay, az, ax2, ay2, az2);
        mappedBoxB.Get(bx, by, bz, bx2, by2, bz2);
        check(std::abs((ax + ax2) - (bx + bx2)) > 1.0,
              "Duplex mapped instances collapsed to one placement");
        std::cout << "  mapped instances: " << mappedProducts[0]->globalId.toStdString()
                  << " center-x " << (ax + ax2) / 2.0 << ", "
                  << mappedProducts[1]->globalId.toStdString() << " center-x "
                  << (bx + bx2) / 2.0 << '\n';
        check(deepestPlacement >= 1, "Duplex placement depth");
        std::cout << "  deepest placement depth: " << deepestPlacement << '\n';
        for (size_t i = 0; i < std::min<size_t>(3, deepestProducts.size()); ++i)
            std::cout << "  deep placement sample: "
                      << deepestProducts[i]->globalId.toStdString() << '\n';

        QTemporaryDir roundTripDirectory;
        check(roundTripDirectory.isValid(), "IFC roundtrip directory");
        const auto temporaryIfc = roundTripDirectory.filePath("duplex.ifc");
        const auto sourceIfc = QStringLiteral(PARAMETRIC_CAD_SOURCE_DIR)
            + "/examples/ifc/Duplex_A_20110505.ifc";
        check(QFile::copy(sourceIfc, temporaryIfc), "copy Duplex IFC");
        cad::parametric::Body roundTripBody;
        QElapsedTimer saveTimer;
        saveTimer.start();
        const auto roundTripResult = importer.importIntoBody(temporaryIfc, roundTripBody);
        check(roundTripResult.statistics.importedCount == result.statistics.importedCount,
              "roundtrip import count");
        const auto pcadPath = roundTripDirectory.filePath("duplex.pcad");
        Document roundTripDocument;
        QString roundTripError;
        check(ProjectFile::save(pcadPath, roundTripDocument, roundTripBody, roundTripError),
              "save Duplex BRep");
        std::set<QString> sourceGlobalIds;
        std::map<QString, QString> sourceSpatialIds;
        for (const auto& feature : roundTripBody.features()) {
            const auto importedFeature = std::static_pointer_cast<cad::parametric::ImportedFeature>(feature);
            sourceGlobalIds.insert(importedFeature->ifcGlobalId());
            sourceSpatialIds.emplace(importedFeature->ifcGlobalId(),
                importedFeature->ifcBuildingGlobalId() + "|"
                + importedFeature->ifcStoreyGlobalId());
        }
        const auto saveMilliseconds = saveTimer.elapsed();
        const auto pcadSize = QFileInfo(pcadPath).size();
        check(QFile::remove(temporaryIfc), "remove source IFC");
        Document reloadedDocument;
        cad::parametric::Body reloadedBody;
        QElapsedTimer loadTimer;
        loadTimer.start();
        check(ProjectFile::load(pcadPath, reloadedDocument, reloadedBody, roundTripError),
              "reload Duplex BRep");
        const auto loadMilliseconds = loadTimer.elapsed();
        const auto reloadedBounds = bounds(reloadedBody);
        check(reloadedBody.features().size() == roundTripBody.features().size(),
              "roundtrip feature count");
        std::set<QString> loadedGlobalIds;
        std::map<QString, QString> loadedSpatialIds;
        for (const auto& feature : reloadedBody.features()) {
            const auto importedFeature = std::static_pointer_cast<cad::parametric::ImportedFeature>(feature);
            loadedGlobalIds.insert(importedFeature->ifcGlobalId());
            loadedSpatialIds.emplace(importedFeature->ifcGlobalId(),
                importedFeature->ifcBuildingGlobalId() + "|"
                + importedFeature->ifcStoreyGlobalId());
        }
        check(loadedGlobalIds == sourceGlobalIds, "roundtrip IFC GlobalIds");
        check(loadedSpatialIds == sourceSpatialIds, "roundtrip IFC spatial IDs");
        check(std::abs(reloadedBounds.xmin - importedBounds.xmin) < 1.0e-5
                  && std::abs(reloadedBounds.xmax - importedBounds.xmax) < 1.0e-5,
              "roundtrip bounds");
        std::cout << "  persistence: source bytes " << QFileInfo(sourceIfc).size()
                  << ", pcad bytes " << pcadSize
                  << ", save ms " << saveMilliseconds
                  << ", reload ms " << loadMilliseconds << '\n';
#else
        check(result.statistics.importedCount == 0 && result.statistics.failedCount > 0,
              "disabled IFC diagnostic");
#endif
        std::cout << "IFC/import persistence tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
