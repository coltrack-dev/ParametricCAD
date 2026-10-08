#include <QApplication>
#include <QAction>
#include <QtTest/QtTest>

#define private public
#include "viewer/MainWindow.h"
#include "viewer/FeatureEditorPanel.h"
#undef private
#include "viewer/CadViewer.h"
#include "viewer/SketchEntityPicker.h"
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QMouseEvent>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <OpenGl_GraphicDriver.hxx>

class MainWindowTests final : public QObject
{
    Q_OBJECT

private slots:
    void sketchLinePickerChoosesNearestAndPrefersConstructionOnOverlap()
    {
        const std::vector<cad::viewer::SketchLineScreenCandidate> candidates{
            {"near", {0.0, 10.0}, {100.0, 10.0}, false},
            {"construction", {0.0, 0.0}, {100.0, 0.0}, true}};
        QCOMPARE(cad::viewer::pickSketchLine(candidates, {50.0, 1.0}, 8.0),
            std::optional<std::string>("construction"));
        QCOMPARE(cad::viewer::pickSketchLine(candidates, {50.0, 9.0}, 8.0),
            std::optional<std::string>("near"));
        QVERIFY(!cad::viewer::pickSketchLine(candidates, {50.0, 30.0}, 8.0));
    }

    void selectionOwnershipSurvivesShapeReplacementWithoutRendering()
    {
        // OCCT selection structures can be exercised without creating a
        // native view or initializing an OpenGL context.
        CadViewer viewer;
        const Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(
            Handle(Aspect_DisplayConnection)(), Standard_False);
        viewer.viewer_ = new V3d_Viewer(driver);
        viewer.context_ = new AIS_InteractiveContext(viewer.viewer_);
        viewer.initialized_ = true;
        const QString id = "sketch-selection-regression";
        viewer.display(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)), id, false);
        QCOMPARE(viewer.managedSelectionModes_.at(id), 0);
        const auto activeMode = [&viewer, &id]() {
            TColStd_ListOfInteger modes;
            viewer.context_->ActivatedModes(viewer.featureObjects_.at(id), modes);
            return modes;
        };
        QCOMPARE(activeMode().Extent(), 1);
        QCOMPARE(activeMode().First(), 0);
        viewer.setSelectionMode(CadViewer::SelectionMode::Face);
        QCOMPARE(viewer.managedSelectionModes_.at(id),
            AIS_Shape::SelectionMode(TopAbs_FACE));
        QCOMPARE(activeMode().Extent(), 1);
        QCOMPARE(activeMode().First(), AIS_Shape::SelectionMode(TopAbs_FACE));
        viewer.setSelectionMode(CadViewer::SelectionMode::Edge);
        QCOMPARE(activeMode().Extent(), 1);
        QCOMPARE(activeMode().First(), AIS_Shape::SelectionMode(TopAbs_EDGE));
        viewer.setSelectionMode(CadViewer::SelectionMode::Vertex);
        QCOMPARE(activeMode().Extent(), 1);
        QCOMPARE(activeMode().First(), AIS_Shape::SelectionMode(TopAbs_VERTEX));
        viewer.setSelectionMode(CadViewer::SelectionMode::Object);
        QCOMPARE(activeMode().Extent(), 1);
        QCOMPARE(activeMode().First(), 0);
        viewer.beginBulkUpdate();
        viewer.updateFeature(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(20, 0, 0)), id);
        QVERIFY(!viewer.managedSelectionModes_.contains(id));
        viewer.updateFeature(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(30, 0, 0)), id);
        QVERIFY(!viewer.managedSelectionModes_.contains(id));
        viewer.endBulkUpdate();
        viewer.setSelectionMode(CadViewer::SelectionMode::Face);
        viewer.enterSketchMode(gp_Pnt(), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0), gp_Dir(0, 0, 1), id);
        QVERIFY(!viewer.managedSelectionModes_.contains(id));
        viewer.updateFeature(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(40, 0, 0)), id);
        viewer.updateFeature(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(50, 0, 0)), id);
        QVERIFY(!viewer.managedSelectionModes_.contains(id));
        TColStd_ListOfInteger modes;
        viewer.context_->ActivatedModes(viewer.featureObjects_.at(id), modes);
        QVERIFY(modes.IsEmpty());
        viewer.exitSketchMode();
        QCOMPARE(viewer.managedSelectionModes_.at(id), AIS_Shape::SelectionMode(TopAbs_FACE));
        modes.Clear();
        viewer.context_->ActivatedModes(viewer.featureObjects_.at(id), modes);
        QCOMPARE(modes.Extent(), 1);
        QCOMPARE(modes.First(), AIS_Shape::SelectionMode(TopAbs_FACE));
    }

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

    void finishedSketchWithConstructionLineEnablesVisibleRevolveAction()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()) {
            QSKIP("OCCT CadViewer requires an X display for MainWindow tests");
        }
        MainWindow window;
        const auto sketch = window.modeling_.createSketch();
        QVERIFY(sketch.success);
        window.refreshModelView();
        window.enterSketchEditing(sketch.id);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {0, 0}, {20, 0}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {20, 0}, {20, 10}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {20, 10}, {0, 10}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {0, 10}, {0, 0}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {0, -10}, {0, 20}, true).success);

        window.finishSketch();

        QTreeWidgetItem* sketchItem = nullptr;
        for (QTreeWidgetItemIterator iterator(window.featureEditorPanel_->tree_);
             *iterator; ++iterator) {
            if ((*iterator)->data(0, Qt::UserRole + 1).toString()
                == QString::fromStdString(sketch.id)) {
                sketchItem = *iterator;
                break;
            }
        }
        QVERIFY(sketchItem != nullptr);
        window.featureEditorPanel_->tree_->clearSelection();
        sketchItem->setSelected(true);
        window.featureEditorPanel_->tree_->setCurrentItem(sketchItem);
        QCoreApplication::processEvents();
        QVERIFY(window.revolveAction_->isEnabled());
    }

    void constructionToggleActionMarksSelectedSketchLine()
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
        const auto separateLine = window.modeling_.addSketchLine(
            sketch.id, {0, -10}, {0, 20});
        QVERIFY(separateLine.success);
        const auto sketchFeature = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            window.modeling_.body().findFeature(sketch.id));
        QVERIFY(sketchFeature);
        const auto lineId = std::get<cad::parametric::SketchLine>(
            sketchFeature->entities().back()).id;

        std::optional<std::size_t> edgeIndex;
        for (std::size_t index = 1; index <= 16; ++index) {
            cad::parametric::RevolveAxisDefinition axis;
            const auto resolved = window.modeling_.resolveRevolveAxis(
                sketch.id,
                cad::application::SelectionSnapshot{{
                    {sketch.id, cad::application::SelectionKind::Edge, index}}},
                axis);
            if (resolved.success && axis.sketchLineId == lineId) {
                edgeIndex = index;
                break;
            }
        }
        QVERIFY(edgeIndex.has_value());

        window.applySelectionSnapshot(cad::application::SelectionSnapshot{{
            {sketch.id, cad::application::SelectionKind::Edge, edgeIndex}}}, false);
        QVERIFY(window.sketchConstructionAction_->isEnabled());
        QVERIFY(!window.sketchConstructionAction_->isChecked());
        window.sketchConstructionAction_->trigger();

        const auto updated = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            window.modeling_.body().findFeature(sketch.id));
        QVERIFY(updated);
        QCOMPARE(std::get<cad::parametric::SketchLine>(updated->entities().back()).id, lineId);
        QVERIFY(std::get<cad::parametric::SketchLine>(updated->entities().back()).construction);
        window.applySelection({QString::fromStdString(sketch.id)});
        QVERIFY(window.revolveAction_->isEnabled());
    }

    void sketchLineAxisPickerConsumesClickAndCreatesRevolve()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()) {
            QSKIP("OCCT CadViewer requires an X display for MainWindow tests");
        }
        MainWindow window;
        window.show();
        QCoreApplication::processEvents();
        const auto sketch = window.modeling_.createSketch();
        QVERIFY(sketch.success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {0, 0}, {20, 0}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {20, 0}, {20, 10}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {20, 10}, {0, 10}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {0, 10}, {0, 0}).success);
        QVERIFY(window.modeling_.addSketchLine(sketch.id, {0, -10}, {0, 20}, true).success);
        window.refreshModelView(false);
        window.applySelection({QString::fromStdString(sketch.id)});
        const auto sketchFeature = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            window.modeling_.body().findFeature(sketch.id));
        QVERIFY(sketchFeature);
        const auto axisId = std::get<cad::parametric::SketchLine>(
            sketchFeature->entities().back()).id;

        window.beginRevolveAxisPick(QString::fromStdString(sketch.id), "SketchLine", false);
        QVERIFY(window.revolveAxisPicking_);
        QPoint hit;
        for (int y = 0; y < window.viewer_->height() && hit.isNull(); y += 2) {
            for (int x = 0; x < window.viewer_->width(); x += 2) {
                if (window.viewer_->sketchLineAtScreen({x, y}) == axisId) {
                    hit = {x, y};
                    break;
                }
            }
        }
        QVERIFY(!hit.isNull());
        QMouseEvent event(QEvent::MouseButtonPress, QPointF(hit), QPointF(hit),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        window.viewer_->mousePressEvent(&event);
        QCoreApplication::processEvents();

        QVERIFY(!window.revolveAxisPicking_);
        std::shared_ptr<cad::parametric::RevolveFeature> revolve;
        for (const auto& feature : window.modeling_.body().features()) {
            revolve = std::dynamic_pointer_cast<cad::parametric::RevolveFeature>(feature);
            if (revolve) break;
        }
        QVERIFY(revolve);
        QVERIFY(!revolve->shape().IsNull());
        QCOMPARE(revolve->axisDefinition().type,
            cad::parametric::RevolveAxisType::SketchLine);
        QCOMPARE(revolve->axisDefinition().sketchLineId, axisId);
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

    void faceAttachedSketchLineToolSurvivesRepeatedLines_data()
    {
        QTest::addColumn<bool>("faceAttached");
        QTest::newRow("face-attached") << true;
        QTest::newRow("global") << false;
    }

    void faceAttachedSketchLineToolSurvivesRepeatedLines()
    {
        QFETCH(bool, faceAttached);
        if (qEnvironmentVariable("DISPLAY").isEmpty()
            || qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
            QSKIP("Native OCCT viewer is required for this MainWindow regression test");
        }
        MainWindow window;
        const auto box = window.modeling_.createBox();
        QVERIFY(box.success);
        const auto sketch = faceAttached
            ? window.modeling_.createSketchOnFace({{
                {box.id, cad::application::SelectionKind::Face, 1}}})
            : window.modeling_.createSketch();
        QVERIFY(sketch.success);

        const auto sketchId = QString::fromStdString(sketch.id);
        window.refreshModelView();
        window.viewer_->setSelectionMode(CadViewer::SelectionMode::Face);
        window.enterSketchEditing(sketch.id);
        QVERIFY(!window.viewer_->managedSelectionModes_.contains(sketchId));
        QCOMPARE(window.viewer_->editingSketchFeatureId_, sketchId);
        const auto initialFeature = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            window.modeling_.body().findFeature(sketch.id));
        QVERIFY(initialFeature);
        const auto initialCount = initialFeature->entityCount();
        window.handleSketchPoint({0.0, 0.0}, 0.1);
        window.handleSketchPoint({20.0, 0.0}, 0.1);
        window.refreshModelView();
        QVERIFY(!window.viewer_->managedSelectionModes_.contains(sketchId));
        window.handleSketchPoint({20.0, 0.0}, 0.1);
        window.handleSketchPoint({20.0, 10.0}, 0.1);
        window.refreshModelView();
        QVERIFY(!window.viewer_->managedSelectionModes_.contains(sketchId));

        const auto feature = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            window.modeling_.body().findFeature(sketch.id));
        QVERIFY(feature);
        QCOMPARE(feature->entityCount(), initialCount + 2);
        QVERIFY(std::visit([](const auto& entity) { return !entity.id.empty(); },
            feature->entities()[initialCount]));
        QVERIFY(std::visit([](const auto& entity) { return !entity.id.empty(); },
            feature->entities()[initialCount + 1]));

        window.finishSketch();
        QVERIFY(window.activeSketchId_.empty());
        QVERIFY(!window.viewer_->sketchMode());
        QVERIFY(window.viewer_->editingSketchFeatureId_.isEmpty());
        QCOMPARE(window.viewer_->managedSelectionModes_.at(sketchId),
            AIS_Shape::SelectionMode(TopAbs_FACE));
    }

    void shapeReplacementReleasesSelectionBeforeMutation()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()
            || qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
            QSKIP("Native OCCT viewer is required for selection replacement tests");
        }
        MainWindow window;
        const auto box = window.modeling_.createBox();
        QVERIFY(box.success);
        window.refreshModelView();
        auto* viewer = window.viewer_;
        const auto id = QString::fromStdString(box.id);
        viewer->setSelectionMode(CadViewer::SelectionMode::Object);
        QCOMPARE(viewer->managedSelectionModes_.at(id), 0);
        viewer->beginBulkUpdate();
        viewer->updateFeature(BRepPrimAPI_MakeBox(20, 30, 40).Shape(), id);
        QVERIFY(!viewer->managedSelectionModes_.contains(id));
        viewer->updateFeature(BRepPrimAPI_MakeBox(30, 40, 50).Shape(), id);
        QVERIFY(!viewer->managedSelectionModes_.contains(id));
        viewer->endBulkUpdate();
        QCOMPARE(viewer->managedSelectionModes_.at(id), 0);
        viewer->setSelectionMode(CadViewer::SelectionMode::Face);
        QCOMPARE(viewer->managedSelectionModes_.at(id), AIS_Shape::SelectionMode(TopAbs_FACE));
    }
};

QTEST_MAIN(MainWindowTests)
#include "main_window_tests.moc"
