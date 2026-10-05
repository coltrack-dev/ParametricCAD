#include "model/Feature.h"
#include "model/Document.h"
#include "model/Body.h"
#include "model/ProjectFile.h"
#include "model/ProjectArchive.h"
#include "operations/BoxFeature.h"
#include "operations/CylinderFeature.h"
#include "operations/ParametricFeatures.h"

#include <QtTest/QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace cad::parametric;

namespace {
double area(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(shape, properties);
    return properties.Mass();
}
}

class SketchFaceTests final : public QObject
{
    Q_OBJECT
private slots:
    void invalidDimensions_data()
    {
        QTest::addColumn<double>("width");
        QTest::addColumn<double>("height");
        QTest::newRow("zero width") << 0.0 << 3.0;
        QTest::newRow("negative width") << -2.0 << 3.0;
        QTest::newRow("zero height") << 2.0 << 0.0;
        QTest::newRow("negative height") << 2.0 << -3.0;
        QTest::newRow("nan width") << std::numeric_limits<double>::quiet_NaN() << 3.0;
        QTest::newRow("infinite height") << 2.0 << std::numeric_limits<double>::infinity();
    }

    void invalidDimensions()
    {
        QFETCH(double, width);
        QFETCH(double, height);
        auto sketch = std::make_shared<SketchFeature>("sketch", width, height);
        QVERIFY(!sketch->recompute());
        QCOMPARE(sketch->state(), FeatureState::Failed);
        QVERIFY(sketch->shape().IsNull());
        QVERIFY(!sketch->error().empty());
        FaceFeature face("face", sketch);
        QVERIFY(!face.recompute());
        QVERIFY(!face.error().empty());
    }

    void geometryAndRebuild()
    {
        Body body;
        auto sketch = std::make_shared<SketchFeature>("sketch", 10, 6);
        auto face = std::make_shared<FaceFeature>("face", sketch);
        body.addFeature(sketch);
        body.addFeature(face);
        QVERIFY2(body.recompute(), body.lastError().c_str());
        QVERIFY(!sketch->shape().IsNull());
        QVERIFY(!face->shape().IsNull());
        QCOMPARE(sketch->shape().ShapeType(), TopAbs_WIRE);
        QVERIFY(BRep_Tool::IsClosed(TopoDS::Wire(sketch->shape())));
        QVERIFY(BRepCheck_Analyzer(sketch->shape()).IsValid());
        QCOMPARE(face->shape().ShapeType(), TopAbs_FACE);
        QVERIFY(BRepCheck_Analyzer(face->shape()).IsValid());
        QVERIFY(std::abs(area(face->shape()) - 60) < 1e-8);
        QCOMPARE(face->sourceFeatureId(), sketch->id());
        QCOMPARE(face->dependencies().size(), std::size_t{1});
        QCOMPARE(face->dependencies().front().lock().get(), sketch.get());
        const auto oldWire = sketch->shape();
        const auto oldFace = face->shape();
        sketch->setSize(20, 6);
        body.markDirtyFrom(sketch->id());
        QVERIFY(body.recompute());
        QVERIFY(!sketch->shape().IsSame(oldWire));
        QVERIFY(!face->shape().IsSame(oldFace));
        QVERIFY(std::abs(area(face->shape()) - 120) < 1e-8);
        sketch->setSize(20, 8);
        body.markDirtyFrom(sketch->id());
        QVERIFY(body.recompute());
        QVERIFY(std::abs(area(face->shape()) - 160) < 1e-8);
        QCOMPARE(body.findFeature("face").get(), face.get());
        QVERIFY_EXCEPTION_THROWN(body.addFeature(std::make_shared<SketchFeature>("sketch", 1, 1)), std::invalid_argument);
    }

    void invalidSource()
    {
        QVERIFY_EXCEPTION_THROWN(FaceFeature("missing", {}), std::invalid_argument);
        auto box = std::make_shared<BoxParametricFeature>("box", 1, 2, 3);
        QVERIFY_EXCEPTION_THROWN(FaceFeature("wrong-type", box), std::invalid_argument);
        auto sketch = std::make_shared<SketchFeature>("sketch", 10, 6);
        FaceFeature face("face", sketch);
        QVERIFY(!face.recompute());
        QVERIFY(face.error().find("rebuilt") != std::string::npos);
        QVERIFY(sketch->recompute());
        QVERIFY(face.recompute());
        sketch->setSize(0, 6);
        QVERIFY(!sketch->recompute());
        QVERIFY(!face.recompute());
        QVERIFY(face.shape().IsNull());
        sketch->setSize(5, 6);
        QVERIFY(sketch->recompute());
        QVERIFY(face.recompute());
        QVERIFY(std::abs(area(face.shape()) - 30) < 1e-8);
        sketch.reset();
        QVERIFY(!face.recompute());
        QVERIFY(face.error().find("no longer exists") != std::string::npos);
        QVERIFY(face.shape().IsNull());
    }

    void roundTripAndValidation()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("sketch-face.pcad");
        Document document;
        document.addFeature(std::make_unique<BoxFeature>(2, 3, 4));
        document.addFeature(std::make_unique<CylinderFeature>(2, 5));
        Body body;
        auto sketch = std::make_shared<SketchFeature>("sketch-001", 10, 6);
        sketch->setName("Sketch001");
        auto face = std::make_shared<FaceFeature>("face-001", sketch);
        face->setName("Face001");
        body.addFeature(sketch);
        body.addFeature(face);
        QVERIFY(body.recompute());
        QString error;
        QVERIFY2(ProjectFile::save(path, document, body, error), qPrintable(error));
        Document loaded;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loaded, loadedBody, error), qPrintable(error));
        QVERIFY(loaded.features().empty());
        QCOMPARE(loadedBody.features().size(), std::size_t{4});
        const auto loadedBox = loadedBody.findFeature("legacy-box-0");
        const auto loadedCylinder = loadedBody.findFeature("legacy-cylinder-1");
        QVERIFY(loadedBox && std::string(loadedBox->typeId()) == "Box");
        QVERIFY(loadedCylinder && std::string(loadedCylinder->typeId()) == "Cylinder");
        const auto loadedSketch = loadedBody.findFeature("sketch-001");
        const auto loadedFace = loadedBody.findFeature("face-001");
        QVERIFY(loadedSketch && loadedFace);
        QCOMPARE(loadedSketch->role(), FeatureRole::Sketch);
        QCOMPARE(loadedFace->role(), FeatureRole::Face);
        QCOMPARE(loadedSketch->name(), std::string("Sketch001"));
        QCOMPARE(loadedFace->name(), std::string("Face001"));
        QCOMPARE(std::get<double>(loadedSketch->properties()[0].value), 10.0);
        QCOMPARE(std::get<double>(loadedSketch->properties()[1].value), 6.0);
        QCOMPARE(loadedFace->dependencies().front().lock().get(), loadedSketch.get());
        QVERIFY(std::abs(area(loadedFace->shape()) - 60) < 1e-8);
        QVERIFY(loadedSketch->setNumericProperty("width", 15));
        QVERIFY(loadedSketch->setNumericProperty("height", 8));
        loadedBody.markDirtyFrom(loadedSketch->id());
        QVERIFY(loadedBody.recompute());
        QVERIFY(std::abs(area(loadedFace->shape()) - 120) < 1e-8);
        QVERIFY(ProjectFile::save(path, loaded, loadedBody, error));

        cad::persistence::ProjectArchiveReader archive(path);
        QString archiveError;
        QVERIFY(archive.open(archiveError));
        QByteArray validBytes;
        QVERIFY(archive.readEntry("manifest.json", validBytes, archiveError));
        QFile file(path);
        const auto root = QJsonDocument::fromJson(validBytes).object();
        const auto history = root.value("body").toArray();
        QCOMPARE(history[3].toObject().value("sourceFeatureId").toString(), QString("sketch-001"));
        for (const auto& sourceId : {QString("missing"), QString("face-001")}) {
            auto bad = root;
            auto entries = history;
            auto record = entries[3].toObject();
            record.insert("sourceFeatureId", sourceId);
            entries[3] = record;
            bad.insert("body", entries);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            const auto bytes = QJsonDocument(bad).toJson();
            QCOMPARE(file.write(bytes), qint64(bytes.size()));
            file.close();
            QVERIFY(!ProjectFile::load(path, loaded, loadedBody, error));
            QVERIFY2(error.contains(sourceId) && error.contains("face-001"), qPrintable(error));
            QCOMPARE(loadedBody.findFeature("sketch-001").get(), loadedSketch.get());
            QVERIFY(loaded.features().empty());
        }
        Body missingSource;
        QVERIFY_EXCEPTION_THROWN(missingSource.addFeature(loadedFace), std::invalid_argument);
    }
};

QTEST_APPLESS_MAIN(SketchFaceTests)
#include "sketch_face_tests.moc"
