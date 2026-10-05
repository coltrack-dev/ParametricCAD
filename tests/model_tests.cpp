#include "model/Feature.h"
#include "model/Document.h"
#include "model/Body.h"
#include "model/ProjectFile.h"
#include "application/VisibilityManager.h"
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
using cad::application::VisibilityManager;
using cad::application::VisibilityMode;
using cad::application::VisibilityCategory;

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

class ClassifiedFixture final : public ParametricFeature
{
public:
    ClassifiedFixture(std::string id, std::string type, FeatureRole role)
        : ParametricFeature(std::move(id), type), type_(std::move(type)), role_(role) {}
    const char* typeId() const noexcept override { return type_.c_str(); }
    FeatureRole role() const noexcept override { return role_; }
private:
    TopoDS_Shape build() const override { return BasicFeatures::box(1, 1, 1); }
    std::string type_;
    FeatureRole role_;
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

    void visibilityManagerProjectsTemporaryPolicy()
    {
        Body body;
        auto a = std::make_shared<BoxParametricFeature>("a", 2, 2, 2);
        auto b = std::make_shared<BoxParametricFeature>("b", 3, 3, 3);
        auto c = std::make_shared<BoxParametricFeature>("c", 4, 4, 4);
        body.addFeature(a);
        body.addFeature(b);
        body.addFeature(c);
        QVERIFY(body.recompute());

        VisibilityManager manager;
        QCOMPARE(manager.effectiveMode(*a, body), VisibilityMode::Visible);
        QCOMPARE(manager.projection(body).size(), std::size_t{3});

        a->setUserVisible(false);
        QCOMPARE(manager.effectiveMode(*a, body), VisibilityMode::Hidden);
        a->setUserVisible(true);

        manager.setIsolatedFeatures({"a"});
        QCOMPARE(manager.effectiveMode(*a, body), VisibilityMode::Visible);
        QCOMPARE(manager.effectiveMode(*b, body), VisibilityMode::Hidden);

        manager.ghostOthers({"a"});
        QCOMPARE(manager.effectiveMode(*a, body), VisibilityMode::Visible);
        QCOMPARE(manager.effectiveMode(*b, body), VisibilityMode::Hidden);
        manager.clearIsolation();
        QCOMPARE(manager.effectiveMode(*a, body), VisibilityMode::Visible);
        QCOMPARE(manager.effectiveMode(*b, body), VisibilityMode::Ghosted);

        b->setUserVisible(false);
        QCOMPARE(manager.effectiveMode(*b, body), VisibilityMode::Hidden);
        manager.clearGhosting();
        QCOMPARE(manager.effectiveMode(*b, body), VisibilityMode::Hidden);
        b->setUserVisible(true);
        QCOMPARE(manager.effectiveMode(*b, body), VisibilityMode::Visible);

        QVERIFY(a->userVisible());
        QVERIFY(b->userVisible());
        QVERIFY(c->userVisible());

        Body dependentBody;
        auto base = std::make_shared<HexagonFeature>("base", 30, 12);
        auto tool = std::make_shared<CylinderParametricFeature>("tool", 5, 20);
        auto cut = std::make_shared<BooleanFeature>(
            "cut", base, tool, BooleanOperation::Cut);
        dependentBody.addFeature(base);
        dependentBody.addFeature(tool);
        dependentBody.addFeature(cut);
        QVERIFY(dependentBody.recompute());
        VisibilityManager dependentManager;
        QCOMPARE(dependentManager.effectiveMode(*base, dependentBody), VisibilityMode::Hidden);
        QCOMPARE(dependentManager.effectiveMode(*cut, dependentBody), VisibilityMode::Visible);

        dependentManager.ghostOthers({"cut"});
        QCOMPARE(dependentManager.effectiveMode(*base, dependentBody), VisibilityMode::Hidden);
    }

    void visibilityGroupsCombineAndValidate()
    {
        Body body;
        auto a = std::make_shared<BoxParametricFeature>("a", 2, 2, 2);
        auto b = std::make_shared<BoxParametricFeature>("b", 3, 3, 3);
        auto c = std::make_shared<BoxParametricFeature>("c", 4, 4, 4);
        body.addFeature(a);
        body.addFeature(b);
        body.addFeature(c);
        QVERIFY(body.recompute());

        VisibilityManager manager;
        const auto building = manager.createGroup("Building");
        QVERIFY(building);
        const auto roof = manager.createGroup("Roof", *building);
        QVERIFY(roof);
        const auto fasteners = manager.createGroup("Fasteners", *roof);
        QVERIFY(fasteners);
        QVERIFY(manager.addFeaturesToGroup(*roof, {"a", "b"}));
        QVERIFY(manager.addFeaturesToGroup(*fasteners, {"a", "missing"}));
        QVERIFY(manager.setGroupVisibility(*building, VisibilityMode::Ghosted));
        QCOMPARE(manager.effectiveMode(*a, body), VisibilityMode::Ghosted);
        QCOMPARE(manager.effectiveMode(*c, body), VisibilityMode::Visible);
        QVERIFY(manager.setGroupVisibility(*building, VisibilityMode::Hidden));
        QVERIFY(manager.setGroupVisibility(*roof, VisibilityMode::Visible));
        QCOMPARE(manager.effectiveMode(*a, body), VisibilityMode::Hidden);
        QVERIFY(manager.renameGroup(*roof, "Roof Renamed"));
        QCOMPARE(manager.groups()[1].name, std::string("Roof Renamed"));
        QVERIFY(manager.setGroupVisibility(*building, VisibilityMode::Visible));
        QVERIFY(manager.setGroupVisibility(*fasteners, VisibilityMode::Hidden));
        QCOMPARE(manager.effectiveMode(*a, body), VisibilityMode::Hidden);
        QCOMPARE(manager.effectiveMode(*b, body), VisibilityMode::Visible);

        QVERIFY(manager.setGroupVisibility(*fasteners, VisibilityMode::Visible));
        manager.ghostOthers({"a"});
        QCOMPARE(manager.effectiveMode(*a, body), VisibilityMode::Visible);
        QCOMPARE(manager.effectiveMode(*b, body), VisibilityMode::Ghosted);
        manager.setIsolatedFeatures({"c"});
        QCOMPARE(manager.effectiveMode(*a, body), VisibilityMode::Hidden);
        QCOMPARE(manager.effectiveMode(*c, body), VisibilityMode::Ghosted);
        manager.clearIsolation();
        manager.clearGhosting();
        QVERIFY(manager.removeFeaturesFromGroup(*roof, {"b"}));
        QCOMPARE(manager.effectiveMode(*b, body), VisibilityMode::Visible);

        QVERIFY(!manager.setGroupParent(*building, *building));
        QVERIFY(!manager.setGroupParent(*building, *fasteners));
        QVERIFY(manager.removeGroup(*fasteners));
        QCOMPARE(manager.groups().size(), std::size_t{2});
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

    void visibilityGroupsRoundTrip()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Document document;
        Body body;
        auto a = std::make_shared<BoxParametricFeature>("a", 2, 3, 4);
        auto b = std::make_shared<CylinderParametricFeature>("b", 2, 6);
        body.addFeature(a);
        body.addFeature(b);
        QVERIFY(body.recompute());

        VisibilityManager manager;
        const auto parent = manager.createGroup("Parent");
        const auto child = manager.createGroup("Child", *parent);
        QVERIFY(parent && child);
        QVERIFY(manager.addFeaturesToGroup(*parent, {"a"}));
        QVERIFY(manager.addFeaturesToGroup(*child, {"a", "b", "missing"}));
        QVERIFY(manager.setGroupVisibility(*child, VisibilityMode::Hidden));
        QVERIFY(manager.setTypeFilter("FutureCustom", VisibilityMode::Ghosted));
        QVERIFY(manager.setRoleFilter(FeatureRole::Generic, VisibilityMode::Visible));

        QString error;
        const auto path = directory.filePath("groups.pcad");
        QVERIFY2(ProjectFile::save(path, document, body, error, &manager), qPrintable(error));
        VisibilityManager loadedManager;
        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loadedDocument, loadedBody, error,
            {}, nullptr, true, &loadedManager), qPrintable(error));
        QCOMPARE(loadedManager.groups().size(), std::size_t{2});
        QCOMPARE(loadedManager.filters().typeModes.at("FutureCustom"), VisibilityMode::Ghosted);
        QCOMPARE(loadedManager.filters().roleModes.at(FeatureRole::Generic), VisibilityMode::Visible);
        QCOMPARE(loadedManager.effectiveMode(*loadedBody.findFeature("a"), loadedBody),
                 VisibilityMode::Hidden);
        QCOMPARE(loadedManager.effectiveMode(*loadedBody.findFeature("b"), loadedBody),
                 VisibilityMode::Hidden);

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        root.remove("visibility");
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const auto oldFormat = QJsonDocument(root).toJson();
        QCOMPARE(file.write(oldFormat), qint64(oldFormat.size()));
        file.close();
        VisibilityManager oldFormatManager;
        QVERIFY2(ProjectFile::load(path, loadedDocument, loadedBody, error,
            {}, nullptr, true, &oldFormatManager), qPrintable(error));
        QVERIFY(oldFormatManager.groups().empty());
        QVERIFY(oldFormatManager.filters().empty());
    }

    void visibilityFiltersComposeWithoutRecompute()
    {
        Body body;
        auto box = std::make_shared<ClassifiedFixture>("box", "Box", FeatureRole::Generic);
        auto sketch = std::make_shared<ClassifiedFixture>("sketch", "Sketch", FeatureRole::Sketch);
        auto face = std::make_shared<ClassifiedFixture>("face", "Face", FeatureRole::Face);
        auto operation = std::make_shared<ClassifiedFixture>("operation", "Extrude", FeatureRole::Generic);
        auto pattern = std::make_shared<ClassifiedFixture>("pattern", "LinearPattern", FeatureRole::Generic);
        auto custom = std::make_shared<ClassifiedFixture>("custom", "FutureCustom", FeatureRole::Generic);
        for (const auto& feature : {box, sketch, face, operation, pattern, custom})
            body.addFeature(feature);
        QVERIFY(body.recompute());
        const auto originalShape = box->shape();

        VisibilityManager manager;
        QCOMPARE(manager.effectiveMode(*box, body), VisibilityMode::Visible);
        QVERIFY(manager.setTypeFilter("Box", VisibilityMode::Hidden));
        QCOMPARE(manager.effectiveMode(*box, body), VisibilityMode::Hidden);
        QVERIFY(manager.setTypeFilter("Box", VisibilityMode::Ghosted));
        QCOMPARE(manager.effectiveMode(*box, body), VisibilityMode::Ghosted);
        QVERIFY(manager.setCategoryFilter(VisibilityCategory::Primitives,
                                          VisibilityMode::Hidden));
        QCOMPARE(manager.effectiveMode(*box, body), VisibilityMode::Hidden);

        QVERIFY(manager.setRoleFilter(FeatureRole::Sketch, VisibilityMode::Ghosted));
        QCOMPARE(manager.effectiveMode(*sketch, body), VisibilityMode::Ghosted);
        QVERIFY(manager.setRoleFilter(FeatureRole::Face, VisibilityMode::Hidden));
        QCOMPARE(manager.effectiveMode(*face, body), VisibilityMode::Hidden);
        QVERIFY(manager.setCategoryFilter(VisibilityCategory::Operations,
                                          VisibilityMode::Hidden));
        QCOMPARE(manager.effectiveMode(*operation, body), VisibilityMode::Hidden);
        QVERIFY(manager.setCategoryFilter(VisibilityCategory::Patterns,
                                          VisibilityMode::Ghosted));
        QCOMPARE(manager.effectiveMode(*pattern, body), VisibilityMode::Ghosted);
        QCOMPARE(manager.effectiveMode(*custom, body), VisibilityMode::Visible);

        const auto group = manager.createGroup("Operation Group");
        QVERIFY(group);
        QVERIFY(manager.addFeaturesToGroup(*group, {"operation"}));
        QVERIFY(manager.setGroupVisibility(*group, VisibilityMode::Ghosted));
        QVERIFY(manager.setTypeFilter("Extrude", VisibilityMode::Hidden));
        QCOMPARE(manager.effectiveMode(*operation, body), VisibilityMode::Hidden);

        operation->setUserVisible(false);
        QVERIFY(manager.setTypeFilter("Extrude", VisibilityMode::Visible));
        QCOMPARE(manager.effectiveMode(*operation, body), VisibilityMode::Hidden);
        operation->setUserVisible(true);

        manager.setIsolatedFeatures({"custom"});
        manager.ghostOthers({"custom"});
        QCOMPARE(manager.effectiveMode(*custom, body), VisibilityMode::Visible);
        QCOMPARE(manager.effectiveMode(*box, body), VisibilityMode::Hidden);

        manager.clearIsolation();
        manager.clearGhosting();
        manager.clearFilters();
        QCOMPARE(manager.effectiveMode(*box, body), VisibilityMode::Visible);
        QVERIFY(box->state() == FeatureState::UpToDate);
        QVERIFY(box->shape().IsSame(originalShape));
    }
};

QTEST_APPLESS_MAIN(ModelTests)
#include "model_tests.moc"
