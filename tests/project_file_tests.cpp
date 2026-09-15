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
        check(loaded.features().size() == 2 && loadedBody.features().size() == 7, "feature count");
        const auto* loadedBox = dynamic_cast<const BoxFeature*>(loaded.features()[0].get());
        const auto* loadedCylinder = dynamic_cast<const CylinderFeature*>(loaded.features()[1].get());
        check(loadedBox && loadedBox->width() == 11 && loadedBox->depth() == 12 && loadedBox->height() == 13, "box dimensions");
        check(loadedCylinder && loadedCylinder->radius() == 7 && loadedCylinder->height() == 15, "cylinder dimensions");
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
            check(loaded.features().size() == 2 && loadedBody.findFeature("box-1") == editable,
                  "failed load preserves current model");
        };
        reject("{broken");
        auto bad = root;
        bad.insert("version", 999);
        reject(QJsonDocument(bad).toJson());
        bad = root;
        auto features = bad.value("features").toArray();
        auto primitive = features[0].toObject();
        primitive.insert("width", -1);
        features[0] = primitive;
        bad.insert("features", features);
        reject(QJsonDocument(bad).toJson());
        bad = root;
        auto history = bad.value("body").toArray();
        auto operation = history[2].toObject();
        operation.insert("left", "missing");
        history[2] = operation;
        bad.insert("body", history);
        reject(QJsonDocument(bad).toJson());
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
