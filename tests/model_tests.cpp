#include "model/Feature.h"
#include "model/Document.h"
#include "model/Body.h"
#include "model/ProjectFile.h"
#include "operations/BoxFeature.h"
#include "operations/BasicFeatures.h"
#include "operations/ParametricFeatures.h"

#include <QtTest/QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <limits>
#include <stdexcept>

using cad::modeling::BasicFeatures;
using namespace cad::parametric;

namespace {
double volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

// Test fixture isolates the vector-based ExtrudeFeature API.
// Production Sketch/Face coverage is in sketch_face_tests.
class ProfileFixture final : public ParametricFeature
{
public:
    ProfileFixture() : ParametricFeature("profile", "Test profile") {}
    void setWidth(double width) { width_ = width; markDirty(); }
private:
    TopoDS_Shape build() const override
    {
        return BasicFeatures::face(BasicFeatures::rectangleWire(width_, 3));
    }
    double width_{2};
};
}

class ModelTests final : public QObject
{
    Q_OBJECT
private slots:
    void rectangleValidation_data()
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

    void rectangleValidation()
    {
        QFETCH(double, width);
        QFETCH(double, height);
        QVERIFY_EXCEPTION_THROWN(BasicFeatures::rectangleWire(width, height), std::invalid_argument);
    }

    void rectangleAndFaceGeometry()
    {
        // Kernel checks complement the production feature tests in sketch_face_tests.
        for (const auto dimensions : {std::pair{2.0, 3.0}, {4.0, 3.0}, {4.0, 5.0}}) {
            const auto wire = BasicFeatures::rectangleWire(dimensions.first, dimensions.second);
            QVERIFY(BRep_Tool::IsClosed(wire));
            QVERIFY(BRepCheck_Analyzer(wire).IsValid());
            const auto face = BasicFeatures::face(wire);
            QCOMPARE(face.ShapeType(), TopAbs_FACE);
            QVERIFY(BRepCheck_Analyzer(face).IsValid());
            GProp_GProps properties;
            BRepGProp::SurfaceProperties(face, properties);
            QVERIFY(std::abs(properties.Mass() - dimensions.first * dimensions.second) < 1e-8);
        }
    }

    void faceRejectsInvalidSource()
    {
        QVERIFY_EXCEPTION_THROWN(BasicFeatures::face(TopoDS_Wire{}), std::invalid_argument);
        BRepBuilderAPI_MakeWire builder;
        builder.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(2, 0, 0)).Edge());
        builder.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(2, 0, 0), gp_Pnt(2, 3, 0)).Edge());
        builder.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(2, 3, 0), gp_Pnt(0, 3, 0)).Edge());
        const auto openWire = builder.Wire();
        QVERIFY(!BRep_Tool::IsClosed(openWire));
        QVERIFY_EXCEPTION_THROWN(BasicFeatures::face(openWire), std::invalid_argument);
    }

    void extrudeGeometryAndErrors()
    {
        auto profile = std::make_shared<ProfileFixture>();
        QVERIFY(profile->recompute());
        ExtrudeFeature extrusion("extrude", profile, gp_Vec(0, 0, 4));
        QVERIFY2(extrusion.recompute(), extrusion.error().c_str());
        QCOMPARE(extrusion.shape().ShapeType(), TopAbs_SOLID);
        QVERIFY(BRepCheck_Analyzer(extrusion.shape()).IsValid());
        QVERIFY(std::abs(volume(extrusion.shape()) - 24) < 1e-8);
        extrusion.setVector(gp_Vec(0, 0, 7));
        QVERIFY(extrusion.isDirty());
        QVERIFY(extrusion.recompute());
        QVERIFY(std::abs(volume(extrusion.shape()) - 42) < 1e-8);
        extrusion.setVector(gp_Vec(0, 0, 0));
        QVERIFY(!extrusion.recompute());
        QVERIFY(extrusion.shape().IsNull());
        QVERIFY(extrusion.error().find("zero") != std::string::npos);
        QVERIFY_EXCEPTION_THROWN(ExtrudeFeature("missing", {}, gp_Vec(0, 0, 4)), std::invalid_argument);
        // Current API accepts a vector, including -Z; no positive length parameter exists.
        // TODO: length > 0 and missing sourceFeatureId tests for the future API.
    }

    void documentOwnership()
    {
        Document document;
        auto feature = std::make_unique<BoxFeature>(2, 3, 4);
        const auto* identity = feature.get();
        QCOMPARE(&document.addFeature(std::move(feature)), identity);
        QCOMPARE(document.features().size(), std::size_t{1});
        QVERIFY(std::abs(volume(document.features().front()->shape()) - 24) < 1e-8);
        QVERIFY_EXCEPTION_THROWN(document.addFeature(nullptr), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(document.addFeature(std::make_unique<BoxFeature>(0, 3, 4)), std::invalid_argument);
        QCOMPARE(document.features().size(), std::size_t{1});
        document.clear();
        QVERIFY(document.features().empty());
        // TODO: move identity/dependency ownership into Document; today these belong to Body.
    }

    void bodyIdentityAndDependencies()
    {
        Body body;
        auto profile = std::make_shared<ProfileFixture>();
        auto extrusion = std::make_shared<ExtrudeFeature>("extrude", profile, gp_Vec(0, 0, 4));
        body.addFeature(profile);
        body.addFeature(extrusion);
        QCOMPARE(body.findFeature("profile").get(), profile.get());
        QVERIFY(!body.findFeature("missing"));
        QVERIFY_EXCEPTION_THROWN(body.addFeature(std::make_shared<ProfileFixture>()), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(body.addFeature(nullptr), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(BoxParametricFeature("", 1, 2, 3), std::invalid_argument);
        QCOMPARE(body.features().size(), std::size_t{2});
        QCOMPARE(extrusion->dependencies().size(), std::size_t{1});
        QCOMPARE(extrusion->dependencies().front().lock().get(), profile.get());
        QVERIFY2(body.recompute(), body.lastError().c_str());
        profile->setWidth(5);
        // TODO: automatic graph propagation. Existing callers explicitly dirty the history suffix.
        body.markDirtyFrom(profile->id());
        QVERIFY(extrusion->isDirty());
        QVERIFY2(body.recompute(), body.lastError().c_str());
        QVERIFY(std::abs(volume(body.shape()) - 60) < 1e-8);
        profile->setWidth(0);
        body.markDirtyFrom(profile->id());
        QVERIFY(!body.recompute());
        QVERIFY(!body.lastError().empty());
        QVERIFY(!extrusion->recompute());
        QVERIFY(extrusion->error().find("Dependency") != std::string::npos);
        profile->setWidth(2);
        body.markDirtyFrom(profile->id());
        QVERIFY(body.recompute());
        QVERIFY(std::abs(volume(body.shape()) - 24) < 1e-8);
    }

    void serializationValidation()
    {
        // TODO: extend the production Sketch/Face round-trip to Extrude when serializable.
        // Existing project_file_tests covers all currently serializable types and dependency links.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("validation.pcad");
        Document document;
        Body body;
        auto box = std::make_shared<BoxParametricFeature>("box", 2, 3, 4);
        body.addFeature(box);
        QVERIFY(body.recompute());
        QString error;
        QVERIFY2(ProjectFile::save(path, document, body, error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto original = file.readAll();
        file.close();
        auto root = QJsonDocument::fromJson(original).object();
        root.insert("version", 999);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const auto invalid = QJsonDocument(root).toJson();
        QCOMPARE(file.write(invalid), qint64(invalid.size()));
        file.close();
        QVERIFY(!ProjectFile::load(path, document, body, error));
        QVERIFY2(error.contains("version"), qPrintable(error));
        QCOMPARE(body.findFeature("box").get(), box.get());
        QVERIFY(std::abs(volume(body.shape()) - 24) < 1e-8);

        Body unsupported;
        auto profile = std::make_shared<ProfileFixture>();
        QVERIFY(profile->recompute());
        unsupported.addFeature(profile);
        unsupported.addFeature(std::make_shared<ExtrudeFeature>("extrude", profile, gp_Vec(0, 0, 4)));
        QVERIFY(unsupported.recompute());
        QVERIFY(!ProjectFile::save(path, document, unsupported, error));
        QVERIFY2(error.contains("Unsupported parametric feature type"), qPrintable(error));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), invalid);
    }
};

QTEST_APPLESS_MAIN(ModelTests)
#include "model_tests.moc"
