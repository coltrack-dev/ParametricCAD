#include "viewer/FeatureEditorPanel.h"
#include "commands/FeatureCommands.h"
#include "operations/ParametricFeatures.h"
#include <QtTest/QtTest>
#include <QAction>
#include <QDoubleSpinBox>
#include <QUndoStack>

using namespace cad::parametric;

class UndoPanelTests final : public QObject
{
    Q_OBJECT
private slots:
    void editingFinishedIsOneCommand()
    {
        Body body;
        auto cylinder = std::make_shared<CylinderParametricFeature>("cylinder", 5, 20);
        body.addFeature(cylinder);
        QVERIFY(body.recompute());
        QUndoStack stack;
        FeatureEditorPanel panel;
        panel.setUndoStack(&stack);
        panel.setBody(&body);
        connect(&stack, &QUndoStack::indexChanged, &panel, &FeatureEditorPanel::scheduleRefresh);
        panel.selectFeatures({"cylinder"});
        auto* radius = panel.findChildren<QDoubleSpinBox*>().front();
        radius->setValue(6);
        radius->setValue(7);
        radius->setValue(10);
        QCOMPARE(stack.count(), 0);
        QCOMPARE(cylinder->radius(), 5.0);
        QVERIFY(QMetaObject::invokeMethod(radius, "editingFinished", Qt::DirectConnection));
        QCOMPARE(stack.count(), 1);
        QCOMPARE(stack.undoText(), QString("Change Cylinder Radius"));
        QCOMPARE(cylinder->radius(), 10.0);
        QCoreApplication::processEvents();
        stack.undo();
        QCoreApplication::processEvents();
        QCOMPARE(stack.count(), 1);
        QCOMPARE(cylinder->radius(), 5.0);
        QCOMPARE(panel.findChildren<QDoubleSpinBox*>().front()->value(), 5.0);
        stack.redo();
        QCoreApplication::processEvents();
        QCOMPARE(cylinder->radius(), 10.0);
        QCOMPARE(panel.findChildren<QDoubleSpinBox*>().front()->value(), 10.0);
    }

    void deletingSelectedFeatureClearsSelection()
    {
        Body body;
        auto sketch = std::make_shared<SketchFeature>("sketch", 10, 6);
        auto face = std::make_shared<FaceFeature>("face", sketch);
        body.addFeature(sketch);
        body.addFeature(face);
        QVERIFY(body.recompute());
        QUndoStack stack;
        FeatureEditorPanel panel;
        panel.setUndoStack(&stack);
        panel.setBody(&body);
        connect(&stack, &QUndoStack::indexChanged, &panel, &FeatureEditorPanel::scheduleRefresh);
        panel.selectFeatures({"sketch"});
        QVERIFY(!panel.findChildren<QDoubleSpinBox*>().isEmpty());
        stack.push(new cad::commands::RemoveFeatureCommand(body, "sketch"));
        QCoreApplication::processEvents();
        QVERIFY(panel.selectedFeatureIds().isEmpty());
        QVERIFY(panel.findChildren<QDoubleSpinBox*>().isEmpty());
        stack.undo();
        QCoreApplication::processEvents();
        panel.selectFeatures({"face"});
        QCOMPARE(panel.selectedFeatureIds(), QStringList{"face"});
        stack.redo();
        QCoreApplication::processEvents();
        QVERIFY(panel.selectedFeatureIds().isEmpty());
    }

    void shortcutsAndCleanActions()
    {
        Body body;
        auto cylinder = std::make_shared<CylinderParametricFeature>("cylinder", 5, 20);
        body.addFeature(cylinder);
        QVERIFY(body.recompute());
        QUndoStack stack;
        FeatureEditorPanel panel;
        panel.setUndoStack(&stack);
        panel.setBody(&body);
        connect(&stack, &QUndoStack::indexChanged, &panel, &FeatureEditorPanel::scheduleRefresh);
        auto* undo = stack.createUndoAction(&panel);
        auto* redo = stack.createRedoAction(&panel);
        QVERIFY(!undo->isEnabled() && !redo->isEnabled());
        panel.selectFeatures({"cylinder"});
        panel.show();
        panel.activateWindow();
        auto* radius = panel.findChildren<QDoubleSpinBox*>().front();
        radius->setFocus();
        QTRY_VERIFY(radius->hasFocus());
        radius->setValue(9);
        QTest::keyClick(radius, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(cylinder->radius(), 5.0);
        QCOMPARE(stack.count(), 1);
        QVERIFY(stack.isClean());
        QVERIFY(redo->isEnabled());
        QCoreApplication::processEvents();
        radius = panel.findChildren<QDoubleSpinBox*>().front();
        radius->setFocus();
        QTest::keyClick(radius, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(cylinder->radius(), 9.0);
        QCOMPARE(stack.count(), 1);
        QVERIFY(undo->isEnabled());
        stack.setClean();
        QVERIFY(stack.isClean());
        stack.clear();
        QVERIFY(!undo->isEnabled() && !redo->isEnabled());
    }
};

QTEST_MAIN(UndoPanelTests)
#include "undo_panel_tests.moc"
