#include "application/IfcImporter.h"
#include "model/Body.h"
#include "model/Document.h"
#include "model/Feature.h"
#include "model/ProjectFile.h"
#include "model/ShapePayload.h"
#include "operations/ImportedFeature.h"

#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
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
