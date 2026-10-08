#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QEvent>
#include <QPushButton>
#include <QToolBar>
#include <QSettings>
#include <QSet>
#include <array>
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
    void groupedToolbarsReuseExistingActions()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()) {
            QSKIP("Native MainWindow is required for toolbar construction tests");
        }
        MainWindow window;
        const auto toolbar = [&window](const QString& objectName) {
            return window.findChild<QToolBar*>(objectName);
        };
        for (const auto& name : {QStringLiteral("File and HistoryToolBar"),
                                 QStringLiteral("SketchToolBar"),
                                 QStringLiteral("ConstraintsToolBar"),
                                 QStringLiteral("Solid ModelingToolBar"),
                                 QStringLiteral("ModifyToolBar"),
                                 QStringLiteral("TransformToolBar"),
                                 QStringLiteral("SelectionToolBar"),
                                 QStringLiteral("ViewToolBar"),
                                 QStringLiteral("VisibilityToolBar"),
                                 QStringLiteral("BIMToolBar")}) {
            QVERIFY(toolbar(name));
            QCOMPARE(toolbar(name)->iconSize(), QSize(24, 24));
        }
        QVERIFY(window.findChild<QToolBar*>("SketchToolBar")->actions().contains(
            window.createSketchAction_));
        QVERIFY(window.findChild<QToolBar*>("Solid ModelingToolBar")->actions().contains(
            window.sweepAction_));
        QVERIFY(window.findChild<QToolBar*>("File and HistoryToolBar")->actions().contains(
            window.undoAction_));
        QVERIFY(window.newAction_->shortcut() == QKeySequence::New);
        QVERIFY(!window.createSketchAction_->icon().isNull());
        QVERIFY(!window.sweepAction_->icon().isNull());
        QSet<const QAction*> toolbarActions;
        QSet<quint64> iconKeys;
        const auto groupedToolbars = [&window]() {
            QList<QToolBar*> result;
            for (const auto& name : {QStringLiteral("File and HistoryToolBar"),
                                     QStringLiteral("SketchToolBar"),
                                     QStringLiteral("ConstraintsToolBar"),
                                     QStringLiteral("Solid ModelingToolBar"),
                                     QStringLiteral("ModifyToolBar"),
                                     QStringLiteral("TransformToolBar"),
                                     QStringLiteral("SelectionToolBar"),
                                     QStringLiteral("ViewToolBar"),
                                     QStringLiteral("VisibilityToolBar"),
                                     QStringLiteral("BIMToolBar")}) {
                if (auto* toolbar = window.findChild<QToolBar*>(name)) result.append(toolbar);
            }
            return result;
        }();
        for (auto* toolbar : groupedToolbars) {
            for (auto* action : toolbar->actions()) {
                if (!action || action->isSeparator() || toolbarActions.contains(action)) continue;
                toolbarActions.insert(action);
                QVERIFY2(!action->icon().isNull(), qPrintable(
                    QString("Missing toolbar icon for %1").arg(action->text())));
                QVERIFY2(!action->icon().pixmap(QSize(24, 24), QIcon::Normal).isNull(), qPrintable(
                    QString("Icon does not render for %1").arg(action->text())));
                QVERIFY2(!action->icon().pixmap(QSize(24, 24), QIcon::Disabled).isNull(), qPrintable(
                    QString("Disabled icon does not render for %1").arg(action->text())));
                QVERIFY2(!iconKeys.contains(action->icon().cacheKey()), qPrintable(
                    QString("Duplicate toolbar icon for %1").arg(action->text())));
                iconKeys.insert(action->icon().cacheKey());
            }
        }
        QVERIFY(toolbarActions.size() >= 50);
        QVERIFY(window.viewer_->objectSelectionAction()->actionGroup());
        QVERIFY(window.viewer_->objectSelectionAction()->actionGroup()->isExclusive());
        QVERIFY(window.viewer_->faceSelectionAction()->actionGroup()
            == window.viewer_->objectSelectionAction()->actionGroup());
        QVERIFY(!window.saveState(1).isEmpty());
        QSettings settings("ParametricCAD", "ParametricCAD");
        const auto oldGeometry = settings.value("mainWindow/geometry");
        const auto oldState = settings.value("mainWindow/state");
        window.saveWindowLayout();
        QVERIFY(settings.value("mainWindow/geometry").isValid());
        QVERIFY(settings.value("mainWindow/state").isValid());
        if (oldGeometry.isValid()) settings.setValue("mainWindow/geometry", oldGeometry);
        else settings.remove("mainWindow/geometry");
        if (oldState.isValid()) settings.setValue("mainWindow/state", oldState);
        else settings.remove("mainWindow/state");
    }

    void rectangleInputKeepsFourDirectionsAndExactDimensions()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()) {
            QSKIP("Native MainWindow is required for Sketch input tests");
        }
        MainWindow window;
        const auto sketch = window.modeling_.createSketch();
        QVERIFY(sketch.success);
        window.activeSketchId_ = sketch.id;
        window.sketchModeState_ = MainWindow::SketchModeState::Editing;
        window.selectSketchRectangleTool();

        const std::array<gp_Pnt2d, 4> cursors{
            gp_Pnt2d(20.0, 10.0), gp_Pnt2d(-20.0, 10.0),
            gp_Pnt2d(-20.0, -10.0), gp_Pnt2d(20.0, -10.0)};
        for (const auto& cursor : cursors) {
            window.sketchFirstPoint_ = gp_Pnt2d(0.0, 0.0);
            window.rectangleState_ = MainWindow::RectangleState::Drawing;
            window.updateRectangleInput(cursor);
            const auto end = window.rectanglePointForCursor(cursor);
            QVERIFY(end);
            QCOMPARE(end->X(), cursor.X());
            QCOMPARE(end->Y(), cursor.Y());
            window.sketchFirstPoint_.reset();
            window.clearRectangleInput();
        }

        window.sketchFirstPoint_ = gp_Pnt2d(0.0, 0.0);
        window.rectangleState_ = MainWindow::RectangleState::Drawing;
        window.updateRectangleInput(gp_Pnt2d(20.0, 10.0));
        QKeyEvent tabEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QVERIFY(window.eventFilter(window.rectangleWidthEdit_, &tabEvent));
        QKeyEvent escapeEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QVERIFY(window.eventFilter(window.rectangleHeightEdit_, &escapeEvent));
        QVERIFY(!window.sketchFirstPoint_.has_value());

        window.sketchFirstPoint_ = gp_Pnt2d(10.0, 20.0);
        window.rectangleState_ = MainWindow::RectangleState::Drawing;
        window.updateRectangleInput(gp_Pnt2d(-100.0, -100.0));
        window.rectangleWidthEdit_->setText("80");
        window.rectangleHeightEdit_->setText("45");
        window.commitRectangleFromInput();
        const auto updated = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            window.modeling_.body().findFeature(sketch.id));
        QVERIFY(updated);
        QCOMPARE(updated->entities().size(), std::size_t(4));
        const auto* first = std::get_if<cad::parametric::SketchLine>(&updated->entities()[0]);
        const auto* second = std::get_if<cad::parametric::SketchLine>(&updated->entities()[1]);
        QVERIFY(first && second);
        QCOMPARE(first->start.X(), 10.0);
        QCOMPARE(first->start.Y(), 20.0);
        QCOMPARE(first->end.X(), -70.0);
        QCOMPARE(first->end.Y(), 20.0);
        QCOMPARE(second->end.X(), -70.0);
        QCOMPARE(second->end.Y(), -25.0);
        QVERIFY(window.sketchFirstPoint_.has_value() == false);
    }

    void sketchEditingStartsWithoutDrawingTool()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()) {
            QSKIP("Native MainWindow is required for Sketch lifecycle tests");
        }
        MainWindow window;
        const auto sketch = window.modeling_.createSketch();
        QVERIFY(sketch.success);
        window.enterSketchEditing(sketch.id);
        QCOMPARE(window.sketchModeState_, MainWindow::SketchModeState::Editing);
        QCOMPARE(window.sketchTool_, MainWindow::SketchTool::None);
        QCOMPARE(window.rectangleState_, MainWindow::RectangleState::Ready);
        QCOMPARE(window.viewer_->sketchPreviewTool_, CadViewer::SketchPreviewTool::None);
        QVERIFY(window.viewer_->sketchPreviewObject_.IsNull());
        QVERIFY(window.rectangleWidthEdit_->testAttribute(Qt::WA_TransparentForMouseEvents));
        QVERIFY(window.rectangleHeightEdit_->testAttribute(Qt::WA_TransparentForMouseEvents));
        QVERIFY(window.sketchFirstPoint_.has_value() == false);
        QMouseEvent move(QEvent::MouseMove, QPointF(100.0, 100.0),
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        window.viewer_->mouseMoveEvent(&move);
        QVERIFY(window.viewer_->sketchPreviewObject_.IsNull());
        const auto edited = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            window.modeling_.body().findFeature(sketch.id));
        QVERIFY(edited);
        QCOMPARE(edited->entities().size(), std::size_t(0));

        window.selectSketchRectangleTool();
        QKeyEvent readyEscape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        window.viewer_->keyPressEvent(&readyEscape);
        QCOMPARE(window.sketchTool_, MainWindow::SketchTool::None);
        QCOMPARE(window.rectangleState_, MainWindow::RectangleState::Ready);

        window.selectSketchRectangleTool();
        window.sketchFirstPoint_ = gp_Pnt2d(1.0, 1.0);
        window.rectangleState_ = MainWindow::RectangleState::Drawing;
        QKeyEvent drawingEscape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        window.viewer_->keyPressEvent(&drawingEscape);
        QCOMPARE(window.sketchTool_, MainWindow::SketchTool::Rectangle);
        QCOMPARE(window.rectangleState_, MainWindow::RectangleState::Ready);
        QVERIFY(!window.sketchFirstPoint_.has_value());

    }

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

    void sweepStagesPathBeforeCommit()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()) {
            QSKIP("OCCT CadViewer requires an X display for MainWindow tests");
        }
        MainWindow window;
        const auto sketch = window.modeling_.createSketch();
        QVERIFY(sketch.success);
        QVERIFY(window.modeling_.addSketchCircle(sketch.id, {0.0, 0.0}, 5.0).success);
        const auto box = window.modeling_.createBox();
        QVERIFY(box.success);
        window.refreshModelView();
        window.applySelection({QString::fromStdString(sketch.id)});

        window.createSweep();
        QVERIFY(window.operationSession_.active());
        QVERIFY(window.sweepDialog_);
        QCOMPARE(window.sweepPathLabel_->text(), QString("Path: <not selected>"));
        QVERIFY(!window.sweepPathSelection_);
        QVERIFY(!window.modeling_.body().findFeature("sweep"));

        const auto buttons = window.sweepDialog_->findChildren<QPushButton*>();
        const auto pickButton = std::find_if(buttons.begin(), buttons.end(),
            [](const auto* button) { return button->text() == "Pick Path"; });
        const auto commitButton = std::find_if(buttons.begin(), buttons.end(),
            [](const auto* button) { return button->text() == "Commit"; });
        QVERIFY(pickButton != buttons.end());
        QVERIFY(commitButton != buttons.end());
        (*pickButton)->click();
        window.applySelectionSnapshot({});
        QVERIFY(window.operationSession_.active());
        const cad::application::SelectionSnapshot pathSelection{{
            {box.id, cad::application::SelectionKind::Edge, 1}}};
        QVERIFY(window.handleSweepPathSelection(pathSelection));
        QVERIFY(window.sweepPathSelection_);
        QVERIFY(window.sweepPathReference_);
        QVERIFY(window.operationSession_.active());
        QCOMPARE(window.operationSession_.context().kind,
            cad::application::InteractiveOperationKind::Sweep);
        QCOMPARE(window.sweepPathLabel_->text(), QString("Path: Box | Edge 1"));
        QVERIFY(window.sweepCommitButton_->isEnabled());
        QVERIFY(window.modeling_.body().features().size() == 2);

        (*commitButton)->click();
        QVERIFY(!window.operationSession_.active());
        QVERIFY(!window.sweepDialog_);
        QVERIFY(window.modeling_.body().features().size() == 3);
        QVERIFY(window.modeling_.body().features().back()->typeId() == std::string("Sweep"));
    }

    void cancellingSweepLeavesModelUnchanged()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()) {
            QSKIP("OCCT CadViewer requires an X display for MainWindow tests");
        }
        MainWindow window;
        const auto sketch = window.modeling_.createSketch();
        QVERIFY(sketch.success);
        QVERIFY(window.modeling_.addSketchCircle(sketch.id, {0.0, 0.0}, 5.0).success);
        const auto box = window.modeling_.createBox();
        QVERIFY(box.success);
        window.refreshModelView();
        window.applySelection({QString::fromStdString(sketch.id)});
        window.createSweep();
        QVERIFY(window.operationSession_.active());
        QVERIFY(window.sweepDialog_);

        const auto buttons = window.sweepDialog_->findChildren<QPushButton*>();
        const auto cancelButton = std::find_if(buttons.begin(), buttons.end(),
            [](const auto* button) { return button->text() == "Cancel"; });
        QVERIFY(cancelButton != buttons.end());
        (*cancelButton)->click();

        QVERIFY(!window.operationSession_.active());
        QVERIFY(!window.sweepDialog_);
        QCOMPARE(window.modeling_.body().features().size(), std::size_t(2));
        for (const auto& feature : window.modeling_.body().features()) {
            QVERIFY(feature->typeId() != std::string("Sweep"));
        }
    }

    void sweepAcceptsSketchPathFromTreeBeforeCommit()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()) {
            QSKIP("OCCT CadViewer requires an X display for MainWindow tests");
        }
        MainWindow window;
        const auto profile = window.modeling_.createSketch(
            cad::parametric::SketchSupportType::YZ);
        QVERIFY(profile.success);
        QVERIFY(window.modeling_.addSketchCircle(profile.id, {0.0, 0.0}, 5.0).success);
        const auto path = window.modeling_.createSketch(
            cad::parametric::SketchSupportType::XZ);
        QVERIFY(path.success);
        QVERIFY(window.modeling_.addSketchLine(
            path.id, {0.0, 0.0}, {100.0, 0.0}).success);
        window.refreshModelView();
        window.applySelection({QString::fromStdString(profile.id)});
        window.createSweep();
        QVERIFY(window.operationSession_.active());
        QVERIFY(window.sweepDialog_);

        const auto buttons = window.sweepDialog_->findChildren<QPushButton*>();
        const auto pickButton = std::find_if(buttons.begin(), buttons.end(),
            [](const auto* button) { return button->text() == "Pick Path"; });
        const auto commitButton = std::find_if(buttons.begin(), buttons.end(),
            [](const auto* button) { return button->text() == "Commit"; });
        QVERIFY(pickButton != buttons.end());
        QVERIFY(commitButton != buttons.end());
        (*pickButton)->click();

        QTreeWidgetItem* pathItem = nullptr;
        for (QTreeWidgetItemIterator iterator(window.featureEditorPanel_->tree_);
             *iterator; ++iterator) {
            if ((*iterator)->data(0, Qt::UserRole + 1).toString()
                == QString::fromStdString(path.id)) {
                pathItem = *iterator;
                break;
            }
        }
        QVERIFY(pathItem);
        {
            const QSignalBlocker blocker(window.featureEditorPanel_->tree_);
            window.featureEditorPanel_->tree_->clearSelection();
            pathItem->setSelected(true);
            window.featureEditorPanel_->tree_->setCurrentItem(pathItem);
        }
        QVERIFY(QMetaObject::invokeMethod(
            window.featureEditorPanel_->tree_, "itemSelectionChanged", Qt::DirectConnection));

        QVERIFY(window.operationSession_.active());
        QCOMPARE(window.operationSession_.context().kind,
            cad::application::InteractiveOperationKind::Sweep);
        QCOMPARE(window.sweepPathLabel_->text(), QString("Path: Sketch | Entities: 1"));
        QVERIFY(window.sweepCommitButton_->isEnabled());
        QCOMPARE(window.modeling_.body().features().size(), std::size_t(2));

        (*commitButton)->click();
        QVERIFY(!window.operationSession_.active());
        QVERIFY(window.modeling_.body().features().size() == 3);
        QVERIFY(window.modeling_.body().features().back()->typeId() == std::string("Sweep"));
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

    void faceAttachedRectangleCommitsOnCurrentFaceFrame()
    {
        if (qEnvironmentVariable("DISPLAY").isEmpty()
            || qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
            QSKIP("Native OCCT viewer is required for face-attached Rectangle tests");
        }
        MainWindow window;
        const auto box = window.modeling_.createBox();
        QVERIFY(box.success);
        const auto sketch = window.modeling_.createSketchOnFace({{
            {box.id, cad::application::SelectionKind::Face, 1}}});
        QVERIFY(sketch.success);

        window.refreshModelView();
        window.enterSketchEditing(sketch.id);
        window.selectSketchRectangleTool();
        const auto before = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            window.modeling_.body().findFeature(sketch.id));
        QVERIFY(before);
        QCOMPARE(before->entityCount(), std::size_t{0});

        window.handleSketchPoint({0.0, 0.0}, 0.1);
        QCOMPARE(window.rectangleState_, MainWindow::RectangleState::Drawing);
        window.handleSketchPoint({40.0, 20.0}, 0.1);

        const auto after = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            window.modeling_.body().findFeature(sketch.id));
        QVERIFY(after);
        QCOMPARE(after->entityCount(), std::size_t{4});
        QCOMPARE(window.rectangleState_, MainWindow::RectangleState::Ready);
        QVERIFY(!window.sketchFirstPoint_.has_value());
        QVERIFY(window.viewer_->sketchPreviewObject_.IsNull());

        const auto frame = after->currentFrame();
        for (const auto& entity : after->entities()) {
            const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
            QVERIFY(line);
            const gp_Pnt start = frame.origin.Translated(
                gp_Vec(frame.xDirection) * line->start.X()
                + gp_Vec(frame.yDirection) * line->start.Y());
            const gp_Pnt end = frame.origin.Translated(
                gp_Vec(frame.xDirection) * line->end.X()
                + gp_Vec(frame.yDirection) * line->end.Y());
            QVERIFY(std::abs(gp_Vec(frame.origin, start).Dot(gp_Vec(frame.normal))) < 1.0e-7);
            QVERIFY(std::abs(gp_Vec(frame.origin, end).Dot(gp_Vec(frame.normal))) < 1.0e-7);
        }
    }

    void rectangleMouseClickCommitsExactlyFourEntities_data()
    {
        QTest::addColumn<bool>("faceAttached");
        QTest::newRow("xy") << false;
        QTest::newRow("face-attached") << true;
    }

    void rectangleMouseClickCommitsExactlyFourEntities()
    {
        QFETCH(bool, faceAttached);
        if (qEnvironmentVariable("DISPLAY").isEmpty()
            || qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
            QSKIP("Native OCCT viewer is required for Qt mouse Rectangle tests");
        }
        MainWindow window;
        const auto box = window.modeling_.createBox();
        const auto sketch = faceAttached
            ? window.modeling_.createSketchOnFace({{
                {box.id, cad::application::SelectionKind::Face, 1}}})
            : window.modeling_.createSketch();
        QVERIFY(!faceAttached || box.success);
        QVERIFY(sketch.success);

        window.show();
        QCoreApplication::processEvents();
        window.enterSketchEditing(sketch.id);
        window.selectSketchRectangleTool();
        QCoreApplication::processEvents();

        const auto first = window.viewer_->sketchPointToScreen({0.0, 0.0});
        const auto second = window.viewer_->sketchPointToScreen({40.0, 20.0});
        QVERIFY(first);
        QVERIFY(second);
        QVERIFY(window.viewer_->rect().contains(*first));
        QVERIFY(window.viewer_->rect().contains(*second));
        const auto selectionBefore = window.viewer_->selectionSnapshot();

        QTest::mouseClick(window.viewer_, Qt::LeftButton, Qt::NoModifier, *first);
        QTest::mouseMove(window.viewer_, *second);
        QTest::mouseClick(window.viewer_, Qt::LeftButton, Qt::NoModifier, *second);
        QCoreApplication::processEvents();

        const auto updated = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            window.modeling_.body().findFeature(sketch.id));
        QVERIFY(updated);
        QCOMPARE(updated->entityCount(), std::size_t{4});
        QCOMPARE(window.rectangleState_, MainWindow::RectangleState::Ready);
        QVERIFY(!window.sketchFirstPoint_.has_value());
        QVERIFY(window.viewer_->sketchPreviewObject_.IsNull());
        QCOMPARE(window.viewer_->selectionSnapshot().items, selectionBefore.items);
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
