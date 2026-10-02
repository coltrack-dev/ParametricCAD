#include "application/ModelingController.h"
#include "application/ProjectController.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>

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
            BooleanOperation::Cut, {box.id, cylinder.id}, cylinder.id);
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
};

QTEST_APPLESS_MAIN(ControllerTests)
#include "controller_tests.moc"
