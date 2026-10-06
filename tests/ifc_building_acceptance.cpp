#include "application/IfcImporter.h"
#include "application/BimNavigationModel.h"
#include "model/Body.h"
#include "model/Document.h"
#include "model/Feature.h"
#include "model/ProjectFile.h"
#include "operations/ImportedFeature.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <vector>

#ifndef PARAMETRIC_CAD_SOURCE_DIR
#define PARAMETRIC_CAD_SOURCE_DIR "."
#endif

namespace {
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

struct Bounds { double xmin, ymin, zmin, xmax, ymax, zmax; };

Bounds bounds(const TopoDS_Shape& shape)
{
    check(!shape.IsNull(), "Cannot bound null shape");
    try {
        Bnd_Box box;
        BRepBndLib::Add(shape, box);
        check(!box.IsVoid(), "Cannot bound void shape");
        Bounds result{};
        box.Get(result.xmin, result.ymin, result.zmin,
                result.xmax, result.ymax, result.zmax);
        return result;
    } catch (const Standard_Failure& failure) {
        throw std::runtime_error(failure.GetMessageString());
    }
}

const char* shapeKind(const TopoDS_Shape& shape)
{
    switch (shape.ShapeType()) {
    case TopAbs_SOLID: return "solid";
    case TopAbs_COMPOUND: return "compound";
    case TopAbs_SHELL: return "shell";
    case TopAbs_FACE: return "face";
    default: return "other";
    }
}

Bounds bounds(const cad::parametric::Body& body)
{
    Bnd_Box box;
    for (const auto& feature : body.features()) {
        check(!feature->shape().IsNull(), "Building contains a null imported shape");
        BRepBndLib::Add(feature->shape(), box);
    }
    Bounds result{};
    box.Get(result.xmin, result.ymin, result.zmin,
            result.xmax, result.ymax, result.zmax);
    return result;
}
}

int main()
{
    try {
        const auto sourcePath = QStringLiteral(PARAMETRIC_CAD_SOURCE_DIR)
            + "/examples/ifc/BuildingBIMModel.ifc";
        QTemporaryDir directory;
        check(directory.isValid(), "temporary directory");
        const auto temporaryIfc = directory.filePath("building.ifc");
        check(QFile::copy(sourcePath, temporaryIfc), "copy Building IFC");

        cad::parametric::Body body;
        cad::application::IfcImporter importer;
        QElapsedTimer timer;
        timer.start();
        const auto result = importer.importIntoBody(temporaryIfc, body);
        const auto importMilliseconds = timer.elapsed();
        check(result.statistics.schema == "IFC4", "Building schema");
        check(result.statistics.importedCount > 1000, "Building imported count");
        check(result.statistics.failedCount == 0, "Building geometry failures");
        const auto modelBounds = bounds(body);
        cad::application::BimNavigationModel navigation;
        navigation.rebuild(body);
        check(!navigation.buildings().empty(), "Building BIM hierarchy");

        std::map<QString, int> representations;
        std::vector<const cad::import::IfcImportProduct*> mapped;
        std::vector<const cad::import::IfcImportProduct*> polygonal;
        for (const auto& product : result.products) {
            for (const auto& representation : product.representationTypes) {
                ++representations[representation];
                if (representation == "IfcMappedItem") mapped.push_back(&product);
                if (representation == "IfcPolygonalFaceSet") polygonal.push_back(&product);
            }
        }
        check(mapped.size() >= 3, "Building mapped products were not reported");
        check(polygonal.size() >= 3, "Building polygonal products were not reported");

        const auto importedShape = [&body](const cad::import::IfcImportProduct* product) {
            const auto feature = body.findFeature((QStringLiteral("ifc-") + product->globalId)
                                                      .toStdString());
            check(feature && !feature->shape().IsNull(), "Imported product shape lookup");
            return feature->shape();
        };
        const auto mappedFirst = bounds(importedShape(mapped[0]));
        const auto mappedSecond = bounds(importedShape(mapped[1]));
        const auto mappedFirstCenter = (mappedFirst.xmin + mappedFirst.xmax) / 2.0;
        const auto mappedSecondCenter = (mappedSecond.xmin + mappedSecond.xmax) / 2.0;
        check(std::abs(mappedFirstCenter - mappedSecondCenter) > 1.0,
              "Mapped instances collapsed to one placement");

        std::cout << "BuildingBIMModel IFC acceptance\n"
                  << "  schema: " << result.statistics.schema.toStdString() << '\n'
                  << "  considered: " << result.statistics.consideredCount << '\n'
                  << "  with geometry: " << result.statistics.productsWithGeometry << '\n'
                  << "  imported: " << result.statistics.importedCount << '\n'
                  << "  skipped: " << result.statistics.skippedCount << '\n'
                  << "  failed: " << result.statistics.failedCount << '\n'
                  << "  import ms: " << importMilliseconds << '\n'
                  << "  bounds mm: [" << modelBounds.xmin << ", " << modelBounds.ymin
                  << ", " << modelBounds.zmin << "] - [" << modelBounds.xmax << ", "
                  << modelBounds.ymax << ", " << modelBounds.zmax << "]\n"
                  << "  mapped products: " << mapped.size() << '\n'
                  << "  polygonal products: " << polygonal.size() << '\n'
                  << "  mapped sample 1: " << mapped[0]->globalId.toStdString()
                  << " center-x " << mappedFirstCenter << '\n'
                  << "  mapped sample 2: " << mapped[1]->globalId.toStdString()
                  << " center-x " << mappedSecondCenter << '\n';
        for (size_t i = 0; i < std::min<size_t>(3, polygonal.size()); ++i) {
            const auto polygonalShape = importedShape(polygonal[i]);
            const auto polygonalBounds = bounds(polygonalShape);
            std::cout << "  polygonal sample " << (i + 1) << ": "
                      << polygonal[i]->globalId.toStdString() << ' '
                      << polygonal[i]->entityType.toStdString() << ' '
                      << shapeKind(polygonalShape) << " bounds ["
                      << polygonalBounds.xmin << ", " << polygonalBounds.ymin << ", "
                      << polygonalBounds.zmin << "] - [" << polygonalBounds.xmax << ", "
                      << polygonalBounds.ymax << ", " << polygonalBounds.zmax << "]\n";
            check(!polygonalShape.IsNull(), "PolygonalFaceSet geometry is null");
        }
        for (const auto& [type, count] : result.statistics.importedByEntity)
            std::cout << "  imported " << type.toStdString() << ": " << count << '\n';
        for (const auto& [type, count] : representations)
            std::cout << "  representation " << type.toStdString() << ": " << count << '\n';
        for (const auto& diagnostic : result.diagnostics)
            std::cout << "  diagnostic " << diagnostic.entityType.toStdString()
                      << ": " << diagnostic.message.toStdString() << '\n';

        std::size_t storeyCount = 0;
        std::size_t categoryCount = 0;
        std::cout << "  BIM buildings: " << navigation.buildings().size() << '\n';
        for (const auto& building : navigation.buildings()) {
            std::cout << "    Building: " << building.name << '\n';
            storeyCount += building.storeys.size();
            for (const auto& storey : building.storeys) {
                categoryCount += storey.categories.size();
                std::cout << "      Storey: " << storey.name << " ("
                          << storey.categories.size() << " categories)\n";
            }
        }
        check(storeyCount > 0 && categoryCount > 0, "Building BIM storeys/categories");

        check(QFile::remove(temporaryIfc), "remove source IFC");
        const auto pcadPath = directory.filePath("building.pcad");
        Document document;
        QString error;
        QElapsedTimer saveTimer;
        saveTimer.start();
        check(ProjectFile::save(pcadPath, document, body, error), "save Building pcad");
        const auto saveMilliseconds = saveTimer.elapsed();
        Document loadedDocument;
        cad::parametric::Body loadedBody;
        QElapsedTimer loadTimer;
        loadTimer.start();
        check(ProjectFile::load(pcadPath, loadedDocument, loadedBody, error),
              "load Building pcad");
        const auto loadMilliseconds = loadTimer.elapsed();
        const auto reloadedBounds = bounds(loadedBody);
        check(loadedBody.features().size() == body.features().size(),
              "Building persistence feature count");
        check(std::abs(reloadedBounds.xmax - modelBounds.xmax) < 1.0e-4
                  && std::abs(reloadedBounds.ymax - modelBounds.ymax) < 1.0e-4
                  && std::abs(reloadedBounds.zmax - modelBounds.zmax) < 1.0e-4,
              "Building persistence bounds");
        std::cout << "  source bytes: " << QFileInfo(sourcePath).size() << '\n'
                  << "  pcad bytes: " << QFileInfo(pcadPath).size() << '\n'
                  << "  save ms: " << saveMilliseconds << '\n'
                  << "  reload ms: " << loadMilliseconds << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
