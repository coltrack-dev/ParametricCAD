#include <QApplication>
#include <QAction>
#include <QtTest/QtTest>

#define private public
#include "viewer/MainWindow.h"
#undef private
#include "viewer/CadViewer.h"

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

    void persistentFeatureSelectionModesTransitionWithoutGlobalReset()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()
            || qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
            QSKIP("Native OCCT viewer is required for selection activation tests");
        }
        MainWindow window;
        const auto box = window.modeling_.createBox();
        QVERIFY(box.success);

        window.viewer_->setSelectionMode(CadViewer::SelectionMode::Object);
        window.viewer_->setSelectionMode(CadViewer::SelectionMode::Face);
        window.viewer_->setSelectionMode(CadViewer::SelectionMode::Edge);
        window.viewer_->setSelectionMode(CadViewer::SelectionMode::Vertex);
        window.viewer_->setSelectionMode(CadViewer::SelectionMode::Object);
        QVERIFY(window.viewer_->selectionMode() == CadViewer::SelectionMode::Object);
    }

    void validSketchEnablesVisibleRevolveAction()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()) {
            QSKIP("OCCT CadViewer requires an X display for MainWindow tests");
        }
        MainWindow window;
        const auto sketch = window.modeling_.createSketch();
        QVERIFY(sketch.success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {0, 0}, {20, 0}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {20, 0}, {20, 10}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {20, 10}, {0, 10}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {0, 10}, {0, 0}).success);
        window.applySelection({QString::fromStdString(sketch.id)});
        QVERIFY(window.revolveAction_->isEnabled());
    }

    void cancellingAxisPickPreservesRevolveSession()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()) {
            QSKIP("OCCT CadViewer requires an X display for MainWindow tests");
        }
        MainWindow window;
        const auto sketch = window.modeling_.createSketch();
        QVERIFY(sketch.success);
        QVERIFY(window.operationSession_.beginCreate(
            cad::application::InteractiveOperationKind::Revolve, {sketch.id}));
        window.beginRevolveAxisPick(QString::fromStdString(sketch.id), "SketchLine");
        QVERIFY(window.revolveAxisPicking_);
        window.cancelRevolveAxisPick();
        QVERIFY(!window.revolveAxisPicking_);
        QVERIFY(window.operationSession_.active());
        window.operationSession_.cancel();
    }

    void faceAttachedSketchLineToolSurvivesRepeatedLines()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()
            || qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
            QSKIP("Native OCCT viewer is required for this MainWindow regression test");
        }
        MainWindow window;
        const auto box = window.modeling_.createBox();
        QVERIFY(box.success);
        const auto sketch = window.modeling_.createSketchOnFace({{
            {box.id, cad::application::SelectionKind::Face, 1}}});
        QVERIFY(sketch.success);

        window.enterSketchEditing(sketch.id);
        window.handleSketchPoint({0.0, 0.0}, 0.1);
        window.handleSketchPoint({20.0, 0.0}, 0.1);
        window.handleSketchPoint({20.0, 0.0}, 0.1);
        window.handleSketchPoint({20.0, 10.0}, 0.1);

        const auto feature = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            window.modeling_.body().findFeature(sketch.id));
        QVERIFY(feature);
        QCOMPARE(feature->entityCount(), std::size_t(2));
        QVERIFY(std::visit([](const auto& entity) { return !entity.id.empty(); },
            feature->entities()[0]));
        QVERIFY(std::visit([](const auto& entity) { return !entity.id.empty(); },
            feature->entities()[1]));

        window.finishSketch();
        QVERIFY(window.activeSketchId_.empty());
        QVERIFY(!window.viewer_->sketchMode());
    }
};

QTEST_MAIN(MainWindowTests)
#include "main_window_tests.moc"
