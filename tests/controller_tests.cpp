#include "application/ModelingController.h"
#include "application/ProjectController.h"
#include "application/SelectionResolver.h"
#include "model/ProjectFile.h"
#include "model/TopologicalReference.h"
#include "operations/SketchProfileBuilder.h"
#include "operations/SketchPathBuilder.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Builder.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopAbs_Orientation.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopoDS.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <array>
#include <variant>

namespace {
struct FacePick
{
    int index;
    gp_Vec normal;
};

FacePick firstPlanarFace(const TopoDS_Shape& shape)
{
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    for (int index = 1; index <= faces.Extent(); ++index) {
        const auto face = TopoDS::Face(faces.FindKey(index));
        BRepAdaptor_Surface surface(face, Standard_True);
        if (surface.GetType() != GeomAbs_Plane) continue;
        gp_Dir direction = surface.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED) direction.Reverse();
        return {index, gp_Vec(direction)};
    }
    return {0, {}};
}

int edgeAtOriginAlongX(const TopoDS_Shape& shape)
{
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    const gp_Pnt origin;
    for (int index = 1; index <= edges.Extent(); ++index) {
        const auto edge = TopoDS::Edge(edges.FindKey(index));
        BRepAdaptor_Curve curve(edge);
        if (curve.GetType() != GeomAbs_Line) continue;
        const auto first = curve.Value(curve.FirstParameter());
        const auto last = curve.Value(curve.LastParameter());
        const auto other = first.Distance(origin) <= 1.0e-6 ? last
            : last.Distance(origin) <= 1.0e-6 ? first : gp_Pnt(1.0, 1.0, 1.0);
        if ((first.Distance(origin) <= 1.0e-6 || last.Distance(origin) <= 1.0e-6)
            && other.X() > 1.0e-6
            && std::abs(other.Y()) <= 1.0e-6
            && std::abs(other.Z()) <= 1.0e-6) {
            return index;
        }
    }
    return 0;
}

double volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}
}

using namespace cad::application;
using namespace cad::parametric;

class ControllerTests final : public QObject
{
    Q_OBJECT

private slots:
    void typedSelectionResolvesCurrentSubshapes()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto feature = controller.body().findFeature(box.id);
        QVERIFY(feature);

        SelectionResolver resolver(controller.body());
        const auto object = resolver.resolve({box.id, SelectionKind::Object, std::nullopt});
        QVERIFY(object && !object->IsNull());

        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(feature->shape(), TopAbs_FACE, faces);
        const auto face = resolver.resolve({
            box.id, SelectionKind::Face, 1});
        QVERIFY(face && face->ShapeType() == TopAbs_FACE);

        const auto wrongType = resolver.resolve({box.id, SelectionKind::Edge, 1});
        QVERIFY(wrongType && wrongType->ShapeType() == TopAbs_EDGE);
        QVERIFY(!resolver.resolve({box.id, SelectionKind::Face, faces.Extent() + 1}));
        QVERIFY(!resolver.resolve({box.id, SelectionKind::Face, std::nullopt}));
        QVERIFY(!resolver.resolve({box.id, SelectionKind::Object, 1}));
    }

    void modelingOperationsUseStableSelectionIds()
    {
        ModelingController controller;
        const auto sketch = controller.createSketch();
        QVERIFY(sketch.success);

        auto state = controller.actionState({sketch.id});
        QVERIFY(state.canCreateFace);
        QVERIFY(state.canExtrude);

        const auto face = controller.createFace({sketch.id});
        QVERIFY(face.success);
        state = controller.actionState({face.id});
        QVERIFY(state.canExtrude);

        const auto extrude = controller.createExtrude({face.id});
        QVERIFY(extrude.success);
        QCOMPARE(controller.body().features().size(), std::size_t{3});

        controller.undoStack().undo();
        QVERIFY(!controller.body().findFeature(extrude.id));
        controller.undoStack().redo();
        QVERIFY(controller.body().findFeature(extrude.id));
        const auto box = controller.createBox();
        const auto cylinder = controller.createCylinder();
        QVERIFY(box.success && cylinder.success);
        const auto boolean = controller.createBoolean(
            BooleanKind::Cut, {box.id, cylinder.id}, cylinder.id);
        QVERIFY(boolean.success);
        QVERIFY(controller.actionState({box.id, cylinder.id}).canBoolean);
    }

    void createSketchSupportsAllPlacementsAndRoundTrips()
    {
        ModelingController controller;
        const auto xy = controller.createSketch(SketchSupportType::XY);
        const auto xz = controller.createSketch(SketchSupportType::XZ);
        const auto yz = controller.createSketch(SketchSupportType::YZ);
        QVERIFY(xy.success && xz.success && yz.success);

        QVERIFY(controller.addSketchLine(xy.id, {0, 0}, {20, 0}).success);
        QVERIFY(controller.addSketchLine(xy.id, {20, 0}, {20, 10}).success);
        QVERIFY(controller.addSketchLine(xy.id, {20, 10}, {0, 10}).success);
        QVERIFY(controller.addSketchLine(xy.id, {0, 10}, {0, 0}).success);
        QVERIFY(controller.addSketchCircle(xz.id, {0, 0}, 5.0).success);
        QVERIFY(controller.addSketchCircle(yz.id, {0, 0}, 5.0).success);

        const auto xyFeature = std::dynamic_pointer_cast<SketchFeature>(
            controller.body().findFeature(xy.id));
        QVERIFY(xyFeature);
        QCOMPARE(xyFeature->supportType(), SketchSupportType::XY);
        QCOMPARE(xyFeature->role(), FeatureRole::Sketch);
        QCOMPARE(xyFeature->name(), std::string("Sketch"));
        QVERIFY(!cad::operations::SketchProfileBuilder::build(*xyFeature).faces.empty());
        const auto firstLineId = std::visit(
            [](const auto& entity) { return entity.id; }, xyFeature->entities().front());
        QVERIFY(controller.addSketchHorizontal(xy.id, firstLineId).success);

        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto face = firstPlanarFace(controller.body().findFeature(box.id)->shape());
        QVERIFY(face.index > 0);
        const auto attached = controller.createSketchOnFace({{
            {box.id, SelectionKind::Face, face.index}}});
        QVERIFY(attached.success);
        QVERIFY(controller.addSketchCircle(attached.id, {0, 0}, 3.0).success);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto path = directory.filePath("sketch-placements.pcad");
        QVERIFY(ProjectFile::save(path, controller.document(), controller.body(), error));
        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loadedDocument, loadedBody, error), qPrintable(error));

        const auto loadedXY = std::dynamic_pointer_cast<SketchFeature>(loadedBody.findFeature(xy.id));
        const auto loadedXZ = std::dynamic_pointer_cast<SketchFeature>(loadedBody.findFeature(xz.id));
        const auto loadedYZ = std::dynamic_pointer_cast<SketchFeature>(loadedBody.findFeature(yz.id));
        const auto loadedFace = std::dynamic_pointer_cast<SketchFeature>(loadedBody.findFeature(attached.id));
        QVERIFY(loadedXY && loadedXZ && loadedYZ && loadedFace);
        QCOMPARE(loadedXY->supportType(), SketchSupportType::XY);
        QCOMPARE(loadedXZ->supportType(), SketchSupportType::XZ);
        QCOMPARE(loadedYZ->supportType(), SketchSupportType::YZ);
        QCOMPARE(loadedFace->supportType(), SketchSupportType::Face);
        QCOMPARE(loadedXY->entityCount(), std::size_t{4});
        QCOMPARE(loadedXZ->entityCount(), std::size_t{1});
        QCOMPARE(loadedYZ->entityCount(), std::size_t{1});
        QCOMPARE(loadedFace->entityCount(), std::size_t{1});
        QCOMPARE(loadedXY->constraints().size(), std::size_t{1});
        QVERIFY(loadedFace->faceReference().has_value());
    }

    void sketchExtrudeEligibilityIncludesFaceAttachedProfiles()
    {
        ModelingController controller;
        const auto global = controller.createSketch();
        QVERIFY(global.success);
        QVERIFY(controller.addSketchLine(global.id, {0, 0}, {20, 0}).success);
        QVERIFY(controller.addSketchLine(global.id, {20, 0}, {20, 10}).success);
        QVERIFY(controller.addSketchLine(global.id, {20, 10}, {0, 10}).success);
        QVERIFY(controller.addSketchLine(global.id, {0, 10}, {0, 0}).success);
        QVERIFY(controller.actionState({global.id}).canExtrude);
        QVERIFY(controller.actionState(SelectionSnapshot{{
            {global.id, SelectionKind::Object, std::nullopt}}}).canExtrude);
        QVERIFY(controller.createExtrudeFromSketch(SelectionSnapshot{{
            {global.id, SelectionKind::Object, std::nullopt}}}, 5.0).success);

        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto attached = controller.createSketchOnFace(SelectionSnapshot{{
            {box.id, SelectionKind::Face, 1}}});
        QVERIFY(attached.success);
        QVERIFY(controller.addSketchLine(attached.id, {0, 0}, {20, 0}).success);
        QVERIFY(controller.addSketchLine(attached.id, {20, 0}, {20, 10}).success);
        QVERIFY(controller.addSketchLine(attached.id, {20, 10}, {0, 10}).success);
        QVERIFY(controller.addSketchLine(attached.id, {0, 10}, {0, 0}).success);
        QVERIFY(controller.addSketchLine(attached.id, {0, -10}, {0, 20}, true).success);
        QVERIFY(controller.actionState({attached.id}).canExtrude);
        QVERIFY(controller.actionState({attached.id}).canRevolve);
        const auto attachedSelection = SelectionSnapshot{{
            {attached.id, SelectionKind::Object, std::nullopt}}};
        QVERIFY(controller.actionState(attachedSelection).canExtrude);
        QVERIFY(controller.actionState(attachedSelection).canRevolve);
        const auto extrude = controller.createExtrudeFromSketch(attachedSelection, 5.0);
        QVERIFY2(extrude.success, qPrintable(QString::fromStdString(extrude.error)));
        QVERIFY(!controller.body().findFeature(extrude.id)->shape().IsNull());

        const auto open = controller.createSketch();
        QVERIFY(open.success);
        QVERIFY(controller.addSketchLine(open.id, {0, 0}, {20, 0}).success);
        QVERIFY(!controller.actionState({open.id}).canExtrude);
        QVERIFY(!controller.actionState({box.id}).canExtrude);
        QVERIFY(!controller.actionState({global.id, attached.id}).canExtrude);
    }

    void sketchExtrudeAndPocketAreParametric()
    {
        ModelingController extrudeController;
        const auto sketch = extrudeController.createSketch();
        QVERIFY(sketch.success);
        const SelectionSnapshot sketchSelection{{
            {sketch.id, SelectionKind::Object, std::nullopt}}};
        const auto extrude = extrudeController.createExtrudeFromSketch(sketchSelection, 25.0);
        QVERIFY2(extrude.success, qPrintable(QString::fromStdString(extrude.error)));
        const auto extruded = extrudeController.body().findFeature(extrude.id);
        QVERIFY(extruded && !extruded->shape().IsNull());
        QVERIFY(volume(extruded->shape()) > 100.0);
        QVERIFY(extruded->dependencies().size() == 1);
        QVERIFY(std::abs(volume(extruded->shape()) - 150000.0) < 1e-7);
        extrudeController.undo();
        QVERIFY(!extrudeController.body().findFeature(extrude.id));
        extrudeController.redo();
        QVERIFY(extrudeController.body().findFeature(extrude.id));
        QVERIFY(!extruded->shape().IsNull());
        QVERIFY(extrudeController.setFeatureProperty(
            extrude.id, "distance", 30.0).success);
        QVERIFY(std::abs(volume(extruded->shape()) - 180000.0) < 1e-7);
        QVERIFY(extrudeController.setFeatureProperty(
            extrude.id, "reversed", true).success);
        extrudeController.undo();
        QVERIFY(extruded->state() == cad::parametric::FeatureState::UpToDate);
        extrudeController.redo();
        QVERIFY(extruded->state() == cad::parametric::FeatureState::UpToDate);

        ModelingController pocketController;
        const auto box = pocketController.createBox();
        QVERIFY(box.success);
        const SelectionSnapshot faceSelection{{
            {box.id, SelectionKind::Face, 1}}};
        const auto sketchOnFace = pocketController.createSketchOnFace(faceSelection);
        QVERIFY2(sketchOnFace.success, qPrintable(QString::fromStdString(sketchOnFace.error)));
        QVERIFY(pocketController.addSketchCircle(sketchOnFace.id, {0.0, 0.0}, 5.0).success);
        const double before = volume(pocketController.body().findFeature(box.id)->shape());
        const auto pocket = pocketController.createPocketFromSketch({{
            {sketchOnFace.id, SelectionKind::Object, std::nullopt}}}, 10.0);
        QVERIFY2(pocket.success, qPrintable(QString::fromStdString(pocket.error)));
        const auto pocketFeature = pocketController.body().findFeature(pocket.id);
        QVERIFY(pocketFeature && !pocketFeature->shape().IsNull());
        const double pocketVolumeAtTen = volume(pocketFeature->shape());
        QVERIFY(pocketVolumeAtTen < before);
        QVERIFY(pocketFeature->dependencies().size() == 2);
        pocketController.undo();
        QVERIFY(!pocketController.body().findFeature(pocket.id));
        pocketController.redo();
        QVERIFY(pocketController.body().findFeature(pocket.id));
        QVERIFY(!pocketFeature->shape().IsNull());
        QVERIFY(pocketController.setFeatureProperty(
            pocket.id, "depth", 5.0).success);
        const double pocketVolumeAtFive = volume(pocketFeature->shape());
        QVERIFY(pocketVolumeAtFive < before);
        QVERIFY(pocketVolumeAtFive > pocketVolumeAtTen);
        pocketController.undo();
        QVERIFY(pocketFeature->state() == cad::parametric::FeatureState::UpToDate);
        pocketController.redo();
        QVERIFY(pocketFeature->state() == cad::parametric::FeatureState::UpToDate);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto path = directory.filePath("sketch-extrude-pocket.pcad");
        Document document;
        QVERIFY2(ProjectFile::save(path, document, pocketController.body(), error),
                 qPrintable(error));
        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loadedDocument, loadedBody, error),
                 qPrintable(error));
        QVERIFY(loadedBody.findFeature(pocket.id));
        QVERIFY(!loadedBody.findFeature(pocket.id)->shape().IsNull());
    }

    void sketchProfilesCreateIndependentExtrudes()
    {
        ModelingController circleController;
        const auto circleSketch = circleController.createSketch();
        QVERIFY(circleSketch.success);
        QVERIFY(circleController.addSketchCircle(
            circleSketch.id, {0.0, 0.0}, 10.0).success);
        const auto circleSketchFeature = circleController.body().findFeature(circleSketch.id);
        QVERIFY(circleSketchFeature->shape().ShapeType() == TopAbs_FACE);
        const auto circleExtrude = circleController.createExtrudeFromSketch({{
            {circleSketch.id, SelectionKind::Object, std::nullopt}}}, 8.0);
        QVERIFY2(circleExtrude.success,
            qPrintable(QString::fromStdString(circleExtrude.error)));
        QVERIFY(volume(circleController.body().findFeature(circleExtrude.id)->shape()) > 0.0);

        ModelingController rectangleController;
        const auto rectangleSketch = rectangleController.createSketch();
        QVERIFY(rectangleSketch.success);
        QVERIFY(rectangleController.addSketchLine(
            rectangleSketch.id, {-10.0, -5.0}, {10.0, -5.0}).success);
        QVERIFY(rectangleController.addSketchLine(
            rectangleSketch.id, {10.0, -5.0}, {10.0, 5.0}).success);
        QVERIFY(rectangleController.addSketchLine(
            rectangleSketch.id, {10.0, 5.0}, {-10.0, 5.0}).success);
        QVERIFY(rectangleController.addSketchLine(
            rectangleSketch.id, {-10.0, 5.0}, {-10.0, -5.0}).success);
        const auto rectangleSketchFeature = rectangleController.body().findFeature(rectangleSketch.id);
        QVERIFY(rectangleSketchFeature->shape().ShapeType() == TopAbs_FACE);
        const auto rectangleExtrude = rectangleController.createExtrudeFromSketch({{
            {rectangleSketch.id, SelectionKind::Object, std::nullopt}}}, 8.0);
        QVERIFY2(rectangleExtrude.success,
            qPrintable(QString::fromStdString(rectangleExtrude.error)));
        QVERIFY(volume(rectangleController.body().findFeature(rectangleExtrude.id)->shape()) > 0.0);

        ModelingController openController;
        const auto openSketch = openController.createSketch();
        QVERIFY(openSketch.success);
        QVERIFY(openController.addSketchLine(
            openSketch.id, {0.0, 0.0}, {10.0, 0.0}).success);
        const auto openExtrude = openController.createExtrudeFromSketch({{
            {openSketch.id, SelectionKind::Object, std::nullopt}}}, 8.0);
        QVERIFY(!openExtrude.success);
        QVERIFY(openController.body().features().size() == 1);
    }

    void sweepUsesPersistentEdgeReference()
    {
        ModelingController controller;
        const auto sketch = controller.createSketch(SketchSupportType::YZ);
        QVERIFY(sketch.success);
        QVERIFY(controller.addSketchCircle(sketch.id, {0.0, 0.0}, 5.0).success);

        const auto path = controller.createBox();
        QVERIFY(path.success);
        const SelectionSnapshot profileSelection{{
            {sketch.id, SelectionKind::Object, std::nullopt}}};
        const auto pathEdge = edgeAtOriginAlongX(
            controller.body().findFeature(path.id)->shape());
        QVERIFY(pathEdge > 0);
        const SelectionSnapshot pathSelection{{
            {path.id, SelectionKind::Edge, pathEdge}}};
        const auto missingPath = controller.createSweep(profileSelection, {});
        QVERIFY(!missingPath.success);
        QVERIFY(controller.body().features().size() == 2);
        const auto sweep = controller.createSweep(profileSelection, pathSelection);
        QVERIFY2(sweep.success, qPrintable(QString::fromStdString(sweep.error)));
        const auto feature = std::dynamic_pointer_cast<SweepFeature>(
            controller.body().findFeature(sweep.id));
        QVERIFY(feature);
        QVERIFY(feature->pathOwner());
        QVERIFY(feature->pathReference());
        QCOMPARE(feature->pathReference()->featureId, path.id);
        QVERIFY(!feature->shape().IsNull());
        int solidCount = 0;
        for (TopExp_Explorer explorer(feature->shape(), TopAbs_SOLID);
             explorer.More(); explorer.Next()) ++solidCount;
        QVERIFY(solidCount > 0);
        QVERIFY(volume(feature->shape()) > 0.0);

        controller.undo();
        QVERIFY(!controller.body().findFeature(sweep.id));
        controller.redo();
        QVERIFY(controller.body().findFeature(sweep.id));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVERIFY2(ProjectFile::save(directory.filePath("sweep.pcad"),
            controller.document(), controller.body(), error), qPrintable(error));
        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(directory.filePath("sweep.pcad"),
            loadedDocument, loadedBody, error), qPrintable(error));
        const auto loaded = std::dynamic_pointer_cast<SweepFeature>(
            loadedBody.findFeature(sweep.id));
        QVERIFY(loaded);
        QVERIFY(loaded->pathReference());
        QVERIFY(!loaded->shape().IsNull());
        QVERIFY(volume(loaded->shape()) > 0.0);
    }

    void sweepUsesSketchPathLineArcChainAndPersistence()
    {
        ModelingController controller;
        const auto profile = controller.createSketch(SketchSupportType::YZ);
        QVERIFY(profile.success);
        QVERIFY(controller.addSketchCircle(profile.id, {0.0, 0.0}, 5.0).success);
        const auto path = controller.createSketch(SketchSupportType::XZ);
        QVERIFY(path.success);
        QVERIFY(controller.addSketchLine(path.id, {20.0, 10.0}, {30.0, 10.0}).success);
        QVERIFY(controller.addSketchArc(path.id, {10.0, 10.0}, {10.0, 0.0}, {20.0, 10.0}).success);
        QVERIFY(controller.addSketchLine(path.id, {0.0, 0.0}, {10.0, 0.0}).success);

        const auto pathFeature = std::dynamic_pointer_cast<SketchFeature>(
            controller.body().findFeature(path.id));
        QVERIFY(pathFeature);
        const auto built = cad::operations::SketchPathBuilder::build(*pathFeature);
        QCOMPARE(built.entityIds.size(), std::size_t{3});
        QVERIFY(!built.wire.IsNull());

        const auto result = controller.createSweep({{
            {profile.id, SelectionKind::Object, std::nullopt}}}, {{
            {path.id, SelectionKind::Object, std::nullopt}}});
        QVERIFY2(result.success, qPrintable(QString::fromStdString(result.error)));
        const auto sweep = std::dynamic_pointer_cast<SweepFeature>(
            controller.body().findFeature(result.id));
        QVERIFY(sweep);
        QCOMPARE(sweep->pathDefinition().type, SweepPathType::SketchPath);
        QCOMPARE(sweep->pathDefinition().sketchFeatureId, path.id);
        QCOMPARE(sweep->pathDefinition().entityIds.size(), std::size_t{3});
        int solidCount = 0;
        for (TopExp_Explorer explorer(sweep->shape(), TopAbs_SOLID);
             explorer.More(); explorer.Next()) ++solidCount;
        QCOMPARE(solidCount, 1);
        QVERIFY(volume(sweep->shape()) > 0.0);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto file = directory.filePath("sketch-path-sweep.pcad");
        QVERIFY2(ProjectFile::save(file, controller.document(), controller.body(), error),
            qPrintable(error));
        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(file, loadedDocument, loadedBody, error),
            qPrintable(error));
        const auto loaded = std::dynamic_pointer_cast<SweepFeature>(
            loadedBody.findFeature(result.id));
        QVERIFY(loaded);
        QCOMPARE(loaded->pathDefinition().type, SweepPathType::SketchPath);
        QCOMPARE(loaded->pathDefinition().entityIds, sweep->pathDefinition().entityIds);
        QVERIFY(volume(loaded->shape()) > 0.0);

        controller.undo();
        QVERIFY(!controller.body().findFeature(result.id));
        controller.redo();
        QVERIFY(controller.body().findFeature(result.id));
    }

    void sweepUsesSingleSketchLineAndArcPaths()
    {
        const auto makeProfile = [](ModelingController& controller) {
            const auto profile = controller.createSketch(SketchSupportType::YZ);
            if (!profile.success) return profile;
            if (!controller.addSketchCircle(profile.id, {0.0, 0.0}, 5.0).success)
                return ModelingResult{false, {}, "profile creation failed"};
            return profile;
        };

        ModelingController lineController;
        const auto lineProfile = makeProfile(lineController);
        QVERIFY(lineProfile.success);
        const auto linePath = lineController.createSketch(SketchSupportType::XZ);
        QVERIFY(linePath.success);
        QVERIFY(lineController.addSketchLine(
            linePath.id, {0.0, 0.0}, {100.0, 0.0}).success);
        const auto lineSweep = lineController.createSweep({{
            {lineProfile.id, SelectionKind::Object, std::nullopt}}}, {{
            {linePath.id, SelectionKind::Object, std::nullopt}}});
        QVERIFY2(lineSweep.success, qPrintable(QString::fromStdString(lineSweep.error)));
        const auto lineFeature = std::dynamic_pointer_cast<SketchFeature>(
            lineController.body().findFeature(linePath.id));
        QVERIFY(lineFeature);
        const auto beforeEdit = volume(lineController.body().findFeature(lineSweep.id)->shape());
        const auto line = std::get<SketchLine>(lineFeature->entities().front());
        lineFeature->replaceEntities(0, 1, {SketchLine{line.start, {60.0, 0.0}, line.id, false}});
        lineController.body().markDirtyFrom(linePath.id);
        QVERIFY(lineController.body().recompute());
        const auto afterEdit = volume(lineController.body().findFeature(lineSweep.id)->shape());
        QVERIFY(afterEdit > 0.0);
        QVERIFY(afterEdit < beforeEdit);

        ModelingController arcController;
        const auto arcProfile = makeProfile(arcController);
        QVERIFY(arcProfile.success);
        const auto arcPath = arcController.createSketch(SketchSupportType::XZ);
        QVERIFY(arcPath.success);
        QVERIFY(arcController.addSketchArc(
            arcPath.id, {0.0, 10.0}, {0.0, 0.0}, {10.0, 10.0}).success);
        const auto arcSweep = arcController.createSweep({{
            {arcProfile.id, SelectionKind::Object, std::nullopt}}}, {{
            {arcPath.id, SelectionKind::Object, std::nullopt}}});
        QVERIFY2(arcSweep.success, qPrintable(QString::fromStdString(arcSweep.error)));
        QVERIFY(volume(arcController.body().findFeature(arcSweep.id)->shape()) > 0.0);
    }

    void sketchPathBuilderRejectsDisconnectedBranchesAndConstructionGeometry()
    {
        ModelingController controller;
        const auto sketch = controller.createSketch(SketchSupportType::XZ);
        QVERIFY(sketch.success);
        QVERIFY(controller.addSketchLine(sketch.id, {0.0, 0.0}, {10.0, 0.0}).success);
        QVERIFY(controller.addSketchLine(sketch.id, {20.0, 0.0}, {30.0, 0.0}).success);
        const auto feature = std::dynamic_pointer_cast<SketchFeature>(
            controller.body().findFeature(sketch.id));
        QVERIFY(feature);
        QVERIFY_EXCEPTION_THROWN(cad::operations::SketchPathBuilder::build(*feature),
            std::runtime_error);

        controller.undo();
        controller.undo();
        QVERIFY(controller.addSketchLine(sketch.id, {10.0, 0.0}, {10.0, 10.0}).success);
        QVERIFY(controller.addSketchLine(sketch.id, {10.0, 0.0}, {10.0, -10.0}).success);
        QVERIFY(controller.addSketchLine(sketch.id, {10.0, 0.0}, {20.0, 0.0}).success);
        QVERIFY_EXCEPTION_THROWN(cad::operations::SketchPathBuilder::build(*feature),
            std::runtime_error);

        controller.undo();
        controller.undo();
        controller.undo();
        QVERIFY(controller.addSketchLine(sketch.id, {0.0, 0.0}, {10.0, 0.0}).success);
        QVERIFY(controller.addSketchLine(
            sketch.id, {100.0, 100.0}, {110.0, 100.0}, true).success);
        const auto valid = cad::operations::SketchPathBuilder::build(*feature);
        QCOMPARE(valid.entityIds.size(), std::size_t{1});
    }

    void sketchProfileBuilderSupportsHolesAndDisconnectedRegions()
    {
        ModelingController controller;
        const auto sketch = controller.createSketch();
        QVERIFY(sketch.success);
        const auto addLine = [&controller, &sketch](gp_Pnt2d start, gp_Pnt2d end) {
            return controller.addSketchLine(sketch.id, start, end).success;
        };
        QVERIFY(addLine({-40.0, -30.0}, {40.0, -30.0}));
        QVERIFY(addLine({40.0, -30.0}, {40.0, 30.0}));
        QVERIFY(addLine({40.0, 30.0}, {-40.0, 30.0}));
        QVERIFY(addLine({-40.0, 30.0}, {-40.0, -30.0}));
        QVERIFY(controller.addSketchCircle(sketch.id, {0.0, 0.0}, 10.0).success);
        QVERIFY(controller.addSketchCircle(sketch.id, {20.0, 0.0}, 5.0).success);

        const auto sketchFeature = controller.body().findFeature(sketch.id);
        const auto profile = cad::operations::SketchProfileBuilder::build(
            *std::dynamic_pointer_cast<SketchFeature>(sketchFeature));
        QCOMPARE(profile.faces.size(), std::size_t{1});
        int wireCount = 0;
        for (TopExp_Explorer explorer(profile.faces.front(), TopAbs_WIRE);
             explorer.More(); explorer.Next()) ++wireCount;
        QCOMPARE(wireCount, 3);

        const auto extrude = controller.createExtrudeFromSketch({{
            {sketch.id, SelectionKind::Object, std::nullopt}}}, 8.0);
        QVERIFY2(extrude.success,
            qPrintable(QString::fromStdString(extrude.error)));
        const double expectedVolume = (80.0 * 60.0
            - std::acos(-1.0) * (10.0 * 10.0 + 5.0 * 5.0)) * 8.0;
        QVERIFY(std::abs(volume(controller.body().findFeature(extrude.id)->shape())
            - expectedVolume) < 1.0e-5);

        ModelingController invalidLoops;
        const auto invalidSketch = invalidLoops.createSketch();
        QVERIFY(invalidSketch.success);
        QVERIFY(invalidLoops.addSketchCircle(invalidSketch.id, {0.0, 0.0}, 10.0).success);
        QVERIFY(invalidLoops.addSketchCircle(invalidSketch.id, {15.0, 0.0}, 10.0).success);
        const auto invalidFeature = std::dynamic_pointer_cast<SketchFeature>(
            invalidLoops.body().findFeature(invalidSketch.id));
        QVERIFY_EXCEPTION_THROWN(
            cad::operations::SketchProfileBuilder::build(*invalidFeature),
            std::runtime_error);

        ModelingController disconnected;
        const auto separate = disconnected.createSketch();
        QVERIFY(separate.success);
        const auto addSeparateRectangle = [&disconnected, &separate](double x) {
            return disconnected.addSketchLine(separate.id, {x, -5.0}, {x + 10.0, -5.0}).success
                && disconnected.addSketchLine(separate.id, {x + 10.0, -5.0}, {x + 10.0, 5.0}).success
                && disconnected.addSketchLine(separate.id, {x + 10.0, 5.0}, {x, 5.0}).success
                && disconnected.addSketchLine(separate.id, {x, 5.0}, {x, -5.0}).success;
        };
        QVERIFY(addSeparateRectangle(-30.0));
        QVERIFY(addSeparateRectangle(10.0));
        const auto separateFeature = std::dynamic_pointer_cast<SketchFeature>(
            disconnected.body().findFeature(separate.id));
        const auto separateProfile = cad::operations::SketchProfileBuilder::build(*separateFeature);
        QCOMPARE(separateProfile.faces.size(), std::size_t{2});
        const auto separateExtrude = disconnected.createExtrudeFromSketch({{
            {separate.id, SelectionKind::Object, std::nullopt}}}, 8.0);
        QVERIFY(separateExtrude.success);
        int solidCount = 0;
        for (TopExp_Explorer explorer(
                 disconnected.body().findFeature(separateExtrude.id)->shape(), TopAbs_SOLID);
             explorer.More(); explorer.Next()) ++solidCount;
        QCOMPARE(solidCount, 2);

        ModelingController pocketController;
        const auto box = pocketController.createBox();
        QVERIFY(box.success);
        const auto attached = pocketController.createSketchOnFace({{
            {box.id, SelectionKind::Face, 1}}});
        QVERIFY(attached.success);
        const auto addPocketLine = [&pocketController, &attached](gp_Pnt2d start, gp_Pnt2d end) {
            return pocketController.addSketchLine(attached.id, start, end).success;
        };
        QVERIFY(addPocketLine({-30.0, -20.0}, {30.0, -20.0}));
        QVERIFY(addPocketLine({30.0, -20.0}, {30.0, 20.0}));
        QVERIFY(addPocketLine({30.0, 20.0}, {-30.0, 20.0}));
        QVERIFY(addPocketLine({-30.0, 20.0}, {-30.0, -20.0}));
        QVERIFY(pocketController.addSketchCircle(attached.id, {0.0, 0.0}, 8.0).success);
        const double boxVolume = volume(pocketController.body().findFeature(box.id)->shape());
        const auto pocket = pocketController.createPocketFromSketch({{
            {attached.id, SelectionKind::Object, std::nullopt}}}, 10.0);
        QVERIFY2(pocket.success, qPrintable(QString::fromStdString(pocket.error)));
        QVERIFY(volume(pocketController.body().findFeature(pocket.id)->shape()) < boxVolume);
    }

    void constructionGeometryDoesNotAffectProfilesAndIsPersistent()
    {
        ModelingController controller;
        const auto sketch = controller.createSketch();
        QVERIFY(sketch.success);
        QVERIFY(controller.addSketchLine(sketch.id, {0.0, -10.0}, {20.0, -10.0}).success);
        QVERIFY(controller.addSketchLine(sketch.id, {20.0, -10.0}, {20.0, 10.0}).success);
        QVERIFY(controller.addSketchLine(sketch.id, {20.0, 10.0}, {0.0, 10.0}).success);
        QVERIFY(controller.addSketchLine(sketch.id, {0.0, 10.0}, {0.0, -10.0}).success);
        QVERIFY(controller.addSketchLine(sketch.id, {0.0, -30.0}, {0.0, 30.0}).success);
        const auto feature = std::dynamic_pointer_cast<SketchFeature>(
            controller.body().findFeature(sketch.id));
        QVERIFY(feature);
        const auto axisId = std::get<SketchLine>(feature->entities().back()).id;
        QVERIFY(controller.toggleSketchEntityConstruction(sketch.id, axisId).success);
        QCOMPARE(std::get<SketchLine>(feature->entities().back()).id, axisId);
        QVERIFY(std::get<SketchLine>(feature->entities().back()).construction);
        QCOMPARE(cad::operations::SketchProfileBuilder::build(*feature).faces.size(), std::size_t{1});
        QVERIFY(controller.actionState({sketch.id}).canRevolve);
        QVERIFY(controller.actionState(SelectionSnapshot{{
            {sketch.id, SelectionKind::Object, std::nullopt}}}).canRevolve);

        controller.undo();
        QVERIFY(!std::get<SketchLine>(feature->entities().back()).construction);
        QVERIFY(!controller.actionState({sketch.id}).canRevolve);
        QVERIFY_EXCEPTION_THROWN(cad::operations::SketchProfileBuilder::build(*feature),
            std::runtime_error);
        controller.redo();
        QVERIFY(std::get<SketchLine>(feature->entities().back()).construction);
        QVERIFY(controller.createExtrudeFromSketch({{
            {sketch.id, SelectionKind::Object, std::nullopt}}}, 5.0).success);

        cad::parametric::RevolveAxisDefinition axis;
        axis.type = RevolveAxisType::SketchLine;
        axis.sketchFeatureId = sketch.id;
        axis.sketchLineId = axisId;
        const auto revolve = controller.createRevolve({{
            {sketch.id, SelectionKind::Object, std::nullopt}}}, axis, 180.0);
        QVERIFY2(revolve.success, qPrintable(QString::fromStdString(revolve.error)));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto path = directory.filePath("construction.pcad");
        QVERIFY2(ProjectFile::save(path, controller.document(), controller.body(), error),
            qPrintable(error));
        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loadedDocument, loadedBody, error),
            qPrintable(error));
        const auto loadedSketch = std::dynamic_pointer_cast<SketchFeature>(
            loadedBody.findFeature(sketch.id));
        QVERIFY(loadedSketch);
        const auto loadedAxis = std::find_if(loadedSketch->entities().begin(),
            loadedSketch->entities().end(), [&axisId](const auto& entity) {
                return std::visit([&axisId](const auto& value) { return value.id == axisId; }, entity);
            });
        QVERIFY(loadedAxis != loadedSketch->entities().end());
        QVERIFY(std::visit([](const auto& value) { return value.construction; }, *loadedAxis));
        QCOMPARE(cad::operations::SketchProfileBuilder::build(*loadedSketch).faces.size(),
            std::size_t{1});
    }

    void sketchArcMixedProfileIsParametricAndPersistent()
    {
        ModelingController controller;
        const auto sketch = controller.createSketch();
        QVERIFY(sketch.success);
        // The four entities form a capsule/slot. Input order is intentionally shuffled.
        QVERIFY(controller.addSketchArc(
            sketch.id, {0.0, -20.0}, {-10.0, -20.0}, {10.0, -20.0}).success);
        QVERIFY(controller.addSketchLine(
            sketch.id, {10.0, -20.0}, {10.0, 0.0}).success);
        QVERIFY(controller.addSketchArc(
            sketch.id, {0.0, 0.0}, {10.0, 0.0}, {-10.0, 0.0}).success);
        QVERIFY(controller.addSketchLine(
            sketch.id, {-10.0, 0.0}, {-10.0, -20.0}).success);
        QVERIFY(controller.addSketchCircle(sketch.id, {0.0, -10.0}, 2.0).success);

        const auto sketchFeature = std::dynamic_pointer_cast<SketchFeature>(
            controller.body().findFeature(sketch.id));
        QCOMPARE(sketchFeature->entityCount(), std::size_t{5});
        const auto& arc = std::get<SketchArc>(sketchFeature->entities().front());
        QVERIFY(std::abs(arc.startPoint().X() + 10.0) < 1.0e-9);
        QVERIFY(std::abs(arc.endPoint().X() - 10.0) < 1.0e-9);
        QVERIFY(std::abs(std::abs(arc.signedSweep()) - std::acos(-1.0)) < 1.0e-9);

        controller.undo();
        QCOMPARE(sketchFeature->entityCount(), std::size_t{4});
        controller.redo();
        QCOMPARE(sketchFeature->entityCount(), std::size_t{5});

        const auto profile = cad::operations::SketchProfileBuilder::build(*sketchFeature);
        QCOMPARE(profile.faces.size(), std::size_t{1});
        const auto extrude = controller.createExtrudeFromSketch({{
            {sketch.id, SelectionKind::Object, std::nullopt}}}, 8.0);
        QVERIFY2(extrude.success, qPrintable(QString::fromStdString(extrude.error)));
        QVERIFY(volume(controller.body().findFeature(extrude.id)->shape()) > 0.0);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto path = directory.filePath("arc-sketch.pcad");
        QVERIFY2(ProjectFile::save(path, controller.document(), controller.body(), error),
            qPrintable(error));
        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loadedDocument, loadedBody, error),
            qPrintable(error));
        const auto loadedSketch = std::dynamic_pointer_cast<SketchFeature>(
            loadedBody.findFeature(sketch.id));
        QVERIFY(loadedSketch);
        QCOMPARE(loadedSketch->entityCount(), std::size_t{5});
        QVERIFY(!loadedBody.findFeature(extrude.id)->shape().IsNull());
    }

    void projectControllerReplacesOnlyAfterValidLoad()
    {
        ModelingController controller;
        ProjectController projects(controller);
        QVERIFY(controller.createBox().success);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto path = directory.filePath("controller.pcad");
        QVERIFY(projects.save(path, error));
        QVERIFY(controller.undoStack().isClean());

        QVERIFY(controller.createCylinder().success);
        QVERIFY(!controller.undoStack().isClean());
        QVERIFY(projects.open(path, error));
        QCOMPARE(controller.body().features().size(), std::size_t{1});
        QVERIFY(controller.undoStack().isClean());

        const auto invalid = directory.filePath("invalid.pcad");
        QFile file(invalid);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a project");
        file.close();
        QVERIFY(!projects.open(invalid, error));
        QCOMPARE(controller.body().features().size(), std::size_t{1});
    }

    void propertyEditingIsUndoableAndValidated()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);

        const auto initial = controller.features().front();
        const auto width = std::find_if(initial.properties.begin(),
            initial.properties.end(), [](const auto& property) {
                return property.key == "width";
            });
        QVERIFY(width != initial.properties.end());
        QCOMPARE(std::get<double>(width->value), 100.0);

        const auto changed = controller.setFeatureProperty(box.id, "width", 42.0);
        QVERIFY(changed.success);
        QCOMPARE(std::get<double>(controller.features().front().properties.front().value),
                 42.0);
        QVERIFY(controller.body().findFeature(box.id)->state()
                == FeatureState::UpToDate);

        controller.undo();
        QCOMPARE(std::get<double>(controller.features().front().properties.front().value),
                 100.0);
        controller.redo();
        QCOMPARE(std::get<double>(controller.features().front().properties.front().value),
                 42.0);

        QVERIFY(!controller.setFeatureProperty(box.id, "missing", 1.0).success);
        QVERIFY(!controller.setFeatureProperty(box.id, "width", 0.0).success);
        QVERIFY(!controller.setFeatureProperty(box.id, "width", std::string("42")).success);
    }

    void sketchFacesCanBePushPulled()
    {
        ModelingController circleController;
        const auto circleSketch = circleController.createSketch();
        QVERIFY(circleSketch.success);
        QVERIFY(circleController.addSketchCircle(
            circleSketch.id, {20.0, 20.0}, 8.0).success);
        const auto circle = circleController.body().findFeature(circleSketch.id);
        QVERIFY(circle && circle->shape().ShapeType() == TopAbs_FACE);
        const auto circlePushPull = circleController.pushPull(
            circleSketch.id, 1, {0.0, 0.0, 1.0}, 10.0);
        QVERIFY2(circlePushPull.success, qPrintable(QString::fromStdString(circlePushPull.error)));
        QVERIFY(circleController.body().findFeature(circlePushPull.id));
        circleController.undo();
        QVERIFY(!circleController.body().findFeature(circlePushPull.id));
        circleController.redo();
        QVERIFY(circleController.body().findFeature(circlePushPull.id));

        ModelingController rectangleController;
        const auto rectangleSketch = rectangleController.createSketch();
        QVERIFY(rectangleSketch.success);
        const std::array<gp_Pnt2d, 4> corners{
            gp_Pnt2d(0.0, 0.0), gp_Pnt2d(20.0, 0.0),
            gp_Pnt2d(20.0, 12.0), gp_Pnt2d(0.0, 12.0)};
        for (int index = 0; index < 4; ++index) {
            QVERIFY(rectangleController.addSketchLine(
                rectangleSketch.id, corners[index], corners[(index + 1) % 4]).success);
        }
        const auto rectangle = rectangleController.body().findFeature(rectangleSketch.id);
        QVERIFY(rectangle && rectangle->shape().ShapeType() == TopAbs_FACE);
        const auto rectanglePushPull = rectangleController.pushPull(
            rectangleSketch.id, 1, {0.0, 0.0, 1.0}, 10.0);
        QVERIFY2(rectanglePushPull.success,
                 qPrintable(QString::fromStdString(rectanglePushPull.error)));

        ModelingController openController;
        const auto openSketch = openController.createSketch();
        QVERIFY(openSketch.success);
        QVERIFY(openController.addSketchLine(
            openSketch.id, {0.0, 0.0}, {20.0, 0.0}).success);
        const auto open = openController.body().findFeature(openSketch.id);
        QVERIFY(open && open->shape().ShapeType() == TopAbs_WIRE);
        const auto rejected = openController.pushPull(
            openSketch.id, 1, {0.0, 0.0, 1.0}, 10.0);
        QVERIFY(!rejected.success);
        QVERIFY(!openController.body().findFeature(rejected.id));
        const auto missing = openController.pushPull(
            "missing-sketch", 1, {0.0, 0.0, 1.0}, 10.0);
        QVERIFY(!missing.success);
    }

    void pushPullIsUndoableAndRedoable()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto initialVolume = volume(controller.body().shape());

        const auto firstPick = firstPlanarFace(controller.body().findFeature(box.id)->shape());
        QVERIFY(firstPick.index > 0);
        const auto first = controller.pushPull(box.id, firstPick.index, firstPick.normal, 10.0);
        QVERIFY(first.success);
        QCOMPARE(controller.undoStack().count(), 2);
        const auto firstFeature = controller.body().findFeature(first.id);
        QVERIFY(firstFeature);
        const auto firstVolume = volume(firstFeature->shape());

        const auto secondPick = firstPlanarFace(firstFeature->shape());
        QVERIFY(secondPick.index > 0);
        const auto second = controller.pushPull(
            first.id, secondPick.index, secondPick.normal, 5.0);
        QVERIFY(second.success);
        QCOMPARE(controller.undoStack().count(), 3);
        const auto secondFeature = controller.body().findFeature(second.id);
        QVERIFY(secondFeature);
        const auto secondVolume = volume(secondFeature->shape());
        QVERIFY(std::abs(secondVolume - firstVolume) > 1.0e-6);

        controller.undo();
        QVERIFY(!controller.body().findFeature(second.id));
        QVERIFY(controller.body().findFeature(first.id));
        QVERIFY(std::abs(volume(controller.body().findFeature(first.id)->shape())
                         - firstVolume) < 1.0e-6);

        controller.undo();
        QVERIFY(!controller.body().findFeature(first.id));
        QVERIFY(controller.body().findFeature(box.id));
        QVERIFY(std::abs(volume(controller.body().shape()) - initialVolume) < 1.0e-6);
        QCOMPARE(controller.undoStack().count(), 3);

        controller.redo();
        QVERIFY(controller.body().findFeature(first.id));
        QVERIFY(std::abs(volume(controller.body().findFeature(first.id)->shape())
                         - firstVolume) < 1.0e-6);
        controller.redo();
        QVERIFY(controller.body().findFeature(second.id));
        QVERIFY(std::abs(volume(controller.body().findFeature(second.id)->shape())
                         - secondVolume) < 1.0e-6);
        QCOMPARE(controller.undoStack().index(), 3);
    }

    void persistentTopologySurvivesDetailAndShellResize()
    {
        ModelingController filletController;
        const auto box = filletController.createBox();
        QVERIFY(box.success);
        const auto fillet = filletController.createFillet(
            SelectionSnapshot{{{box.id, SelectionKind::Edge, 1}}}, 1.0);
        QVERIFY2(fillet.success, qPrintable(QString::fromStdString(fillet.error)));
        const auto filletFeature = std::dynamic_pointer_cast<FilletFeature>(
            filletController.body().findFeature(fillet.id));
        QVERIFY(filletFeature);
        QVERIFY(filletFeature->references().front().signature.has_value());
        QVERIFY(filletController.setFeatureProperty(box.id, "width", 140.0).success);
        QVERIFY(filletFeature->state() == FeatureState::UpToDate);
        QVERIFY(!filletFeature->shape().IsNull());
        filletController.undo();
        QVERIFY(filletFeature->state() == FeatureState::UpToDate);
        filletController.redo();
        QVERIFY(filletFeature->state() == FeatureState::UpToDate);

        ModelingController chamferController;
        const auto chamferBox = chamferController.createBox();
        QVERIFY(chamferBox.success);
        const auto chamfer = chamferController.createChamfer(
            SelectionSnapshot{{{chamferBox.id, SelectionKind::Edge, 1}}}, 1.0);
        QVERIFY2(chamfer.success, qPrintable(QString::fromStdString(chamfer.error)));
        const auto chamferFeature = std::dynamic_pointer_cast<ChamferFeature>(
            chamferController.body().findFeature(chamfer.id));
        QVERIFY(chamferFeature);
        QVERIFY(chamferFeature->references().front().signature.has_value());
        QVERIFY(chamferController.setFeatureProperty(chamferBox.id, "height", 45.0).success);
        QVERIFY(chamferFeature->state() == FeatureState::UpToDate);
        QVERIFY(!chamferFeature->shape().IsNull());
        chamferController.undo();
        chamferController.redo();
        QVERIFY(chamferFeature->state() == FeatureState::UpToDate);

        ModelingController pushPullController;
        const auto pushBox = pushPullController.createBox();
        QVERIFY(pushBox.success);
        const auto pushPull = pushPullController.pushPull(
            pushBox.id, 1, {0.0, 0.0, 1.0}, 2.0);
        QVERIFY2(pushPull.success, qPrintable(QString::fromStdString(pushPull.error)));
        const auto pushFeature = std::dynamic_pointer_cast<PushPullFeature>(
            pushPullController.body().findFeature(pushPull.id));
        QVERIFY(pushFeature);
        QVERIFY(pushFeature->faceReference().signature.has_value());
        QVERIFY(pushPullController.setFeatureProperty(pushBox.id, "width", 130.0).success);
        QVERIFY(pushFeature->state() == FeatureState::UpToDate);
        QVERIFY(!pushFeature->shape().IsNull());
    }

    void pushPullDistanceEditIsUndoableAndPersistent()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto pick = firstPlanarFace(controller.body().findFeature(box.id)->shape());
        const auto created = controller.pushPull(box.id, pick.index, pick.normal, 20.0);
        QVERIFY2(created.success, qPrintable(QString::fromStdString(created.error)));
        const auto feature = std::dynamic_pointer_cast<PushPullFeature>(
            controller.body().findFeature(created.id));
        QVERIFY(feature);
        const auto reference = feature->faceReference();

        QVERIFY(controller.setFeatureProperty(created.id, "distance", 35.0).success);
        QCOMPARE(feature->distance(), 35.0);
        controller.undo();
        QCOMPARE(feature->distance(), 20.0);
        controller.redo();
        QCOMPARE(feature->distance(), 35.0);
        QCOMPARE(feature->faceReference().featureId, reference.featureId);
        QVERIFY(cad::topology::toJson(feature->faceReference())
                == cad::topology::toJson(reference));
    }

    void pushPullRoundTripsAndSurvivesSourceResize()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto pick = firstPlanarFace(controller.body().findFeature(box.id)->shape());
        const auto created = controller.pushPull(box.id, pick.index, pick.normal, 20.0);
        QVERIFY2(created.success, qPrintable(QString::fromStdString(created.error)));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto path = directory.filePath("push-pull.pcad");
        QVERIFY(ProjectFile::save(path, controller.document(), controller.body(), error));

        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loadedDocument, loadedBody, error), qPrintable(error));
        const auto loaded = std::dynamic_pointer_cast<PushPullFeature>(
            loadedBody.findFeature(created.id));
        QVERIFY(loaded);
        QVERIFY(loaded->faceReference().signature.has_value());
        QCOMPARE(loaded->distance(), 20.0);

        const auto loadedBox = std::dynamic_pointer_cast<BoxParametricFeature>(
            loadedBody.findFeature(box.id));
        QVERIFY(loadedBox);
        loadedBox->setSize(120.0, 90.0, 40.0);
        loadedBody.markDirtyFrom(box.id);
        QVERIFY2(loadedBody.recompute(), qPrintable(QString::fromStdString(loadedBody.lastError())));
        QCOMPARE(loaded->state(), FeatureState::UpToDate);
        QVERIFY(!loaded->shape().IsNull());
    }

    void pushPullFailsWhenReferencedFaceCannotBeResolved()
    {
        auto source = std::make_shared<BoxParametricFeature>("source", 100.0, 70.0, 30.0);
        QVERIFY(source->recompute());
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(source->shape(), TopAbs_FACE, faces);
        auto reference = cad::topology::TopologicalSignatureBuilder::createReference(
            source->id(), source->shape(), faces(1));
        reference.signature.reset();
        reference.transientIndex = 999;

        auto pushPull = std::make_shared<PushPullFeature>(
            "pushpull", source, reference, gp_Vec(0.0, 0.0, 1.0), 20.0);
        Body body;
        body.addFeature(source);
        body.addFeature(pushPull);
        QVERIFY(!body.recompute());
        QCOMPARE(pushPull->state(), FeatureState::Failed);
        QVERIFY(QString::fromStdString(pushPull->error()).contains("topolog"));
    }

    void revolveGlobalAxisIsParametricAndUndoable()
    {
        ModelingController controller;
        const auto sketch = controller.createSketch();
        QVERIFY(sketch.success);
        const SelectionSnapshot selection{{
            {sketch.id, SelectionKind::Object, std::nullopt}}};
        QVERIFY(controller.actionState(selection).canRevolve);

        RevolveAxisDefinition axis;
        axis.type = RevolveAxisType::GlobalY;
        const auto created = controller.createRevolve(selection, axis, 180.0);
        QVERIFY2(created.success, qPrintable(QString::fromStdString(created.error)));
        const auto feature = std::dynamic_pointer_cast<RevolveFeature>(
            controller.body().findFeature(created.id));
        QVERIFY(feature);
        QCOMPARE(feature->axisDefinition().type, RevolveAxisType::GlobalY);
        QVERIFY(!feature->shape().IsNull());
        QVERIFY(volume(feature->shape()) > 0.0);

        QVERIFY(controller.setFeatureProperty(created.id, "angleDegrees", 270.0).success);
        QCOMPARE(feature->angleRadians(), 270.0 * std::acos(-1.0) / 180.0);
        controller.undo();
        QCOMPARE(feature->angleRadians(), std::acos(-1.0));
        controller.redo();
        QCOMPARE(feature->angleRadians(), 270.0 * std::acos(-1.0) / 180.0);

        controller.undo();
        controller.undo();
        QVERIFY(!controller.body().findFeature(created.id));
        controller.redo();
        QVERIFY(controller.body().findFeature(created.id));

        QVERIFY(controller.setFeatureProperty(sketch.id, "width", 120.0).success);
        const auto updated = std::dynamic_pointer_cast<RevolveFeature>(
            controller.body().findFeature(created.id));
        QVERIFY(updated && updated->state() == FeatureState::UpToDate);
        QVERIFY(volume(updated->shape()) > 0.0);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto path = directory.filePath("revolve-global.pcad");
        QVERIFY2(ProjectFile::save(path, controller.document(), controller.body(), error),
                 qPrintable(error));
        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loadedDocument, loadedBody, error), qPrintable(error));
        const auto loaded = std::dynamic_pointer_cast<RevolveFeature>(
            loadedBody.findFeature(created.id));
        QVERIFY(loaded && !loaded->shape().IsNull());
        QCOMPARE(loaded->axisDefinition().type, RevolveAxisType::GlobalY);
        QCOMPARE(loaded->angleRadians(), std::acos(-1.0));
    }

    void revolveSketchLineAxisUsesStableEntityIdAndRoundTrips()
    {
        const std::vector<SketchEntity> entities{
            SketchLine{{0.0, 0.0}, {0.0, 20.0}, "turning-axis"},
            SketchLine{{0.0, 20.0}, {10.0, 20.0}, "top"},
            SketchLine{{10.0, 20.0}, {10.0, 0.0}, "side"},
            SketchLine{{10.0, 0.0}, {0.0, 0.0}, "bottom"}};
        auto sketch = std::make_shared<SketchFeature>(
            "revolve-sketch", SketchSupportType::XY, 100.0, 60.0, entities);
        RevolveAxisDefinition axis;
        axis.type = RevolveAxisType::SketchLine;
        axis.sketchFeatureId = sketch->id();
        axis.sketchLineId = "turning-axis";
        auto revolve = std::make_shared<RevolveFeature>(
            "revolve-line", sketch, axis, 360.0 * std::acos(-1.0) / 180.0);
        Body body;
        body.addFeature(sketch);
        body.addFeature(revolve);
        QVERIFY2(body.recompute(), qPrintable(QString::fromStdString(body.lastError())));
        QVERIFY(revolve->state() == FeatureState::UpToDate);
        QVERIFY(!revolve->shape().IsNull());
        QVERIFY(volume(revolve->shape()) > 0.0);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto path = directory.filePath("revolve.pcad");
        Document document;
        QVERIFY2(ProjectFile::save(path, document, body, error), qPrintable(error));
        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loadedDocument, loadedBody, error), qPrintable(error));
        const auto loaded = std::dynamic_pointer_cast<RevolveFeature>(
            loadedBody.findFeature("revolve-line"));
        QVERIFY(loaded);
        QCOMPARE(loaded->axisDefinition().type, RevolveAxisType::SketchLine);
        QCOMPARE(loaded->axisDefinition().sketchLineId, std::string("turning-axis"));
        QVERIFY(!loaded->shape().IsNull());

        auto loadedSketch = std::dynamic_pointer_cast<SketchFeature>(
            loadedBody.findFeature("revolve-sketch"));
        QVERIFY(loadedSketch);
        loadedSketch->replaceEntities(0, 1, {});
        loadedBody.markDirtyFrom(loadedSketch->id());
        QVERIFY(!loadedBody.recompute());
        QCOMPARE(loaded->state(), FeatureState::Failed);
        QVERIFY(QString::fromStdString(loaded->error()).contains("Sketch line"));
    }

    void revolveSupportsFaceAttachedSketchProfiles()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto sketch = controller.createSketchOnFace(SelectionSnapshot{{
            {box.id, SelectionKind::Face, 1}}});
        QVERIFY(sketch.success);
        QVERIFY(controller.addSketchLine(sketch.id, {0.0, 0.0}, {20.0, 0.0}).success);
        QVERIFY(controller.addSketchLine(sketch.id, {20.0, 0.0}, {20.0, 10.0}).success);
        QVERIFY(controller.addSketchLine(sketch.id, {20.0, 10.0}, {0.0, 10.0}).success);
        QVERIFY(controller.addSketchLine(sketch.id, {0.0, 10.0}, {0.0, 0.0}).success);
        const SelectionSnapshot selection{{
            {sketch.id, SelectionKind::Object, std::nullopt}}};
        QVERIFY(controller.actionState(selection).canRevolve);
        RevolveAxisDefinition axis;
        axis.type = RevolveAxisType::GlobalZ;
        const auto result = controller.createRevolve(selection, axis, 180.0);
        QVERIFY2(result.success, qPrintable(QString::fromStdString(result.error)));
        const auto revolve = controller.body().findFeature(result.id);
        QVERIFY(revolve && !revolve->shape().IsNull());
    }

    void revolveModelEdgeAxisIsPersistentAndLinear()
    {
        auto axisOwner = std::make_shared<BoxParametricFeature>(
            "axis-owner", 20.0, 20.0, 20.0);
        QVERIFY(axisOwner->recompute());
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(axisOwner->shape(), TopAbs_EDGE, edges);
        QVERIFY(edges.Extent() > 0);
        const auto edgeReference = cad::topology::TopologicalSignatureBuilder::createReference(
            axisOwner->id(), axisOwner->shape(), TopoDS::Edge(edges.FindKey(1)));
        const std::vector<SketchEntity> entities{
            SketchLine{{0.0, 0.0}, {0.0, 10.0}, "a"},
            SketchLine{{0.0, 10.0}, {8.0, 10.0}, "b"},
            SketchLine{{8.0, 10.0}, {8.0, 0.0}, "c"},
            SketchLine{{8.0, 0.0}, {0.0, 0.0}, "d"}};
        auto sketch = std::make_shared<SketchFeature>(
            "edge-axis-profile", SketchSupportType::XY, 40.0, 40.0, entities);
        RevolveAxisDefinition axis;
        axis.type = RevolveAxisType::ModelEdge;
        axis.edgeFeatureId = axisOwner->id();
        axis.edgeReference = edgeReference;
        auto revolve = std::make_shared<RevolveFeature>(
            "edge-axis-revolve", sketch, axis, std::acos(-1.0), axisOwner);
        Body body;
        body.addFeature(axisOwner);
        body.addFeature(sketch);
        body.addFeature(revolve);
        QVERIFY2(body.recompute(), qPrintable(QString::fromStdString(body.lastError())));
        QVERIFY(revolve->state() == FeatureState::UpToDate);
        QVERIFY(!revolve->shape().IsNull());
        QCOMPARE(revolve->axisDefinition().edgeFeatureId, axisOwner->id());
        QVERIFY(revolve->axisDefinition().edgeReference.has_value());
    }

    void revolveAxisSelectionResolvesStableSketchAndModelReferences()
    {
        ModelingController controller;
        const auto sketchResult = controller.createSketch();
        QVERIFY(sketchResult.success);
        QVERIFY(controller.addSketchLine(sketchResult.id, {0, 0}, {20, 0}).success);
        QVERIFY(controller.addSketchLine(sketchResult.id, {20, 0}, {20, 10}).success);
        QVERIFY(controller.addSketchLine(sketchResult.id, {20, 10}, {0, 10}).success);
        QVERIFY(controller.addSketchLine(sketchResult.id, {0, 10}, {0, 0}).success);
        const auto sketch = std::dynamic_pointer_cast<SketchFeature>(
            controller.body().findFeature(sketchResult.id));
        QVERIFY(sketch);
        QVERIFY(controller.body().recompute());

        TopTools_IndexedMapOfShape sketchEdges;
        TopExp::MapShapes(sketch->shape(), TopAbs_EDGE, sketchEdges);
        QVERIFY(sketchEdges.Extent() >= 4);
        const auto line = std::get<SketchLine>(sketch->entities().front());
        const auto frame = sketch->currentFrame();
        gp_Pnt start = frame.origin;
        start.Translate(gp_Vec(frame.xDirection) * line.start.X()
            + gp_Vec(frame.yDirection) * line.start.Y());
        gp_Pnt end = frame.origin;
        end.Translate(gp_Vec(frame.xDirection) * line.end.X()
            + gp_Vec(frame.yDirection) * line.end.Y());
        int lineIndex = 0;
        for (int index = 1; index <= sketchEdges.Extent(); ++index) {
            BRepAdaptor_Curve curve(TopoDS::Edge(sketchEdges.FindKey(index)));
            if (curve.GetType() != GeomAbs_Line) continue;
            const auto a = curve.Value(curve.FirstParameter());
            const auto b = curve.Value(curve.LastParameter());
            if ((a.Distance(start) < 1.0e-6 && b.Distance(end) < 1.0e-6)
                || (a.Distance(end) < 1.0e-6 && b.Distance(start) < 1.0e-6)) {
                lineIndex = index;
                break;
            }
        }
        QVERIFY(lineIndex > 0);
        RevolveAxisDefinition resolvedSketchAxis;
        QVERIFY(controller.resolveRevolveAxis(sketchResult.id,
            SelectionSnapshot{{{sketchResult.id, SelectionKind::Edge, lineIndex}}},
            resolvedSketchAxis).success);
        QCOMPARE(resolvedSketchAxis.type, RevolveAxisType::SketchLine);
        QCOMPARE(resolvedSketchAxis.sketchLineId, line.id);

        const auto box = controller.createBox();
        QVERIFY(box.success);
        RevolveAxisDefinition globalAxis;
        globalAxis.type = RevolveAxisType::GlobalY;
        const auto revolve = controller.createRevolve(
            SelectionSnapshot{{{sketchResult.id, SelectionKind::Object, std::nullopt}}},
            globalAxis, 180.0);
        QVERIFY(revolve.success);
        RevolveAxisDefinition modelAxis;
        QVERIFY(controller.resolveRevolveAxis(sketchResult.id,
            SelectionSnapshot{{{box.id, SelectionKind::Edge, 1}}}, modelAxis).success);
        QCOMPARE(modelAxis.type, RevolveAxisType::ModelEdge);
        QVERIFY(modelAxis.edgeReference.has_value());
        QVERIFY(controller.updateRevolveAxis(revolve.id, modelAxis).success);
        QCOMPARE(std::dynamic_pointer_cast<RevolveFeature>(
            controller.body().findFeature(revolve.id))->axisDefinition().type,
            RevolveAxisType::ModelEdge);
        controller.undo();
        QCOMPARE(std::dynamic_pointer_cast<RevolveFeature>(
            controller.body().findFeature(revolve.id))->axisDefinition().type,
            RevolveAxisType::GlobalY);
        controller.redo();
        QCOMPARE(std::dynamic_pointer_cast<RevolveFeature>(
            controller.body().findFeature(revolve.id))->axisDefinition().type,
            RevolveAxisType::ModelEdge);
    }

    void faceAttachedSketchRepeatedLineInsertionKeepsStableEntities()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto sketch = controller.createSketchOnFace(SelectionSnapshot{{
            {box.id, SelectionKind::Face, 1}}});
        QVERIFY(sketch.success);

        for (int index = 0; index < 100; ++index) {
            QVERIFY(controller.addSketchLine(sketch.id,
                {static_cast<double>(index), 0.0},
                {static_cast<double>(index), 10.0}).success);
        }
        const auto feature = std::dynamic_pointer_cast<SketchFeature>(
            controller.body().findFeature(sketch.id));
        QVERIFY(feature);
        QCOMPARE(feature->entityCount(), std::size_t(100));
        for (const auto& entity : feature->entities()) {
            QVERIFY(std::visit([](const auto& value) { return !value.id.empty(); }, entity));
        }
    }

    void shellUsesPersistentFaceReferencesAfterResize()
    {
        auto base = std::make_shared<BoxParametricFeature>("shell-base", 100.0, 70.0, 30.0);
        QVERIFY(base->recompute());
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(base->shape(), TopAbs_FACE, faces);
        QVERIFY(faces.Extent() == 6);
        const auto reference = cad::topology::TopologicalSignatureBuilder::createReference(
            base->id(), base->shape(), faces(1));
        auto shell = std::make_shared<ShellFeature>(
            "shell", base, std::vector<cad::topology::TopologicalReference>{reference}, 1.0);
        Body body;
        body.addFeature(base);
        body.addFeature(shell);
        QVERIFY(body.recompute());
        QVERIFY(shell->state() == FeatureState::UpToDate);
        QVERIFY(!shell->shape().IsNull());

        base->setSize(140.0, 70.0, 30.0);
        body.markDirtyFrom(base->id());
        QVERIFY(body.recompute());
        QVERIFY(shell->state() == FeatureState::UpToDate);
        QVERIFY(!shell->shape().IsNull());
        QCOMPARE(shell->faceReferences().size(), std::size_t{1});
    }

    void shellThicknessEditUsesCommonUndoPipeline()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto source = controller.body().findFeature(box.id);
        QVERIFY(source);
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(source->shape(), TopAbs_FACE, faces);
        const auto reference = cad::topology::TopologicalSignatureBuilder::createReference(
            box.id, source->shape(), TopoDS::Face(faces.FindKey(1)));
        auto shell = std::make_shared<ShellFeature>(
            "shell", std::dynamic_pointer_cast<ParametricFeature>(source),
            std::vector<cad::topology::TopologicalReference>{reference}, 1.0);
        controller.body().addFeature(shell);
        QVERIFY(controller.body().recompute());

        QVERIFY(controller.setFeatureProperty(shell->id(), "thickness", 2.5).success);
        QCOMPARE(shell->thickness(), 2.5);
        controller.undo();
        QCOMPARE(shell->thickness(), 1.0);
        controller.redo();
        QCOMPARE(shell->thickness(), 2.5);
    }

    void shellCreationUsesOnePersistentCommand()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        SelectionSnapshot selection;
        selection.items.push_back({box.id, SelectionKind::Object, std::nullopt});
        QVERIFY(controller.actionState(selection).canShell);

        const auto created = controller.createShell(selection, 1.5);
        QVERIFY2(created.success, qPrintable(QString::fromStdString(created.error)));
        const auto shell = std::dynamic_pointer_cast<ShellFeature>(
            controller.body().findFeature(created.id));
        QVERIFY(shell);
        QCOMPARE(shell->faceReferences().size(), std::size_t{1});
        QVERIFY(!shell->shape().IsNull());

        controller.undo();
        QVERIFY(!controller.body().findFeature(created.id));
        controller.redo();
        QVERIFY(controller.body().findFeature(created.id));
    }

    void transformIsOneUndoablePlacementChange()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto before = controller.body().findFeature(box.id)->placement();
        gp_Trsf after;
        after.SetTranslation(gp_Vec(12.0, -3.0, 7.0));

        const auto result = controller.transformFeature(box.id, before, after);
        QVERIFY(result.success);
        QCOMPARE(controller.undoStack().count(), 2);
        QCOMPARE(controller.body().findFeature(box.id)->placement().TranslationPart().X(), 12.0);

        controller.undo();
        QCOMPARE(controller.body().findFeature(box.id)->placement().Form(), gp_Identity);
        controller.redo();
        QCOMPARE(controller.body().findFeature(box.id)->placement().TranslationPart().Y(), -3.0);
        QCOMPARE(controller.body().findFeature(box.id)->placement().TranslationPart().Z(), 7.0);
    }

    void snappedTransformDeltaCommitsAndRestoresExactly()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);

        gp_Trsf rawDelta;
        rawDelta.SetTranslation(gp_Vec(7.0, 0.0, 0.0));
        gp_Trsf snapCorrection;
        snapCorrection.SetTranslation(gp_Vec(3.0, 0.0, 0.0));

        gp_Trsf snappedDelta = snapCorrection;
        snappedDelta.Multiply(rawDelta);

        const auto result = controller.transformFeatureDelta(box.id, snappedDelta);
        QVERIFY(result.success);
        QCOMPARE(controller.body().findFeature(box.id)->placement().TranslationPart().X(), 10.0);

        controller.undo();
        QCOMPARE(controller.body().findFeature(box.id)->placement().Form(), gp_Identity);
        controller.redo();
        QCOMPARE(controller.body().findFeature(box.id)->placement().TranslationPart().X(), 10.0);
    }

    void filletAndChamferUseTypedEdgeSelection()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto source = controller.body().findFeature(box.id);
        QVERIFY(source);

        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(source->shape(), TopAbs_EDGE, edges);
        QVERIFY(edges.Extent() >= 2);

        SelectionSnapshot edgeSelection{
            {{box.id, SelectionKind::Edge, 1},
             {box.id, SelectionKind::Edge, 2}}};
        const auto actionState = controller.actionState(edgeSelection);
        QVERIFY(actionState.canFillet);
        QVERIFY(actionState.canChamfer);

        const auto fillet = controller.createFillet(edgeSelection, 2.0);
        QVERIFY(fillet.success);
        const auto filletFeature = controller.body().findFeature(fillet.id);
        QVERIFY(filletFeature);
        QVERIFY(!filletFeature->shape().IsNull());
        QCOMPARE(filletFeature->dependencies().size(), std::size_t{1});
        QCOMPARE(filletFeature->properties().front().key, std::string("sourceFeatureId"));

        controller.undo();
        QVERIFY(!controller.body().findFeature(fillet.id));
        controller.redo();
        QVERIFY(controller.body().findFeature(fillet.id));

        ModelingController chamferController;
        const auto chamferBox = chamferController.createBox();
        QVERIFY(chamferBox.success);
        const auto chamfer = chamferController.createChamfer(
            SelectionSnapshot{{{chamferBox.id, SelectionKind::Edge, 1}}}, 2.0);
        QVERIFY(chamfer.success);
        QVERIFY(!chamferController.body().findFeature(chamfer.id)->shape().IsNull());

        const auto invalidMixed = chamferController.createFillet(
            SelectionSnapshot{{{chamferBox.id, SelectionKind::Edge, 1},
                               {chamferBox.id, SelectionKind::Face, 1}}}, 2.0);
        QVERIFY(!invalidMixed.success);
    }

    void filletAndChamferPersistEdgeIndices()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto fillet = controller.createFillet(
            SelectionSnapshot{{{box.id, SelectionKind::Edge, 1}}}, 2.0);
        QVERIFY(fillet.success);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("details.pcad");
        QString error;
        QVERIFY(ProjectFile::save(path, controller.document(), controller.body(), error));

        Document document;
        cad::parametric::Body body;
        QVERIFY2(ProjectFile::load(path, document, body, error), qPrintable(error));
        const auto loaded = body.findFeature(fillet.id);
        QVERIFY(loaded);
        QCOMPARE(std::string(loaded->typeId()), std::string("Fillet"));
        QVERIFY(!loaded->shape().IsNull());
    }

    void topologicalReferencesValidateAndRecover()
    {
        using namespace cad::topology;
        const auto first = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge();
        const auto second = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 10, 0), gp_Pnt(10, 10, 0)).Edge();
        TopoDS_Compound original;
        BRep_Builder builder;
        builder.MakeCompound(original);
        builder.Add(original, first);
        builder.Add(original, second);

        const auto reference = TopologicalSignatureBuilder::createReference(
            "edges", original, first);
        const auto recoveredShape = [&]() {
            TopoDS_Compound reordered;
            builder.MakeCompound(reordered);
            builder.Add(reordered, second);
            builder.Add(reordered, first);
            return reordered;
        }();
        const auto recovered = TopologicalReferenceResolver::resolveAgainstShape(
            reference, recoveredShape);
        QCOMPARE(recovered.status, ResolveStatus::Resolved);
        QCOMPARE(recovered.resolvedIndex, std::optional<int>{2});

        const auto missingShape = BRepBuilderAPI_MakeEdge(
            gp_Pnt(0, 20, 0), gp_Pnt(10, 20, 0)).Edge();
        const auto missing = TopologicalReferenceResolver::resolveAgainstShape(
            reference, missingShape);
        QCOMPARE(missing.status, ResolveStatus::Missing);

        TopoDS_Compound ambiguousShape;
        builder.MakeCompound(ambiguousShape);
        builder.Add(ambiguousShape, BRepBuilderAPI_MakeEdge(
            gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge());
        builder.Add(ambiguousShape, BRepBuilderAPI_MakeEdge(
            gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge());
        auto ambiguousReference = reference;
        ambiguousReference.transientIndex.reset();
        const auto ambiguous = TopologicalReferenceResolver::resolveAgainstShape(
            ambiguousReference, ambiguousShape);
        QCOMPARE(ambiguous.status, ResolveStatus::Ambiguous);
    }

    void topologyReferencesSurviveBoxResizeAndUndoRedo()
    {
        using namespace cad::topology;
        ModelingController controller;
        const auto created = controller.createBox();
        QVERIFY(created.success);
        const auto original = controller.body().findFeature(created.id)->shape();

        TopTools_IndexedMapOfShape faces;
        TopTools_IndexedMapOfShape edges;
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(original, TopAbs_FACE, faces);
        TopExp::MapShapes(original, TopAbs_EDGE, edges);
        TopExp::MapShapes(original, TopAbs_VERTEX, vertices);
        const auto face = TopologicalSignatureBuilder::createReference(created.id, original, faces(1));
        const auto edge = TopologicalSignatureBuilder::createReference(created.id, original, edges(1));
        const auto vertex = TopologicalSignatureBuilder::createReference(created.id, original, vertices(1));

        QVERIFY(controller.setFeatureProperty(created.id, "width", 150.0).success);
        const auto resized = controller.body().findFeature(created.id)->shape();
        QCOMPARE(TopologicalReferenceResolver::resolveAgainstShape(face, resized).status,
                 ResolveStatus::Resolved);
        QCOMPARE(TopologicalReferenceResolver::resolveAgainstShape(edge, resized).status,
                 ResolveStatus::Resolved);
        QCOMPARE(TopologicalReferenceResolver::resolveAgainstShape(vertex, resized).status,
                 ResolveStatus::Resolved);

        controller.undo();
        QCOMPARE(TopologicalReferenceResolver::resolveAgainstShape(
                     face, controller.body().findFeature(created.id)->shape()).status,
                 ResolveStatus::Resolved);
        controller.redo();
        QCOMPARE(TopologicalReferenceResolver::resolveAgainstShape(
                     edge, controller.body().findFeature(created.id)->shape()).status,
                 ResolveStatus::Resolved);
    }

    void topologicalReferencesCoverFaceVertexAndPersistence()
    {
        using namespace cad::topology;
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto shape = controller.body().findFeature(box.id)->shape();

        TopTools_IndexedMapOfShape faces;
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(shape, TopAbs_FACE, faces);
        TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
        const auto face = TopologicalSignatureBuilder::createReference(
            box.id, shape, faces(1));
        const auto vertex = TopologicalSignatureBuilder::createReference(
            box.id, shape, vertices(1));
        QVERIFY(std::holds_alternative<FaceSignature>(*face.signature));
        QVERIFY(std::holds_alternative<VertexSignature>(*vertex.signature));
        ModelingController cylinderController;
        const auto cylinder = cylinderController.createCylinder();
        QVERIFY(cylinder.success);
        TopTools_IndexedMapOfShape cylinderFaces;
        TopExp::MapShapes(cylinderController.body().findFeature(cylinder.id)->shape(),
                          TopAbs_FACE, cylinderFaces);
        bool foundCylinder = false;
        for (int index = 1; index <= cylinderFaces.Extent(); ++index) {
            const auto cylinderFace = TopologicalSignatureBuilder::createReference(
                cylinder.id, cylinderController.body().findFeature(cylinder.id)->shape(),
                cylinderFaces(index));
            const auto* signature = std::get_if<FaceSignature>(&*cylinderFace.signature);
            if (signature && signature->surfaceKind == SurfaceKind::Cylinder) {
                foundCylinder = true;
                break;
            }
        }
        QVERIFY(foundCylinder);
        const auto faceJson = toJson(face);
        const auto restored = topologicalReferenceFromJson(faceJson);
        QVERIFY(restored.signature.has_value());
        const auto resolved = TopologicalReferenceResolver(controller.body()).resolve(restored);
        QCOMPARE(resolved.status, ResolveStatus::Resolved);
    }

    void sketchOnFaceIsParametricAndUndoable()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        SelectionSnapshot selection{{{box.id, SelectionKind::Face, 1}}};
        QVERIFY(controller.actionState(selection).canSketchOnFace);

        const auto created = controller.createSketchOnFace(selection);
        QVERIFY(created.success);
        const auto sketch = std::dynamic_pointer_cast<SketchFeature>(
            controller.body().findFeature(created.id));
        QVERIFY(sketch);
        QCOMPARE(sketch->supportType(), SketchSupportType::Face);
        QCOMPARE(sketch->dependencies().size(), std::size_t{1});
        QVERIFY(sketch->shape().ShapeType() == TopAbs_COMPOUND);
        const auto frame = sketch->currentFrame();
        QVERIFY(std::abs(gp_Vec(frame.xDirection).Crossed(
            gp_Vec(frame.yDirection)).Dot(gp_Vec(frame.normal)) - 1.0) < 1.0e-9);

        QVERIFY(controller.addSketchLine(created.id, {0, 0}, {20, 0}).success);
        QVERIFY(controller.addSketchCircle(created.id, {10, 10}, 5.0).success);
        QCOMPARE(sketch->entityCount(), std::size_t{2});
        controller.undo();
        QCOMPARE(sketch->entityCount(), std::size_t{1});
        controller.redo();
        QCOMPARE(sketch->entityCount(), std::size_t{2});

        const auto changed = controller.setFeatureProperty(box.id, "width", 150.0);
        QVERIFY(changed.success);
        QVERIFY(sketch->state() == FeatureState::UpToDate);
        QCOMPARE(sketch->entityCount(), std::size_t{2});
        const auto recreated = std::make_shared<BoxParametricFeature>(box.id, 150.0, 70.0, 30.0);
        QVERIFY(recreated->recompute());
        const auto rematched = cad::topology::TopologicalReferenceResolver::resolveAgainstShape(
            *sketch->faceReference(), recreated->shape());
        QCOMPARE(rematched.status, cad::topology::ResolveStatus::Resolved);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto path = directory.filePath("attached-sketch.pcad");
        QVERIFY(ProjectFile::save(path, controller.document(), controller.body(), error));
        Document loadedDocument;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loadedDocument, loadedBody, error), qPrintable(error));
        const auto loaded = std::dynamic_pointer_cast<SketchFeature>(loadedBody.findFeature(created.id));
        QVERIFY(loaded);
        QCOMPARE(loaded->entityCount(), std::size_t{2});
        QCOMPARE(loaded->supportType(), SketchSupportType::Face);
        QVERIFY(!loaded->shape().IsNull());
        const auto loadedBox = std::dynamic_pointer_cast<BoxParametricFeature>(
            loadedBody.findFeature(box.id));
        QVERIFY(loadedBox);
        loadedBox->setSize(170.0, 70.0, 30.0);
        loadedBody.markDirtyFrom(box.id);
        QVERIFY(loadedBody.recompute());
        QVERIFY(loaded->state() == FeatureState::UpToDate);
        const auto loadedSupport = cad::topology::TopologicalReferenceResolver::resolveAgainstShape(
            *loaded->faceReference(), loadedBox->shape());
        QCOMPARE(loadedSupport.status, cad::topology::ResolveStatus::Resolved);

        ModelingController cylinderController;
        const auto cylinder = cylinderController.createCylinder();
        QVERIFY(cylinder.success);
        TopTools_IndexedMapOfShape cylinderFaces;
        TopExp::MapShapes(cylinderController.body().findFeature(cylinder.id)->shape(),
            TopAbs_FACE, cylinderFaces);
        int curvedFaceIndex = 0;
        for (int index = 1; index <= cylinderFaces.Extent(); ++index) {
            if (!SketchFeature::isPlanarFace(cylinderFaces(index))) {
                curvedFaceIndex = index;
                break;
            }
        }
        QVERIFY(curvedFaceIndex > 0);
        const SelectionSnapshot curvedFace{{{cylinder.id, SelectionKind::Face, curvedFaceIndex}}};
        QVERIFY(!cylinderController.actionState(curvedFace).canSketchOnFace);
        QVERIFY(!cylinderController.createSketchOnFace(curvedFace).success);
    }

    void arcUsesSketchFrameForGlobalAndFaceAttachedSketches()
    {
        const auto verifyArc = [](ModelingController& controller,
                                  const std::string& sketchId) {
            QVERIFY(controller.addSketchArc(
                sketchId, {10.0, 10.0}, {15.0, 10.0}, {10.0, 15.0}).success);
            const auto sketch = std::dynamic_pointer_cast<SketchFeature>(
                controller.body().findFeature(sketchId));
            QVERIFY(sketch);
            const auto frame = sketch->currentFrame();
            const auto worldPoint = [&frame](const gp_Pnt2d& point) {
                gp_Pnt result = frame.origin;
                result.Translate(gp_Vec(frame.xDirection) * point.X()
                    + gp_Vec(frame.yDirection) * point.Y());
                return result;
            };
            const auto expectedCenter = worldPoint({10.0, 10.0});
            const auto expectedStart = worldPoint({15.0, 10.0});
            const auto expectedEnd = worldPoint({10.0, 15.0});

            TopTools_IndexedMapOfShape edges;
            TopExp::MapShapes(sketch->shape(), TopAbs_EDGE, edges);
            QVERIFY(edges.Extent() == 1);
            const BRepAdaptor_Curve curve(TopoDS::Edge(edges.FindKey(1)));
            QCOMPARE(curve.GetType(), GeomAbs_Circle);
            QVERIFY(curve.Circle().Location().Distance(expectedCenter) < 1.0e-6);
            const auto first = curve.Value(curve.FirstParameter());
            const auto last = curve.Value(curve.LastParameter());
            QVERIFY((first.Distance(expectedStart) < 1.0e-6
                && last.Distance(expectedEnd) < 1.0e-6)
                || (first.Distance(expectedEnd) < 1.0e-6
                    && last.Distance(expectedStart) < 1.0e-6));
        };

        for (const auto support : {SketchSupportType::XY, SketchSupportType::XZ,
                                   SketchSupportType::YZ}) {
            ModelingController controller;
            const auto sketch = controller.createSketch(support);
            QVERIFY(sketch.success);
            verifyArc(controller, sketch.id);
        }

        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto sketch = controller.createSketchOnFace(SelectionSnapshot{{
            {box.id, SelectionKind::Face, 1}}});
        QVERIFY(sketch.success);
        verifyArc(controller, sketch.id);
    }

    void duplicateCreatesIndependentFeatureAndRepeatsDelta()
    {
        ModelingController controller;
        const auto source = controller.createBox();
        QVERIFY(source.success);
        const auto sourceFeature = controller.body().findFeature(source.id);
        QVERIFY(sourceFeature);
        const auto sourceParameters = sourceFeature->serialize();

        gp_Trsf delta;
        delta.SetTranslation(gp_Vec(100.0, 0.0, 0.0));
        const auto first = controller.duplicateFeatureWithDelta(source.id, delta);
        QVERIFY(first.success);
        QVERIFY(first.id != source.id);
        const auto firstFeature = controller.body().findFeature(first.id);
        QVERIFY(firstFeature);
        QCOMPARE(firstFeature->placement().TranslationPart().X(), 100.0);
        QCOMPARE(sourceFeature->serialize(), sourceParameters);
        QCOMPARE(firstFeature->serialize().value("type"), sourceParameters.value("type"));

        const auto second = controller.duplicateFeature(first.id);
        QVERIFY(second.success);
        const auto secondFeature = controller.body().findFeature(second.id);
        QVERIFY(secondFeature);
        QCOMPARE(secondFeature->placement().TranslationPart().X(), 200.0);

        controller.undo();
        QVERIFY(!controller.body().findFeature(second.id));
        controller.redo();
        QVERIFY(controller.body().findFeature(second.id));
        QCOMPARE(controller.body().findFeature(second.id)->placement().TranslationPart().X(), 200.0);

        controller.undo();
        controller.undo();
        QVERIFY(!controller.body().findFeature(first.id));
        QVERIFY(controller.body().findFeature(source.id));
    }

    void duplicatePlacementSurvivesSerialization()
    {
        ModelingController controller;
        const auto source = controller.createBox();
        QVERIFY(source.success);
        gp_Trsf delta;
        delta.SetTranslation(gp_Vec(25.0, 5.0, 0.0));
        const auto copy = controller.duplicateFeatureWithDelta(source.id, delta);
        QVERIFY(copy.success);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVERIFY(ProjectFile::save(
            directory.filePath("duplicate.pcad"),
            controller.document(),
            controller.body(),
            error));

        Document loadedDocument;
        cad::parametric::Body loadedBody;
        QVERIFY(ProjectFile::load(
            directory.filePath("duplicate.pcad"),
            loadedDocument,
            loadedBody,
            error));
        const auto loadedCopy = loadedBody.findFeature(copy.id);
        QVERIFY(loadedCopy);
        QCOMPARE(loadedCopy->placement().TranslationPart().X(), 25.0);
        QCOMPARE(loadedCopy->placement().TranslationPart().Y(), 5.0);
    }

    void linearAndPathPatternsRecomputeAsSingleFeatures()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto linear = controller.createLinearPattern({box.id});
        QVERIFY(linear.success);
        const auto linearFeature = controller.body().findFeature(linear.id);
        QVERIFY(linearFeature);
        QVERIFY(std::abs(volume(linearFeature->shape())
                         - 2.0 * volume(controller.body().findFeature(box.id)->shape())) < 1.0e-6);

        QVERIFY(controller.setFeatureProperty(linear.id, "count", 3).success);
        const auto updatedLinear = controller.body().findFeature(linear.id);
        QVERIFY(std::abs(volume(updatedLinear->shape())
                         - 3.0 * volume(controller.body().findFeature(box.id)->shape())) < 1.0e-6);

        const auto path = controller.createSketch();
        QVERIFY(path.success);
        const auto pathPattern = controller.createPathPattern({box.id, path.id});
        QVERIFY(pathPattern.success);
        const auto pathFeature = controller.body().findFeature(pathPattern.id);
        QVERIFY(pathFeature);
        QVERIFY(std::abs(volume(pathFeature->shape())
                         - 2.0 * volume(controller.body().findFeature(box.id)->shape())) < 1.0e-6);

        controller.undo();
        QVERIFY(!controller.body().findFeature(pathPattern.id));
        controller.redo();
        QVERIFY(controller.body().findFeature(pathPattern.id));
        QVERIFY(controller.setFeatureProperty(
            pathPattern.id, "orientation", std::string("Tangent")).success);
        QVERIFY(controller.setFeatureProperty(
            pathPattern.id, "distribution", std::string("FixedSpacing")).success);
        QVERIFY(volume(controller.body().findFeature(pathPattern.id)->shape()) > 0.0);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVERIFY(ProjectFile::save(
            directory.filePath("patterns.pcad"),
            controller.document(), controller.body(), error));
        Document loadedDocument;
        cad::parametric::Body loadedBody;
        QVERIFY(ProjectFile::load(
            directory.filePath("patterns.pcad"),
            loadedDocument, loadedBody, error));
        QVERIFY(loadedBody.findFeature(linear.id));
        QVERIFY(loadedBody.findFeature(pathPattern.id));
    }
};

QTEST_APPLESS_MAIN(ControllerTests)
#include "controller_tests.moc"
