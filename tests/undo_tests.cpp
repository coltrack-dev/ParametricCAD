#include "commands/FeatureCommands.h"
#include "model/FeatureVisibility.h"
#include "model/ProjectFile.h"
#include "operations/BoxFeature.h"
#include "operations/ParametricFeatures.h"
#include <QtTest/QtTest>
#include <QUndoStack>
#include <QTemporaryDir>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <algorithm>
#include <cmath>

using namespace cad::parametric;
using namespace cad::commands;

namespace {
double volume(const TopoDS_Shape& shape)
{
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}
bool hidden(const Body& body, const std::string& id)
{
    const auto ids = hiddenFeatureIds(body);
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
void changeWidth(QUndoStack& stack, Body& body, const std::shared_ptr<SketchFeature>& sketch, double width)
{
    stack.push(new ChangeFeatureParameterCommand<SketchFeature, double>(body, sketch,
        sketch->width(), width, [](auto& f, double v) { f.setSize(v, f.height()); }, "Change Sketch Width"));
}
}

class UndoTests final : public QObject
{
    Q_OBJECT
private slots:
    void addStableIdentityAndSequentialHistory()
    {
        Body body;
        QUndoStack stack;
        auto box = std::make_shared<BoxParametricFeature>("box", 2, 3, 4);
        auto cylinder = std::make_shared<CylinderParametricFeature>("cylinder", 2, 6);
        stack.push(new AddFeatureCommand(body, box, "Create Box"));
        stack.push(new AddFeatureCommand(body, cylinder, "Create Cylinder"));
        QCOMPARE(stack.count(), 2);
        QCOMPARE(stack.undoText(), QString("Create Cylinder"));
        stack.undo();
        QVERIFY(!body.findFeature("cylinder"));
        QCOMPARE(body.features().size(), std::size_t{1});
        stack.undo();
        QVERIFY(body.features().empty());
        stack.redo();
        stack.redo();
        QCOMPARE(body.features()[0].get(), box.get());
        QCOMPARE(body.features()[1].get(), cylinder.get());
        QCOMPARE(cylinder->id(), std::string("cylinder"));
        QVERIFY(!body.shape().IsNull());
        stack.undo();
        auto replacement = std::make_shared<SphereFeature>("sphere", 3);
        stack.push(new AddFeatureCommand(body, replacement, "Create Sphere"));
        QVERIFY(!stack.canRedo());
        QVERIFY(!body.findFeature("cylinder"));
    }

    void sourceParameterRebuildsOnlyDependents()
    {
        Body body;
        QUndoStack stack;
        auto sketch = std::make_shared<SketchFeature>("sketch", 10, 6);
        auto face = std::make_shared<FaceFeature>("face", sketch);
        auto unrelated = std::make_shared<BoxParametricFeature>("unrelated", 2, 3, 4);
        auto extrude = std::make_shared<ExtrudeFeature>("extrude", face, gp_Vec(0, 0, 5));
        for (const auto& feature : std::vector<ParametricFeature::Ptr>{sketch, face, unrelated, extrude}) {
            stack.push(new AddFeatureCommand(body, feature, "Create Feature"));
        }
        const auto originalUnrelated = unrelated->shape();
        const auto originalFace = face->shape();
        changeWidth(stack, body, sketch, 15);
        QCOMPARE(stack.undoText(), QString("Change Sketch Width"));
        QCOMPARE(stack.count(), 5);
        QVERIFY(std::abs(volume(extrude->shape()) - 450) < 1e-7);
        QVERIFY(!face->shape().IsSame(originalFace));
        QVERIFY(unrelated->shape().IsSame(originalUnrelated));
        stack.undo();
        QCOMPARE(sketch->width(), 10.0);
        QVERIFY(std::abs(volume(extrude->shape()) - 300) < 1e-7);
        QVERIFY(unrelated->shape().IsSame(originalUnrelated));
        stack.redo();
        QVERIFY(std::abs(volume(extrude->shape()) - 450) < 1e-7);
        stack.push(new ChangeFeatureParameterCommand<ExtrudeFeature, double>(body, extrude, 5, 8,
            [](auto& f, double v) { f.setVector(f.vector().Normalized() * v); }, "Change Extrude Length"));
        QVERIFY(std::abs(volume(extrude->shape()) - 720) < 1e-7);
        stack.undo();
        QVERIFY(std::abs(volume(extrude->shape()) - 450) < 1e-7);
        stack.redo();
        QVERIFY(std::abs(volume(extrude->shape()) - 720) < 1e-7);
    }

    void cutVisibilityAndRemoval()
    {
        Body body;
        QUndoStack stack;
        auto hexagon = std::make_shared<HexagonFeature>("hexagon", 30, 12);
        auto cylinder = std::make_shared<CylinderParametricFeature>("cylinder", 5, 20);
        auto cut = std::make_shared<BooleanFeature>("cut", hexagon, cylinder, BooleanOperation::Cut);
        stack.push(new AddFeatureCommand(body, hexagon, "Create Hexagon"));
        stack.push(new AddFeatureCommand(body, cylinder, "Create Cylinder"));
        stack.push(new AddFeatureCommand(body, cut, "Boolean Cut"));
        QVERIFY(hidden(body, "hexagon") && hidden(body, "cylinder"));
        QVERIFY(!hidden(body, "cut"));
        stack.undo();
        QVERIFY(!body.findFeature("cut"));
        QVERIFY(!hidden(body, "hexagon") && !hidden(body, "cylinder"));
        stack.redo();
        QCOMPARE(body.findFeature("cut").get(), cut.get());
        QCOMPARE(cut->left().get(), hexagon.get());
        QCOMPARE(cut->right().get(), cylinder.get());
        QVERIFY(hidden(body, "hexagon") && hidden(body, "cylinder"));
        stack.push(new RemoveFeatureCommand(body, "hexagon"));
        QVERIFY(!body.findFeature("hexagon") && !body.findFeature("cut"));
        QVERIFY(!hidden(body, "cylinder"));
        stack.undo();
        QCOMPARE(body.features()[0].get(), hexagon.get());
        QCOMPARE(body.features()[2].get(), cut.get());
        QVERIFY(hidden(body, "hexagon") && hidden(body, "cylinder"));
        stack.push(new RemoveFeatureCommand(body, "cut"));
        QVERIFY(!hidden(body, "hexagon") && !hidden(body, "cylinder"));
        stack.undo();
        QCOMPARE(body.features().back().get(), cut.get());
        QVERIFY(hidden(body, "hexagon") && hidden(body, "cylinder"));
        stack.redo();
        QVERIFY(!body.findFeature("cut"));
    }

    void removeRestoresOrderAndSource()
    {
        Body body;
        QUndoStack stack;
        auto sketch = std::make_shared<SketchFeature>("sketch", 10, 6);
        auto face = std::make_shared<FaceFeature>("face", sketch);
        auto box = std::make_shared<BoxParametricFeature>("box", 2, 3, 4);
        body.addFeature(sketch);
        body.addFeature(face);
        body.addFeature(box);
        QVERIFY(body.recompute());
        const auto oldBoxShape = box->shape();
        stack.push(new RemoveFeatureCommand(body, "face"));
        stack.push(new RemoveFeatureCommand(body, "sketch"));
        stack.undo();
        stack.undo();
        QCOMPARE(body.features()[0].get(), sketch.get());
        QCOMPARE(body.features()[1].get(), face.get());
        QCOMPARE(body.features()[2].get(), box.get());
        QCOMPARE(face->source().get(), sketch.get());
        QVERIFY(box->shape().IsSame(oldBoxShape));
    }

    void cascadeDeletion()
    {
        Body body;
        QUndoStack stack;
        auto sketch = std::make_shared<SketchFeature>("sketch", 10, 6);
        auto face = std::make_shared<FaceFeature>("face", sketch);
        auto box = std::make_shared<BoxParametricFeature>("box", 2, 3, 4);
        auto extrude = std::make_shared<ExtrudeFeature>("extrude", face, gp_Vec(0, 0, 5));
        for (const auto& feature : std::vector<ParametricFeature::Ptr>{sketch, face, box, extrude}) body.addFeature(feature);
        QVERIFY(body.recompute());
        const auto original = body.features();
        const auto boxShape = box->shape();
        // Preparing a confirmation and cancelling it must not mutate anything.
        { RemoveFeatureCommand pending(body, "sketch"); QCOMPARE(pending.dependentNames().size(), 2); }
        QCOMPARE(body.features(), original);
        stack.push(new RemoveFeatureCommand(body, "sketch"));
        QCOMPARE(stack.count(), 1);
        QCOMPARE(body.features().size(), std::size_t{1});
        QCOMPARE(body.features().front().get(), box.get());
        QVERIFY_EXCEPTION_THROWN(RemoveFeatureCommand(body, "sketch"), std::invalid_argument);
        QVERIFY(!body.removeFeature("sketch"));
        for (int i = 0; i < 3; ++i) {
            stack.undo();
            QCOMPARE(body.features(), original);
            QCOMPARE(body.findFeature("extrude").get(), extrude.get());
            QCOMPARE(face->source().get(), sketch.get());
            QCOMPARE(extrude->profile().get(), face.get());
            QVERIFY(body.recompute());
            QVERIFY(std::abs(volume(extrude->shape()) - 300) < 1e-7);
            QVERIFY(box->shape().IsSame(boxShape));
            stack.redo();
            QCOMPARE(body.features().size(), std::size_t{1});
        }
    }

    void standaloneDeletion()
    {
        Body body;
        auto box = std::make_shared<BoxParametricFeature>("box", 2, 3, 4);
        body.addFeature(box);
        QVERIFY(body.recompute());
        QUndoStack stack;
        stack.push(new RemoveFeatureCommand(body, "box"));
        QVERIFY(body.features().empty());
        stack.undo();
        QCOMPARE(body.findFeature("box").get(), box.get());
        stack.redo();
        QVERIFY(body.features().empty());

        Document document;
        document.addFeature(std::make_unique<BoxFeature>(2, 3, 4));
        const auto* original = document.features().front().get();
        QUndoStack legacyStack;
        legacyStack.push(new RemoveDocumentFeatureCommand(document, 0));
        QVERIFY(document.features().empty());
        legacyStack.undo();
        QCOMPARE(document.features().front().get(), original);
        legacyStack.redo();
        QVERIFY(document.features().empty());
        QVERIFY_EXCEPTION_THROWN(RemoveDocumentFeatureCommand(document, 0), std::invalid_argument);
    }

    void cleanStateSaveAndLoad()
    {
        Document document;
        Body body;
        QUndoStack stack;
        auto sketch = std::make_shared<SketchFeature>("sketch", 10, 6);
        auto face = std::make_shared<FaceFeature>("face", sketch);
        auto extrude = std::make_shared<ExtrudeFeature>("extrude", face, gp_Vec(0, 0, 5));
        stack.push(new AddFeatureCommand(body, sketch, "Create Sketch"));
        stack.push(new AddFeatureCommand(body, face, "Create Face"));
        stack.push(new AddFeatureCommand(body, extrude, "Create Extrude"));
        QVERIFY(!stack.isClean());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        const auto path = directory.filePath("undo.pcad");
        QVERIFY2(ProjectFile::save(path, document, body, error), qPrintable(error));
        stack.setClean();
        QVERIFY(stack.isClean());
        QCOMPARE(stack.count(), 3);
        changeWidth(stack, body, sketch, 20);
        QVERIFY(!stack.isClean());
        stack.undo();
        QVERIFY(stack.isClean());
        stack.redo();
        QVERIFY(!stack.isClean());
        Document loaded;
        Body loadedBody;
        QVERIFY2(ProjectFile::load(path, loaded, loadedBody, error), qPrintable(error));
        auto loadedExtrude = std::dynamic_pointer_cast<ExtrudeFeature>(loadedBody.findFeature("extrude"));
        QVERIFY(loadedExtrude);
        QCOMPARE(loadedExtrude->profile().get(), loadedBody.findFeature("face").get());
        QVERIFY(std::abs(volume(loadedExtrude->shape()) - 300) < 1e-7);
        stack.clear();
        QVERIFY(stack.isClean());
        QVERIFY(!stack.canUndo() && !stack.canRedo());
    }

    void clearProjectRestoresBothContainers()
    {
        Document document;
        Body body;
        QUndoStack stack;
        auto sketch = std::make_shared<SketchFeature>("sketch", 10, 6);
        stack.push(new AddDocumentFeatureCommand(document, std::make_unique<BoxFeature>(2, 3, 4), "Create Box"));
        const auto* originalBox = document.features().front().get();
        stack.push(new AddFeatureCommand(body, sketch, "Create Sketch"));
        stack.push(new ClearProjectCommand(document, body));
        QVERIFY(document.features().empty() && body.features().empty());
        stack.undo();
        QCOMPARE(document.features().front().get(), originalBox);
        QCOMPARE(body.findFeature("sketch").get(), sketch.get());
        stack.redo();
        QVERIFY(document.features().empty() && body.features().empty());
        stack.undo();
        stack.undo();
        stack.undo();
        QVERIFY(document.features().empty() && body.features().empty());
        stack.redo();
        stack.redo();
        QCOMPARE(document.features().front().get(), originalBox);
        QCOMPARE(body.findFeature("sketch").get(), sketch.get());
    }

    void invalidEditCanBeUndone()
    {
        Body body;
        QUndoStack stack;
        auto sketch = std::make_shared<SketchFeature>("sketch", 10, 6);
        auto face = std::make_shared<FaceFeature>("face", sketch);
        body.addFeature(sketch);
        body.addFeature(face);
        QVERIFY(body.recompute());
        changeWidth(stack, body, sketch, 0);
        QCOMPARE(sketch->state(), FeatureState::Failed);
        QVERIFY(hidden(body, "face"));
        stack.undo();
        QCOMPARE(sketch->width(), 10.0);
        QCOMPARE(face->state(), FeatureState::UpToDate);
        QVERIFY(!hidden(body, "face"));
        stack.redo();
        QCOMPARE(sketch->state(), FeatureState::Failed);
    }
};

QTEST_APPLESS_MAIN(UndoTests)
#include "undo_tests.moc"
