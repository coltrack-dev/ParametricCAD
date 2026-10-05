#include "model/Body.h"
#include "model/Document.h"
#include "model/Feature.h"
#include "model/ProjectArchive.h"
#include "model/ProjectFile.h"
#include "model/ShapePayload.h"
#include "operations/ImportedFeature.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

QJsonObject importedManifest(const QString& entry)
{
    return QJsonObject{
        {"format", "ParametricCAD"}, {"version", 1},
        {"storage", "archive"}, {"containerVersion", 1},
        {"features", QJsonArray{}},
        {"body", QJsonArray{QJsonObject{
            {"id", "imported"}, {"name", "Imported"}, {"type", "IfcImported"},
            {"visible", true},
            {"placement", QJsonArray{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}},
            {"sourceFormat", "IFC"}, {"sourceFile", "source.ifc"},
            {"ifcGlobalId", "global"}, {"ifcEntityType", "IfcWall"},
            {"ifcDescription", ""}, {"ifcBuilding", "Building"},
            {"ifcStorey", "Storey"},
            {"shapePayload", QJsonObject{{"storage", "archive"}, {"path", entry}}}
        }}}
    };
}

void writePlain(const QString& path, const QJsonObject& object)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "open plain project");
    const auto bytes = QJsonDocument(object).toJson();
    check(file.write(bytes) == bytes.size(), "write plain project");
}

bool loadFails(const QString& path, QString& error)
{
    Document document;
    cad::parametric::Body body;
    return !ProjectFile::load(path, document, body, error) && !error.isEmpty();
}
}

int main()
{
    try {
        QTemporaryDir directory;
        check(directory.isValid(), "temporary directory");
        const auto shape = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
        cad::parametric::Body body;
        body.addFeature(std::make_shared<cad::parametric::ImportedFeature>(
            "imported", "Imported", shape, "IFC", "source.ifc", "global",
            "IfcWall", "", "Building", "Storey"));
        const auto path = directory.filePath("container.pcad");
        Document document;
        QString error;
        check(ProjectFile::save(path, document, body, error), error.toStdString().c_str());
        check(cad::persistence::isProjectArchive(path), "new save is not an archive");

        cad::persistence::ProjectArchiveReader reader(path);
        check(reader.open(error), error.toStdString().c_str());
        QByteArray manifestBytes;
        check(reader.readEntry("manifest.json", manifestBytes, error), error.toStdString().c_str());
        const auto manifest = QJsonDocument::fromJson(manifestBytes).object();
        const auto feature = manifest.value("body").toArray().first().toObject();
        check(!feature.contains("geometryPayload") && feature.value("shapePayload").isObject(),
              "geometry leaked into manifest");
        const auto entry = feature.value("shapePayload").toObject().value("path").toString();
        QByteArray rawShape;
        check(reader.readEntry(entry, rawShape, error), error.toStdString().c_str());
        check(cad::persistence::decodeBRepRaw(rawShape).ShapeType() == shape.ShapeType(),
              "raw BRep archive roundtrip");

        Document loadedDocument;
        cad::parametric::Body loadedBody;
        check(ProjectFile::load(path, loadedDocument, loadedBody, error),
              error.toStdString().c_str());
        check(loadedBody.features().size() == 1
                  && loadedBody.features().front()->id() == "imported",
              "container feature roundtrip");

        const auto legacyPath = directory.filePath("legacy-imported.pcad");
        auto legacy = importedManifest("unused.brep");
        auto legacyFeature = legacy.value("body").toArray().first().toObject();
        legacyFeature.remove("shapePayload");
        legacyFeature.insert("geometryPayload",
            QString::fromLatin1(cad::persistence::encodeBRep(shape)));
        legacy["body"] = QJsonArray{legacyFeature};
        writePlain(legacyPath, legacy);
        loadedBody = {};
        check(ProjectFile::load(legacyPath, loadedDocument, loadedBody, error),
              error.toStdString().c_str());
        check(loadedBody.features().size() == 1, "legacy embedded BRep load");

        const auto missingManifestPath = directory.filePath("missing-manifest.pcad");
        cad::persistence::ProjectArchiveWriter archive;
        check(archive.open(missingManifestPath, error), error.toStdString().c_str());
        check(archive.addEntry("not-manifest", "x", error), error.toStdString().c_str());
        check(archive.close(error), error.toStdString().c_str());
        check(cad::persistence::isProjectArchive(missingManifestPath), "missing manifest is not zip");
        cad::persistence::ProjectArchiveReader missingReader(missingManifestPath);
        check(missingReader.open(error), error.toStdString().c_str());
        QByteArray missingManifest;
        check(!missingReader.readEntry("manifest.json", missingManifest, error),
              "reader accepted missing manifest");
        if (!loadFails(missingManifestPath, error))
            throw std::runtime_error("missing manifest is accepted: " + error.toStdString());

        const auto missingShapePath = directory.filePath("missing-shape.pcad");
        check(archive.open(missingShapePath, error), error.toStdString().c_str());
        check(archive.addEntry("manifest.json",
                               QJsonDocument(importedManifest("geometry/missing.brep")).toJson(),
                               error), error.toStdString().c_str());
        check(archive.close(error), error.toStdString().c_str());
        check(loadFails(missingShapePath, error), "missing geometry is accepted");

        const auto malformedPath = directory.filePath("malformed-shape.pcad");
        check(archive.open(malformedPath, error), error.toStdString().c_str());
        check(archive.addEntry("geometry/bad.brep", "not a brep", error),
              error.toStdString().c_str());
        check(archive.addEntry("manifest.json",
                               QJsonDocument(importedManifest("geometry/bad.brep")).toJson(),
                               error), error.toStdString().c_str());
        check(archive.close(error), error.toStdString().c_str());
        check(loadFails(malformedPath, error), "malformed geometry is accepted");

        const auto invalidPath = directory.filePath("invalid.pcad");
        writePlain(invalidPath, QJsonObject{{"not", "an archive"}});
        check(loadFails(invalidPath, error), "invalid archive is accepted");
        check(!archive.open(directory.filePath("traversal.pcad"), error)
                  || !archive.addEntry("../outside", "x", error),
              "traversal entry accepted");
        std::cout << "Project archive tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
