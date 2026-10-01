#include "model/Feature.h"
#include "model/Document.h"
#include "model/Body.h"
#include "model/ProjectFile.h"
#include "operations/BoxFeature.h"
#include "operations/CylinderFeature.h"
#include "operations/ParametricFeatures.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
int main()
{
    try {
        using namespace cad::parametric;
        QTemporaryDir directory;
        check(directory.isValid(), "temporary directory");
        const auto path = directory.filePath("roundtrip.pcad");
        Document document;
        document.addFeature(std::make_unique<BoxFeature>(11, 12, 13));
        document.addFeature(std::make_unique<CylinderFeature>(7, 15));
        Body body;
        auto box = std::make_shared<BoxParametricFeature>("box-1", 20, 30, 40);
        box->setName("Коробка");
        auto cylinder = std::make_shared<CylinderParametricFeature>("cylinder-2", 4, 10);
        body.addFeature(box);
        body.addFeature(cylinder);
        body.addFeature(std::make_shared<BooleanFeature>("cut-3", box, cylinder, BooleanOperation::Cut));
        body.addFeature(std::make_shared<ConeFeature>("cone-4", 5, 2, 8));
        body.addFeature(std::make_shared<SphereFeature>("sphere-5", 6));
        body.addFeature(std::make_shared<TorusFeature>("torus-6", 8, 2));
        body.addFeature(std::make_shared<HexagonFeature>("hexagon-7", 10, 4));
        check(body.recompute(), "initial recompute");
        QString error;
        check(ProjectFile::save(path, document, body, error), "save");
        Document loaded;
        Body loadedBody;
        check(ProjectFile::load(path, loaded, loadedBody, error), "load");
        check(loaded.features().empty() && loadedBody.features().size() == 9, "feature count");
        const auto loadedBox = std::dynamic_pointer_cast<BoxParametricFeature>(
            loadedBody.findFeature("legacy-box-0"));
        const auto loadedCylinder = std::dynamic_pointer_cast<CylinderParametricFeature>(
            loadedBody.findFeature("legacy-cylinder-1"));
        check(loadedBox && loadedBox->width() == 11 && loadedBox->depth() == 12
            && loadedBox->height() == 13, "box dimensions");
        check(loadedCylinder && loadedCylinder->radius() == 7
            && loadedCylinder->height() == 15, "cylinder dimensions");
        GProp_GProps props;
        BRepGProp::VolumeProperties(loadedBox->shape(), props);
        check(std::abs(props.Mass() - 11*12*13) < 1e-6, "rebuilt volume");
        auto editable = std::dynamic_pointer_cast<BoxParametricFeature>(loadedBody.findFeature("box-1"));
        auto boolean = std::dynamic_pointer_cast<BooleanFeature>(loadedBody.findFeature("cut-3"));
        check(editable && editable->name() == "Коробка" && boolean && boolean->left() == editable
            && boolean->right() == loadedBody.findFeature("cylinder-2"), "identity and links");
        editable->setSize(50, 30, 40);
        loadedBody.markDirtyFrom(editable->id());
        check(loadedBody.recompute(), "edit after load");
        check(ProjectFile::save(path, loaded, loadedBody, error), "save edited model");
        QFile file(path);
        check(file.open(QIODevice::ReadOnly), "read saved file");
        const auto valid = file.readAll();
        file.close();
        const auto root = QJsonDocument::fromJson(valid).object();
        auto reject = [&](const QByteArray& bytes) {
            check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "write invalid file");
            check(file.write(bytes) == bytes.size(), "write bytes");
            file.close();
            check(!ProjectFile::load(path, loaded, loadedBody, error) && !error.isEmpty(), "reject bad project");
            check(loaded.features().empty() && loadedBody.findFeature("box-1"),
                  "failed load preserves current model");
        };
        reject("{broken");
        auto bad = root;
        bad.insert("version", 999);
        reject(QJsonDocument(bad).toJson());
        bad = root;
        auto features = bad.value("features").toArray();
        check(features.isEmpty(), "canonical saves do not write legacy features");
        auto legacyRoot = QJsonObject{
            {"format", "ParametricCAD"},
            {"version", 1},
            {"features", QJsonArray{
                QJsonObject{{"type", "Box"}, {"width", 2}, {"depth", 3}, {"height", 4}},
                QJsonObject{{"type", "Cylinder"}, {"radius", 5}, {"height", 6}}
            }},
            {"body", QJsonArray{}}
        };
        const auto legacyPath = directory.filePath("legacy.pcad");
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "write legacy file");
        check(file.fileName() == path, "test file is open");
        check(file.write(QJsonDocument(legacyRoot).toJson()) > 0, "write legacy JSON");
        file.close();
        check(ProjectFile::load(path, loaded, loadedBody, error), "load legacy file");
        check(loaded.features().empty() && loadedBody.features().size() == 2,
              "legacy features convert into Body");
        check(loadedBody.findFeature("legacy-box-0")
                  && loadedBody.findFeature("legacy-cylinder-1"),
              "legacy features receive stable IDs");
        check(ProjectFile::save(legacyPath, loaded, loadedBody, error), "save migrated legacy file");
        file.setFileName(legacyPath);
        check(file.open(QIODevice::ReadOnly), "read migrated legacy file");
        const auto migratedRoot = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        check(migratedRoot.value("features").toArray().isEmpty()
                  && migratedRoot.value("body").toArray().size() == 2,
              "migrated save is canonical");

        auto primitive = legacyRoot.value("features").toArray()[0].toObject();
        primitive.insert("width", -1);
        auto invalidLegacy = legacyRoot.value("features").toArray();
        invalidLegacy[0] = primitive;
        legacyRoot.insert("features", invalidLegacy);
        file.setFileName(path);
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "write invalid legacy file");
        check(file.write(QJsonDocument(legacyRoot).toJson()) > 0, "write invalid legacy JSON");
        file.close();
        check(!ProjectFile::load(path, loaded, loadedBody, error), "reject invalid legacy feature");
        check(error.contains("positive"), "invalid legacy dimensions explain the error");

        file.setFileName(path);
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "restore canonical file");
        check(file.write(valid) == valid.size(), "restore canonical JSON");
        file.close();
        check(ProjectFile::load(path, loaded, loadedBody, error), "reload canonical model");

        bad = root;
        auto history = bad.value("body").toArray();
        auto operation = history[4].toObject();
        operation.insert("left", "missing");
        history[4] = operation;
        bad.insert("body", history);
        reject(QJsonDocument(bad).toJson());
        check(error.contains("reference"), "broken dependency error explains the reference problem");
        bad = root;
        history = bad.value("body").toArray();
        history.append(history[0]);
        bad.insert("body", history);
        reject(QJsonDocument(bad).toJson());
        check(!ProjectFile::save(directory.filePath("missing/file.pcad"), loaded, loadedBody, error), "write failure");
        Document empty;
        Body emptyBody;
        check(ProjectFile::save(path, empty, emptyBody, error), "save empty");
        check(ProjectFile::load(path, loaded, loadedBody, error), "load empty");
        check(loaded.features().empty() && loadedBody.features().empty(), "empty replaces model");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
