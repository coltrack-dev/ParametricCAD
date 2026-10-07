#include <QApplication>
#include <QAction>
#include <QtTest/QtTest>

#define private public
#include "viewer/MainWindow.h"
#undef private

class MainWindowTests final : public QObject
{
    Q_OBJECT

private slots:
    void finishedFaceAttachedSketchEnablesVisibleExtrudeAction()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()) {
            QSKIP("OCCT CadViewer requires an X display for MainWindow tests");
        }
        MainWindow window;
        const auto box = window.modeling_.createBox();
        QVERIFY(box.success);
        const auto sketch = window.modeling_.createSketchOnFace({{
            {box.id, cad::application::SelectionKind::Face, 1}}});
        QVERIFY(sketch.success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {0, 0}, {20, 0}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {20, 0}, {20, 10}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {20, 10}, {0, 10}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {0, 10}, {0, 0}).success);

        window.activeSketchId_ = sketch.id;
        window.applySelection({QString::fromStdString(sketch.id)});
        QVERIFY(!window.extrudeAction_->isEnabled());

        window.finishSketch();
        QVERIFY(window.extrudeAction_->isEnabled());
    }

    void globalSketchTreeSelectionEnablesVisibleExtrudeAction()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()) {
            QSKIP("OCCT CadViewer requires an X display for MainWindow tests");
        }
        MainWindow window;
        const auto sketch = window.modeling_.createSketch();
        QVERIFY(sketch.success);
        window.applySelection({QString::fromStdString(sketch.id)});
        QVERIFY(window.extrudeAction_->isEnabled());
    }
};

QTEST_MAIN(MainWindowTests)
#include "main_window_tests.moc"
