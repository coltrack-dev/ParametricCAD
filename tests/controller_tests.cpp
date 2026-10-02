#include "application/ModelingController.h"
#include "application/ProjectController.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include <algorithm>
#include <variant>

using namespace cad::application;
using namespace cad::parametric;

class ControllerTests final : public QObject
{
    Q_OBJECT

private slots:
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
};

QTEST_APPLESS_MAIN(ControllerTests)
#include "controller_tests.moc"
