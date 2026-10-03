#include "application/ModelingController.h"
#include "application/ProjectController.h"
#include "application/SelectionResolver.h"
#include "model/ProjectFile.h"
#include "model/TopologicalReference.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Builder.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopAbs_Orientation.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopoDS.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
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
        QVERIFY(!state.canExtrude);

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
