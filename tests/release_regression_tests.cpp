#include "application/ModelingController.h"
#include "model/ProjectFile.h"
#include "operations/ParametricFeatures.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace cad::application;
using namespace cad::parametric;

namespace {

void addRectangle(ModelingController& controller, const std::string& sketchId,
                  const double width, const double height)
{
    QVERIFY(controller.addSketchLine(sketchId, {0, 0}, {width, 0}).success);
    QVERIFY(controller.addSketchLine(sketchId, {width, 0}, {width, height}).success);
    QVERIFY(controller.addSketchLine(sketchId, {width, height}, {0, height}).success);
    QVERIFY(controller.addSketchLine(sketchId, {0, height}, {0, 0}).success);
}

double volumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

void assertValidSolid(const TopoDS_Shape& shape)
{
    QVERIFY(!shape.IsNull());
    QVERIFY(BRepCheck_Analyzer(shape).IsValid());
    TopExp_Explorer solids(shape, TopAbs_SOLID);
    QVERIFY(solids.More());
}

void assertBoundingBox(const TopoDS_Shape& shape, const double x, const double y,
                       const double z, const double tolerance = 1.0e-5)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const auto actualX = xmax - xmin;
    const auto actualY = ymax - ymin;
    const auto actualZ = zmax - zmin;
    QVERIFY2(std::abs(actualX - x) <= tolerance,
        qPrintable(QString("X extent %1, expected %2").arg(actualX).arg(x)));
    QVERIFY2(std::abs(actualY - y) <= tolerance,
        qPrintable(QString("Y extent %1, expected %2").arg(actualY).arg(y)));
    QVERIFY2(std::abs(actualZ - z) <= tolerance,
        qPrintable(QString("Z extent %1, expected %2").arg(actualZ).arg(z)));
}

SelectionSnapshot objectSelection(const std::string& featureId)
{
    return SelectionSnapshot{{{featureId, SelectionKind::Object, std::nullopt}}};
}

} // namespace

class ReleaseRegressionTests final : public QObject
{
    Q_OBJECT

private slots:
    void electronicsEnclosureRoundTrip()
    {
        ModelingController controller;
        const auto baseSketch = controller.createSketch(SketchSupportType::XY);
        QVERIFY2(baseSketch.success, qPrintable(QString::fromStdString(baseSketch.error)));
        addRectangle(controller, baseSketch.id, 80.0, 50.0);

        const auto base = controller.createExtrudeFromSketch(objectSelection(baseSketch.id), 30.0);
        QVERIFY2(base.success, qPrintable(QString::fromStdString(base.error)));
        const auto baseFeature = controller.body().findFeature(base.id);
        QVERIFY(baseFeature);
        assertValidSolid(baseFeature->shape());
        assertBoundingBox(baseFeature->shape(), 80.0, 50.0, 30.0);
        QVERIFY(std::abs(volumeOf(baseFeature->shape()) - 120000.0) < 1.0e-5);

        const auto cavitySketch = controller.createSketchOnFace({{
            {base.id, SelectionKind::Face, 1}}});
        QVERIFY2(cavitySketch.success, qPrintable(QString::fromStdString(cavitySketch.error)));
        addRectangle(controller, cavitySketch.id, 60.0, 30.0);
        const auto pocket = controller.createPocketFromSketch(
            objectSelection(cavitySketch.id), 10.0);
        QVERIFY2(pocket.success, qPrintable(QString::fromStdString(pocket.error)));
        const auto pocketFeature = controller.body().findFeature(pocket.id);
        QVERIFY(pocketFeature);
        assertValidSolid(pocketFeature->shape());
        QVERIFY(volumeOf(pocketFeature->shape()) < volumeOf(baseFeature->shape()));

        const auto fillet = controller.createFillet(
            SelectionSnapshot{{{pocket.id, SelectionKind::Edge, 1}}}, 1.0);
        QVERIFY2(fillet.success, qPrintable(QString::fromStdString(fillet.error)));
        const auto filletFeature = controller.body().findFeature(fillet.id);
        QVERIFY(filletFeature);
        assertValidSolid(filletFeature->shape());

        const auto idsBefore = controller.features();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto path = directory.filePath("enclosure.pcad");
        QVERIFY2(ProjectFile::save(path, controller.document(), controller.body(), error),
                 qPrintable(error));

        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loadedDocument, loadedBody, error), qPrintable(error));
        QVERIFY(loadedBody.recompute());
        QCOMPARE(loadedBody.features().size(), controller.body().features().size());
        for (const auto& descriptor : idsBefore) {
            const auto loaded = loadedBody.findFeature(descriptor.id);
            QVERIFY2(loaded != nullptr, qPrintable(QString::fromStdString(descriptor.id)));
            QCOMPARE(std::string(loaded->typeId()),
                     std::string(controller.body().findFeature(descriptor.id)->typeId()));
            QVERIFY(loaded->state() == FeatureState::UpToDate);
        }
        assertValidSolid(loadedBody.findFeature(fillet.id)->shape());
        QVERIFY(std::abs(volumeOf(loadedBody.findFeature(fillet.id)->shape())
                         - volumeOf(filletFeature->shape())) < 1.0e-5);
    }

    void dependencyRecomputeKeepsIdentityAndChangesGeometry()
    {
        ModelingController controller;
        const auto sketch = controller.createSketch();
        QVERIFY(sketch.success);
        addRectangle(controller, sketch.id, 20.0, 10.0);
        const auto extrude = controller.createExtrudeFromSketch(objectSelection(sketch.id), 10.0);
        QVERIFY(extrude.success);
        const auto sketchFeature = std::dynamic_pointer_cast<SketchFeature>(
            controller.body().findFeature(sketch.id));
        const auto extrudeFeature = controller.body().findFeature(extrude.id);
        QVERIFY(sketchFeature && extrudeFeature);
        const auto oldVolume = volumeOf(extrudeFeature->shape());
        const auto oldRevision = controller.body().geometryRevision();

        sketchFeature->replaceEntities(0, sketchFeature->entityCount(), {
            SketchLine{{0, 0}, {24, 0}}, SketchLine{{24, 0}, {24, 10}},
            SketchLine{{24, 10}, {0, 10}}, SketchLine{{0, 10}, {0, 0}}});
        controller.body().markDirtyFrom(sketch.id);
        QVERIFY(controller.body().recompute());
        QVERIFY(controller.body().geometryRevision() > oldRevision);
        QVERIFY(sketchFeature->state() == FeatureState::UpToDate);
        QVERIFY(extrudeFeature->state() == FeatureState::UpToDate);
        QVERIFY(volumeOf(extrudeFeature->shape()) > oldVolume);
        QCOMPARE(sketchFeature->id(), sketch.id);
        QCOMPARE(extrudeFeature->id(), extrude.id);
        const auto recomputedShape = extrudeFeature->shape();
        QVERIFY(controller.body().recompute());
        QVERIFY(extrudeFeature->shape().IsSame(recomputedShape));
    }

    void faceAttachedSketchResolvesOrFailsExplicitly()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto attached = controller.createSketchOnFace({{
            {box.id, SelectionKind::Face, 1}}});
        QVERIFY(attached.success);
        const auto sketch = std::dynamic_pointer_cast<SketchFeature>(
            controller.body().findFeature(attached.id));
        QVERIFY(sketch && sketch->faceReference());
        addRectangle(controller, attached.id, 10.0, 8.0);
        const auto frameBefore = sketch->currentFrame();
        const auto pocket = controller.createPocketFromSketch(objectSelection(attached.id), 2.0);
        QVERIFY2(pocket.success, qPrintable(QString::fromStdString(pocket.error)));

        QVERIFY(controller.setFeatureProperty(box.id, "width", 120.0).success);
        QVERIFY(sketch->state() == FeatureState::UpToDate);
        QVERIFY(sketch->currentFrame().normal.IsParallel(frameBefore.normal, 1.0e-7));
        QVERIFY(controller.body().findFeature(pocket.id)->state() == FeatureState::UpToDate);
        assertValidSolid(controller.body().findFeature(pocket.id)->shape());
    }

    void undoRedoPreservesFeatureGraph()
    {
        ModelingController controller;
        const auto sketch = controller.createSketch();
        QVERIFY(sketch.success);
        const auto initialCount = controller.body().features().size();
        QVERIFY(controller.addSketchLine(sketch.id, {0, 0}, {20, 0}).success);
        controller.undo();
        const auto sketchFeature = std::dynamic_pointer_cast<SketchFeature>(
            controller.body().findFeature(sketch.id));
        QVERIFY(sketchFeature && sketchFeature->entityCount() == 0);
        controller.redo();
        QVERIFY(sketchFeature->entityCount() == 1);
        controller.undo();
        QVERIFY(controller.deleteFeature(sketch.id).success);
        QCOMPARE(controller.body().features().size(), std::size_t{0});
        controller.undo();
        QCOMPARE(controller.body().features().size(), initialCount);
        QCOMPARE(controller.body().findFeature(sketch.id).get(), sketchFeature.get());
        QVERIFY(controller.body().recompute());
    }

    void emptyAndInvalidSketchesRemainSafe()
    {
        ModelingController controller;
        for (const auto support : {SketchSupportType::XY, SketchSupportType::XZ,
                                   SketchSupportType::YZ}) {
            const auto sketch = controller.createSketch(support);
            QVERIFY(sketch.success);
            const auto feature = std::dynamic_pointer_cast<SketchFeature>(
                controller.body().findFeature(sketch.id));
            QVERIFY(feature);
            QVERIFY(feature->shape().ShapeType() == TopAbs_COMPOUND);
            QVERIFY(!TopExp_Explorer(feature->shape(), TopAbs_EDGE).More());
            const auto extrude = controller.createExtrudeFromSketch(objectSelection(sketch.id));
            QVERIFY(!extrude.success);
            QVERIFY(QString::fromStdString(extrude.error).contains("user geometry"));
        }

        const auto open = controller.createSketch();
        QVERIFY(open.success);
        QVERIFY(controller.addSketchLine(open.id, {0, 0}, {20, 0}).success);
        QVERIFY(!controller.actionState({open.id}).canExtrude);
        QVERIFY(controller.body().recompute());
        const auto openFeature = std::dynamic_pointer_cast<SketchFeature>(
            controller.body().findFeature(open.id));
        QVERIFY(openFeature);
        openFeature->removeLastEntity();
        QVERIFY(controller.body().recompute());
        QVERIFY(!TopExp_Explorer(openFeature->shape(), TopAbs_EDGE).More());

        const auto selfIntersecting = controller.createSketch();
        QVERIFY(selfIntersecting.success);
        QVERIFY(controller.addSketchLine(selfIntersecting.id, {0, 0}, {20, 10}).success);
        QVERIFY(controller.addSketchLine(selfIntersecting.id, {20, 10}, {0, 10}).success);
        QVERIFY(controller.addSketchLine(selfIntersecting.id, {0, 10}, {20, 0}).success);
        QVERIFY(controller.addSketchLine(selfIntersecting.id, {20, 0}, {0, 0}).success);
        QVERIFY(!controller.createExtrudeFromSketch(
            objectSelection(selfIntersecting.id)).success);
    }

    void booleanVisibilityAndPersistenceRegression()
    {
        ModelingController controller;
        const auto left = controller.createBox();
        const auto right = controller.createBox();
        QVERIFY(left.success && right.success);
        QVERIFY(controller.setFeatureProperty(right.id, "width", 5.0).success);
        const auto boolean = controller.createBoolean(BooleanKind::Fuse, {left.id, right.id});
        QVERIFY2(boolean.success, qPrintable(QString::fromStdString(boolean.error)));
        assertValidSolid(controller.body().findFeature(boolean.id)->shape());
        QVERIFY(controller.setFeatureVisibility({boolean.id}, false).success);
        QVERIFY(!controller.body().findFeature(boolean.id)->userVisible());
        controller.undo();
        QVERIFY(controller.body().findFeature(boolean.id)->userVisible());
        controller.redo();
        QVERIFY(!controller.body().findFeature(boolean.id)->userVisible());

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto path = directory.filePath("boolean.pcad");
        QVERIFY(ProjectFile::save(path, controller.document(), controller.body(), error));
        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loadedDocument, loadedBody, error), qPrintable(error));
        QVERIFY(loadedBody.recompute());
        const auto loaded = loadedBody.findFeature(boolean.id);
        QVERIFY(loaded && !loaded->userVisible());
        assertValidSolid(loaded->shape());
    }
};

QTEST_APPLESS_MAIN(ReleaseRegressionTests)
#include "release_regression_tests.moc"
