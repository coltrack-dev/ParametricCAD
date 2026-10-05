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
#include <QDir>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <gp_Trsf.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
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
        auto cone = std::make_shared<ConeFeature>("cone-4", 5, 2, 8);
        gp_Trsf placement;
        placement.SetTranslation(gp_Vec(5.0, 6.0, 7.0));
        cone->setPlacement(placement);
        body.addFeature(cone);
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
        const auto loadedBox = loadedBody.findFeature("legacy-box-0");
        const auto loadedCylinder = loadedBody.findFeature("legacy-cylinder-1");
        check(loadedBox && std::string(loadedBox->typeId()) == "Box"
            && std::get<double>(loadedBox->properties()[0].value) == 11
            && std::get<double>(loadedBox->properties()[1].value) == 12
            && std::get<double>(loadedBox->properties()[2].value) == 13, "box dimensions");
        check(loadedCylinder && std::string(loadedCylinder->typeId()) == "Cylinder"
            && std::get<double>(loadedCylinder->properties()[0].value) == 7
            && std::get<double>(loadedCylinder->properties()[1].value) == 15, "cylinder dimensions");
        GProp_GProps props;
        BRepGProp::VolumeProperties(loadedBox->shape(), props);
        check(std::abs(props.Mass() - 11*12*13) < 1e-6, "rebuilt volume");
        auto editable = loadedBody.findFeature("box-1");
        auto boolean = loadedBody.findFeature("cut-3");
        check(editable && std::string(editable->typeId()) == "Box" && editable->name() == "Коробка"
            && boolean && boolean->dependencies().front().lock() == editable
            && boolean->dependencies().back().lock() == loadedBody.findFeature("cylinder-2"), "identity and links");
        const auto loadedCone = loadedBody.findFeature("cone-4");
        check(loadedCone && std::abs(loadedCone->placement().TranslationPart().X() - 5.0) < 1e-9
                  && std::abs(loadedCone->placement().TranslationPart().Y() - 6.0) < 1e-9
                  && std::abs(loadedCone->placement().TranslationPart().Z() - 7.0) < 1e-9,
              "placement roundtrip");

        auto rotated = std::make_shared<BoxParametricFeature>("rotated-box", 2, 3, 4);
        const double angle = 35.0 * std::acos(-1.0) / 180.0;
        gp_Trsf rotatedPlacement;
        rotatedPlacement.SetRotation(
            gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)), angle);
        rotatedPlacement.SetTranslationPart(gp_Vec(11.0, -4.0, 7.0));
        rotated->setPlacement(rotatedPlacement);
        body.addFeature(rotated);
        check(body.recompute(), "rotated placement recompute");
        const auto rotatedPath = directory.filePath("rotated-placement.pcad");
        check(ProjectFile::save(rotatedPath, document, body, error), "rotated placement save");
        QFile roundedPlacementFile(rotatedPath);
        check(roundedPlacementFile.open(QIODevice::ReadOnly), "read rounded placement file");
        const auto roundedJson = QJsonDocument::fromJson(roundedPlacementFile.readAll());
        roundedPlacementFile.close();
        auto roundedRoot = roundedJson.object();
        auto roundedBody = roundedRoot.value("body").toArray();
        for (int index = 0; index < roundedBody.size(); ++index) {
            auto feature = roundedBody.at(index).toObject();
            if (feature.value("id").toString() != "rotated-box") continue;
            auto placement = feature.value("placement").toArray();
            for (int value = 0; value < placement.size(); ++value) {
                placement[value] = std::round(placement.at(value).toDouble() * 1.0e9) / 1.0e9;
            }
            feature.insert("placement", placement);
            roundedBody[index] = feature;
        }
        roundedRoot.insert("body", roundedBody);
        check(roundedPlacementFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
              "write rounded placement file");
        const auto roundedData = QJsonDocument(roundedRoot).toJson();
        check(roundedPlacementFile.write(roundedData) == roundedData.size(),
              "write rounded placement data");
        roundedPlacementFile.close();
        Document rotatedDocument;
        Body rotatedBody;
        check(ProjectFile::load(rotatedPath, rotatedDocument, rotatedBody, error),
              "rotated placement load");
        check(rotatedBody.recompute(), "rotated placement recompute after load");
        const auto loadedRotated = rotatedBody.findFeature("rotated-box");
        check(loadedRotated && std::abs(loadedRotated->placement().ScaleFactor() - 1.0) < 1e-12,
              "rotated placement scale");
        const auto& loadedMatrix = loadedRotated->placement();
        check(std::abs(loadedMatrix.Value(1, 1) - std::cos(angle)) < 1e-8
                  && std::abs(loadedMatrix.Value(1, 2) + std::sin(angle)) < 1e-8
                  && std::abs(loadedMatrix.Value(2, 1) - std::sin(angle)) < 1e-8
                  && std::abs(loadedMatrix.Value(2, 2) - std::cos(angle)) < 1e-8
                  && std::abs(loadedMatrix.TranslationPart().X() - 11.0) < 1e-12
                  && std::abs(loadedMatrix.TranslationPart().Y() + 4.0) < 1e-12
                  && std::abs(loadedMatrix.TranslationPart().Z() - 7.0) < 1e-12,
              "rotated placement roundtrip");
        editable->setNumericProperty("width", 50);
        editable->setNumericProperty("depth", 30);
        editable->setNumericProperty("height", 40);
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

        const auto largePath = directory.filePath("large-box-project.pcad");
        QJsonArray largeBody;
        constexpr int largeFeatureCount = 489;
        for (int index = 0; index < largeFeatureCount; ++index) {
            largeBody.append(QJsonObject{
                {"id", QString("large-box-%1").arg(index)},
                {"name", QString("Box %1").arg(index)},
                {"type", "Box"},
                {"width", 10.0},
                {"depth", 20.0},
                {"height", 30.0},
                {"placement", QJsonArray{1.0, 0.0, 0.0, index * 40.0,
                    0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0}},
                {"visible", true}
            });
        }
        file.setFileName(largePath);
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "write large project");
        const auto largeData = QJsonDocument(QJsonObject{
            {"format", "ParametricCAD"}, {"version", 1},
            {"features", QJsonArray{}}, {"body", largeBody}
        }).toJson();
        check(file.write(largeData) == largeData.size(), "write large project data");
        file.close();
        Document largeDocument;
        Body largeLoadedBody;
        ProjectLoadMetrics largeMetrics;
        int lastProgress = -1;
        const auto largeLoaded = ProjectFile::load(largePath, largeDocument, largeLoadedBody, error,
            [&lastProgress](const int current, const int total) {
                check(total == largeFeatureCount, "large project progress total");
                check(current >= lastProgress, "large project progress order");
                lastProgress = current;
            }, &largeMetrics);
        check(largeLoaded, "load large project");
        check(lastProgress == largeFeatureCount
                  && largeMetrics.featureCount == largeFeatureCount
                  && largeLoadedBody.features().size() == largeFeatureCount,
              "large project regression");
        const auto demoPath = QDir(QStringLiteral(PARAMETRIC_CAD_SOURCE_DIR))
            .filePath("examples/demo_all_operations.pcad");
        check(QFile::exists(demoPath), "demo all operations project exists");
        Document demoDocument;
        Body demoBody;
        if (!ProjectFile::load(demoPath, demoDocument, demoBody, error))
            throw std::runtime_error("load all operations demo: " + error.toStdString());
        const std::vector<std::string> demoTypes{
            "Extrude", "Pocket", "PushPull", "Revolve", "Boolean",
            "Fillet", "Chamfer", "Shell", "Offset", "Loft", "Sweep"};
        for (const auto& type : demoTypes) {
            bool found = false;
            for (const auto& feature : demoBody.features()) {
                if (feature->typeId() == type) {
                    found = true;
                    break;
                }
            }
            check(found, "all operations demo is missing a feature type");
        }
        const auto demoRoundtripPath = directory.filePath("demo-all-operations-roundtrip.pcad");
        check(ProjectFile::save(demoRoundtripPath, demoDocument, demoBody, error),
              "save all operations demo");
        Document demoRoundtripDocument;
        Body demoRoundtripBody;
        check(ProjectFile::load(demoRoundtripPath, demoRoundtripDocument,
            demoRoundtripBody, error), "reload all operations demo");
        check(demoRoundtripBody.features().size() == demoBody.features().size(),
              "all operations demo roundtrip feature count");
        const auto housePath = QDir::current().filePath("../../examples/house.pcad");
        if (QFile::exists(housePath)) {
            Document houseDocument;
            Body houseBody;
            ProjectLoadMetrics houseMetrics;
            check(ProjectFile::load(housePath, houseDocument, houseBody, error,
                {}, &houseMetrics), "load house project");
            check(houseMetrics.featureCount == static_cast<int>(houseBody.features().size()),
                "house project feature count");
        }
        file.setFileName(path);

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
