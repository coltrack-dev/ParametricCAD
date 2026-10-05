#include "viewer/FeatureEditorPanel.h"
#include "application/ModelingController.h"
#include "application/ProjectController.h"
#include <QtTest/QtTest>
#include <QAction>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>

using namespace cad::application;

class UndoPanelTests final : public QObject
{
    Q_OBJECT
private slots:
    void editingFinishedIsOneCommand()
    {
        ModelingController controller;
        const auto cylinder = controller.createCylinder();
        QVERIFY(cylinder.success);
        FeatureEditorPanel panel;
        panel.setService(&controller);
        panel.setFeatures(controller.features());
        panel.setActionState(controller.actionState({cylinder.id}));
        panel.selectFeatures({QString::fromStdString(cylinder.id)});
        auto* radius = panel.findChildren<QDoubleSpinBox*>().front();
        radius->setValue(6);
        radius->setValue(7);
        radius->setValue(10);
        QCOMPARE(controller.undoStack().count(), 1);
        QCOMPARE(controller.body().findFeature(cylinder.id)->properties().front().value,
                 cad::parametric::PropertyValue{25.0});
        QVERIFY(QMetaObject::invokeMethod(radius, "editingFinished", Qt::DirectConnection));
        QCoreApplication::processEvents();
        QCOMPARE(controller.undoStack().count(), 2);
        QCOMPARE(controller.undoStack().undoText(), QString("Change Cylinder Radius"));
        QCoreApplication::processEvents();
        controller.undo();
        panel.setFeatures(controller.features());
        QCoreApplication::processEvents();
        QCOMPARE(controller.body().findFeature(cylinder.id)->properties().front().value,
                 cad::parametric::PropertyValue{25.0});
        QCOMPARE(panel.findChildren<QDoubleSpinBox*>().front()->value(), 25.0);
        controller.redo();
        panel.setFeatures(controller.features());
        QCoreApplication::processEvents();
        QCOMPARE(controller.body().findFeature(cylinder.id)->properties().front().value,
                 cad::parametric::PropertyValue{10.0});
        QCOMPARE(panel.findChildren<QDoubleSpinBox*>().front()->value(), 10.0);
    }

    void deletingSelectedFeatureClearsSelection()
    {
        ModelingController controller;
        const auto sketch = controller.createSketch();
        QVERIFY(sketch.success);
        const auto face = controller.createFace({sketch.id});
        QVERIFY(face.success);
        FeatureEditorPanel panel;
        panel.setService(&controller);
        panel.setModelChangedHandler([&]() {
            panel.setFeatures(controller.features());
        });
        panel.setFeatures(controller.features());
        panel.selectFeatures({QString::fromStdString(sketch.id)});
        QVERIFY(!panel.findChildren<QDoubleSpinBox*>().isEmpty());
        QVERIFY(controller.deleteFeature(sketch.id).success);
        panel.setFeatures(controller.features());
        QCoreApplication::processEvents();
        QVERIFY(panel.selectedFeatureIds().isEmpty());
        QVERIFY(panel.findChildren<QDoubleSpinBox*>().isEmpty());
        controller.undo();
        panel.setFeatures(controller.features());
        QCoreApplication::processEvents();
        panel.selectFeatures({QString::fromStdString(face.id)});
        QCOMPARE(panel.selectedFeatureIds(), QStringList{QString::fromStdString(face.id)});
        controller.redo();
        panel.setFeatures(controller.features());
        QCoreApplication::processEvents();
        QVERIFY(panel.selectedFeatureIds().isEmpty());
    }

    void linearPatternPropertyEditsDoNotInvalidateActiveEditor()
    {
        ModelingController controller;
        const auto box = controller.createBox();
        QVERIFY(box.success);
        const auto pattern = controller.createLinearPattern({box.id});
        QVERIFY(pattern.success);

        FeatureEditorPanel panel;
        panel.setService(&controller);
        panel.setFeatures(controller.features());
        panel.selectFeatures({QString::fromStdString(pattern.id)});

        auto* count = panel.findChildren<QSpinBox*>().front();
        for (const int value : {5, 1, 3, 2}) {
            count->setValue(value);
            QVERIFY(QMetaObject::invokeMethod(count, "editingFinished", Qt::DirectConnection));
            QCoreApplication::processEvents();
            count = panel.findChildren<QSpinBox*>().front();
        }

        QCOMPARE(controller.body().findFeature(pattern.id)->state(),
                 cad::parametric::FeatureState::UpToDate);
        QVERIFY(!controller.body().findFeature(pattern.id)->shape().IsNull());
    }

    void shortcutsAndCleanActions()
    {
        ModelingController controller;
        const auto cylinder = controller.createCylinder();
        QVERIFY(cylinder.success);
        controller.undoStack().clear();
        FeatureEditorPanel panel;
        panel.setService(&controller);
        panel.setFeatures(controller.features());
        panel.selectFeatures({QString::fromStdString(cylinder.id)});
        auto* undo = controller.undoStack().createUndoAction(&panel);
        auto* redo = controller.undoStack().createRedoAction(&panel);
        QVERIFY(!undo->isEnabled() && !redo->isEnabled());
        panel.show();
        panel.activateWindow();
        auto* radius = panel.findChildren<QDoubleSpinBox*>().front();
        radius->setFocus();
        QTRY_VERIFY(radius->hasFocus());
        radius->setValue(9);
        QTest::keyClick(radius, Qt::Key_Z, Qt::ControlModifier);
        QCoreApplication::processEvents();
        QCOMPARE(controller.body().findFeature(cylinder.id)->properties().front().value,
                 cad::parametric::PropertyValue{25.0});
        QCOMPARE(controller.undoStack().count(), 1);
        QVERIFY(controller.undoStack().isClean());
        QVERIFY(redo->isEnabled());
        panel.setFeatures(controller.features());
        QCoreApplication::processEvents();
        radius = panel.findChildren<QDoubleSpinBox*>().front();
        radius->setFocus();
        QTest::keyClick(radius, Qt::Key_Y, Qt::ControlModifier);
        QCoreApplication::processEvents();
        QCOMPARE(controller.body().findFeature(cylinder.id)->properties().front().value,
                 cad::parametric::PropertyValue{9.0});
        QCOMPARE(controller.undoStack().count(), 1);
        QVERIFY(undo->isEnabled());
        controller.undoStack().setClean();
        QVERIFY(controller.undoStack().isClean());
        controller.undoStack().clear();
        QVERIFY(!undo->isEnabled() && !redo->isEnabled());
    }

    void loadedBodyAndPanelTreeHaveSameFeatureCount()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("project.pcad");

        ModelingController source;
        QVERIFY(source.createBox().success);
        QVERIFY(source.createCylinder().success);
        QString error;
        QVERIFY(ProjectController(source).save(path, error));

        ModelingController loadedController;
        auto loaded = ProjectController::loadProject(path);
        QVERIFY2(loaded.success(), qPrintable(loaded.error));
        const auto expected = loaded.body.features().size();
        loadedController.replaceProject(std::move(loaded.document), std::move(loaded.body));

        FeatureEditorPanel panel;
        panel.setService(&loadedController);
        panel.setFeatures(loadedController.features());

        const auto trees = panel.findChildren<QTreeWidget*>();
        QCOMPARE(trees.size(), 1);
        QCOMPARE(trees.front()->topLevelItemCount(), 1);
        std::size_t featureItems = 0;
        for (QTreeWidgetItemIterator iterator(trees.front()); *iterator; ++iterator) {
            if ((*iterator)->data(0, Qt::UserRole + 1).isValid()) ++featureItems;
        }
        QCOMPARE(featureItems, expected);
    }
};

QTEST_MAIN(UndoPanelTests)
#include "undo_panel_tests.moc"
