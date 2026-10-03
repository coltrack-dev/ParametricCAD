#include "viewer/MainWindow.h"
#include "viewer/CadViewer.h"
#include "viewer/FeatureEditorPanel.h"
#include "viewer/ModelPresenter.h"
#include "operations/SketchTrimService.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QKeySequence>
#include <QMessageBox>
#include <QInputDialog>
#include <QStandardPaths>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <algorithm>

namespace
{
constexpr int ModelPanelWidth = 320;
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      project_(modeling_),
      viewer_(new CadViewer(this)),
      presenter_(std::make_unique<cad::viewer::ModelPresenter>(modeling_.body(), *viewer_))
{
    updateTitle();
    setCentralWidget(viewer_);

    createActions();
    createParametricPanel();
    viewer_->setPushPullCommittedHandler(
        [this](const QString& featureId, const int faceIndex,
               const gp_Vec& normal, const double distance) {
            reportResult(modeling_.pushPull(
                featureId.toStdString(), faceIndex, normal, distance));
        }
    );
    viewer_->setTransformCommittedHandler(
        [this](const QString& featureId, const gp_Trsf& delta) {
            reportResult(modeling_.transformFeatureDelta(
                featureId.toStdString(), delta));
        }
    );
    viewer_->setTransformCopyCommittedHandler(
        [this](const QString& featureId, const gp_Trsf& delta) {
            reportResult(modeling_.duplicateFeatureWithDelta(
                featureId.toStdString(), delta));
        }
    );
    viewer_->setSketchPointClickedHandler(
        [this](const gp_Pnt2d& point, const double tolerance) {
            if (sketchTool_ == SketchTool::Trim) viewer_->clearSketchTrimPreview();
            if (activeSketchId_.empty() || sketchTool_ != SketchTool::Trim) {
                handleSketchPoint(point);
                return;
            }
            const auto result = modeling_.trimSketchEntity(activeSketchId_, point, tolerance);
            if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
            else refreshModelView(false);
        });
    viewer_->setSketchMouseMovedHandler(
        [this](const gp_Pnt2d& point, const double tolerance) {
            if (activeSketchId_.empty() || sketchTool_ != SketchTool::Trim) return;
            const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
                modeling_.body().findFeature(activeSketchId_));
            if (!sketch) { viewer_->clearSketchTrimPreview(); return; }
            const auto plan = cad::operations::SketchTrimService::previewTrim(
                *sketch, point, tolerance);
            if (plan) viewer_->setSketchTrimPreview(plan->removedEntities);
            else viewer_->clearSketchTrimPreview();
        });
    viewer_->setSketchCancelHandler(
        [this]() {
            sketchFirstPoint_.reset(); sketchSecondPoint_.reset();
            if (sketchTool_ == SketchTool::Trim) selectSketchLineTool();
        });
    connect(&modeling_.undoStack(), &QUndoStack::indexChanged, this, [this]() {
        viewer_->clearSketchTrimPreview();
        refreshModelView();
        featureEditorPanel_->scheduleRefresh();
    });
    connect(&modeling_.undoStack(), &QUndoStack::cleanChanged, this, [this](bool) { updateTitle(); });
    statusBar()->showMessage("Ready");
}


MainWindow::~MainWindow()
{
    disconnect(&modeling_.undoStack(), nullptr, this, nullptr);
    featureEditorPanel_->setService(nullptr);
}

void MainWindow::createParametricPanel()
{
    auto* dockWidget =
        new QDockWidget("Model", this);

    dockWidget->setObjectName("ParametricModelDock");
    dockWidget->setFixedWidth(ModelPanelWidth);
    dockWidget->setSizePolicy(
        QSizePolicy::Fixed,
        QSizePolicy::Expanding
    );

    featureEditorPanel_ =
        new FeatureEditorPanel(dockWidget);
    featureEditorPanel_->setSizePolicy(
        QSizePolicy::Fixed,
        QSizePolicy::Expanding
    );

    featureEditorPanel_->setService(&modeling_);
    featureEditorPanel_->setFeatures(modeling_.features());
    featureEditorPanel_->setActionState(modeling_.actionState(selectedIds()));

    featureEditorPanel_->setModelChangedHandler(
        [this]() {
            refreshModelView();
        }
    );

    featureEditorPanel_->setFeatureSelectedHandler(
        [this](const QStringList& featureIds) {
            applySelection(featureIds);
        }
    );
    featureEditorPanel_->setFeatureDoubleClickedHandler(
        [this](const QString& featureId) { editSketchById(featureId); });

    connect(viewer_, &CadViewer::selectionChanged, this,
        [this](const cad::application::SelectionSnapshot& selection) {
            applySelectionSnapshot(selection);
    });

    dockWidget->setWidget(featureEditorPanel_);

    addDockWidget(
        Qt::LeftDockWidgetArea,
        dockWidget
    );
}

void MainWindow::refreshModelView(const bool fitView)
{
    const auto result = presenter_->refresh();
    QStringList surviving;
    for (const auto& id : selectedObjectIds_) {
        if (std::find_if(result.presentedIds.begin(), result.presentedIds.end(),
                [&id](const auto& candidate) { return QString::fromStdString(candidate) == id; })
            != result.presentedIds.end()) surviving.append(id);
    }
    featureEditorPanel_->setFeatures(modeling_.features());
    applySelection(surviving);
    featureEditorPanel_->setActionState(modeling_.actionState(selectedIds()));
    if (fitView) viewer_->fitAll();
    statusBar()->showMessage(result.rebuilt ? "Model updated" : QString::fromStdString(result.error), 3000);
}

void MainWindow::applySelection(
    const QStringList& featureIds,
    const bool updateViewer
)
{
    // This is the single MainWindow projection point for both viewer -> tree
    // and tree -> viewer selection paths. Keep the viewer update optional to
    // preserve feedback-loop suppression for featureSelectionChanged.
    if (updateViewer) {
        viewer_->selectFeatures(featureIds);
        currentSelection_ = viewer_->selectionSnapshot();
    }
    selectedObjectIds_ = featureIds;
    featureEditorPanel_->selectFeatures(featureIds);
    updateActionState();
}

void MainWindow::applySelectionSnapshot(
    const cad::application::SelectionSnapshot& selection,
    const bool updateViewer
)
{
    currentSelection_ = selection;
    const auto featureIds = selection.featureIds();
    QStringList ids;
    for (const auto& id : featureIds) ids.append(QString::fromStdString(id));

    if (updateViewer) {
        viewer_->selectFeatures(ids);
        currentSelection_ = viewer_->selectionSnapshot();
    }
    selectedObjectIds_ = ids;
    featureEditorPanel_->selectFeatures(ids);
    updateActionState();
}

std::vector<std::string> MainWindow::selectedIds() const
{
    return currentSelection_.selectedObjectIds();
}

void MainWindow::updateActionState()
{
    const auto state = modeling_.actionState(currentSelection_);
    deleteAction_->setEnabled(state.canDelete);
    if (duplicateAction_) duplicateAction_->setEnabled(selectedIds().size() == 1);
    faceAction_->setEnabled(state.canCreateFace);
    extrudeAction_->setEnabled(state.canExtrude);
    if (pocketAction_) pocketAction_->setEnabled(state.canPocket);
    if (editSketchAction_) editSketchAction_->setEnabled(
        state.canEditSketch && activeSketchId_.empty());
    if (sketchOnFaceAction_) sketchOnFaceAction_->setEnabled(state.canSketchOnFace);
    if (linearPatternAction_) linearPatternAction_->setEnabled(selectedIds().size() == 1);
    if (pathPatternAction_) pathPatternAction_->setEnabled(selectedIds().size() == 2);
    if (filletAction_) filletAction_->setEnabled(state.canFillet);
    if (chamferAction_) chamferAction_->setEnabled(state.canChamfer);
}

void MainWindow::reportResult(const cad::application::ModelingResult& result)
{
    if (!result.success) {
        QMessageBox::warning(this, "Modeling operation failed",
            QString::fromStdString(result.error));
        return;
    }
    refreshModelView();
    applySelection({QString::fromStdString(result.id)});
}

void MainWindow::createActions()
{
    auto* fileMenu = menuBar()->addMenu("&File");
    auto* newAction = fileMenu->addAction("&New");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newDocument);
    auto* openAction = fileMenu->addAction("&Open...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openDocument);
    auto* saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this]() { saveDocument(); });
    auto* saveAsAction = fileMenu->addAction("Save &As...");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this]() { saveDocumentAs(); });

    auto* editMenu = menuBar()->addMenu("&Edit");
    connect(editMenu, &QMenu::aboutToShow, this, [this]() { featureEditorPanel_->commitPendingEdits(); });
    auto* undoAction = modeling_.undoStack().createUndoAction(this, "Undo");
    auto undoKeys = QKeySequence::keyBindings(QKeySequence::Undo);
    if (!undoKeys.contains(QKeySequence(Qt::CTRL | Qt::Key_Z))) undoKeys.append(QKeySequence(Qt::CTRL | Qt::Key_Z));
    undoAction->setShortcuts(undoKeys);
    editMenu->addAction(undoAction);
    auto* redoAction = modeling_.undoStack().createRedoAction(this, "Redo");
    auto redoKeys = QKeySequence::keyBindings(QKeySequence::Redo);
    if (!redoKeys.contains(QKeySequence(Qt::CTRL | Qt::Key_Y))) redoKeys.append(QKeySequence(Qt::CTRL | Qt::Key_Y));
    redoAction->setShortcuts(redoKeys);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    deleteAction_ = editMenu->addAction("Delete");
    deleteAction_->setShortcut(QKeySequence(Qt::Key_Delete));
    deleteAction_->setEnabled(false);
    connect(deleteAction_, &QAction::triggered, this, &MainWindow::deleteFeature);
    duplicateAction_ = editMenu->addAction("Duplicate");
    duplicateAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    duplicateAction_->setEnabled(false);
    connect(duplicateAction_, &QAction::triggered, this, [this]() {
        const auto ids = selectedIds();
        if (ids.size() != 1) return;
        reportResult(modeling_.duplicateFeature(
            ids.front()));
    });

    auto* modelingMenu = menuBar()->addMenu("&Modeling");
    auto* viewMenu = menuBar()->addMenu("&View");

    auto* toolBar = addToolBar("Modeling");
    toolBar->addAction(deleteAction_);

    auto* boxAction = new QAction("Box", this);
    connect(boxAction, &QAction::triggered, this, &MainWindow::createBox);
    modelingMenu->addAction(boxAction);
    toolBar->addAction(boxAction);

    auto* cylinderAction = new QAction("Cylinder", this);
    connect(cylinderAction, &QAction::triggered, this, &MainWindow::createCylinder);
    modelingMenu->addAction(cylinderAction);
    toolBar->addAction(cylinderAction);

    modelingMenu->addSeparator();
    auto* sketchAction = modelingMenu->addAction("Add Rectangle Sketch");
    connect(sketchAction, &QAction::triggered, this, &MainWindow::createRectangleSketch);
    sketchOnFaceAction_ = modelingMenu->addAction("Sketch on Face");
    sketchOnFaceAction_->setEnabled(false);
    connect(sketchOnFaceAction_, &QAction::triggered, this, &MainWindow::createSketchOnFace);
    editSketchAction_ = modelingMenu->addAction("Edit Sketch");
    editSketchAction_->setEnabled(false);
    connect(editSketchAction_, &QAction::triggered, this, &MainWindow::editSelectedSketch);
    sketchLineAction_ = modelingMenu->addAction("Sketch Line");
    sketchLineAction_->setEnabled(false);
    connect(sketchLineAction_, &QAction::triggered, this, &MainWindow::selectSketchLineTool);
    sketchCircleAction_ = modelingMenu->addAction("Sketch Circle");
    sketchCircleAction_->setEnabled(false);
    connect(sketchCircleAction_, &QAction::triggered, this, &MainWindow::selectSketchCircleTool);
    sketchArcAction_ = modelingMenu->addAction("Sketch Arc");
    sketchArcAction_->setEnabled(false);
    connect(sketchArcAction_, &QAction::triggered, this, &MainWindow::selectSketchArcTool);
    sketchRectangleAction_ = modelingMenu->addAction("Sketch Rectangle");
    sketchRectangleAction_->setEnabled(false);
    connect(sketchRectangleAction_, &QAction::triggered, this, &MainWindow::selectSketchRectangleTool);
    sketchTrimAction_ = modelingMenu->addAction("Trim");
    sketchTrimAction_->setEnabled(false);
    connect(sketchTrimAction_, &QAction::triggered, this, &MainWindow::selectSketchTrimTool);
    finishSketchAction_ = modelingMenu->addAction("Finish Sketch");
    finishSketchAction_->setEnabled(false);
    connect(finishSketchAction_, &QAction::triggered, this, &MainWindow::finishSketch);
    faceAction_ = modelingMenu->addAction("Create Face");
    faceAction_->setEnabled(false);
    connect(faceAction_, &QAction::triggered, this, &MainWindow::createFace);
    extrudeAction_ = modelingMenu->addAction("Extrude");
    extrudeAction_->setEnabled(false);
    connect(extrudeAction_, &QAction::triggered, this, &MainWindow::createExtrude);
    pocketAction_ = modelingMenu->addAction("Pocket");
    pocketAction_->setEnabled(false);
    connect(pocketAction_, &QAction::triggered, this, &MainWindow::createPocket);
    linearPatternAction_ = modelingMenu->addAction("Linear Pattern");
    linearPatternAction_->setEnabled(false);
    linearPatternAction_->setToolTip("Create a linear pattern from the selected object");
    connect(linearPatternAction_, &QAction::triggered, this, &MainWindow::createLinearPattern);
    toolBar->addAction(linearPatternAction_);
    pathPatternAction_ = modelingMenu->addAction("Path Pattern");
    pathPatternAction_->setEnabled(false);
    pathPatternAction_->setToolTip("Create a pattern along the selected path");
    connect(pathPatternAction_, &QAction::triggered, this, &MainWindow::createPathPattern);
    toolBar->addAction(pathPatternAction_);
    filletAction_ = modelingMenu->addAction("Fillet");
    filletAction_->setEnabled(false);
    filletAction_->setToolTip("Fillet selected edges");
    connect(filletAction_, &QAction::triggered, this, &MainWindow::createFillet);
    toolBar->addAction(filletAction_);
    chamferAction_ = modelingMenu->addAction("Chamfer");
    chamferAction_->setEnabled(false);
    chamferAction_->setToolTip("Chamfer selected edges");
    connect(chamferAction_, &QAction::triggered, this, &MainWindow::createChamfer);
    toolBar->addAction(chamferAction_);
    modelingMenu->addSeparator();

    auto* clearAction = new QAction("Clear", this);
    connect(clearAction, &QAction::triggered, this, &MainWindow::clearDocument);
    modelingMenu->addAction(clearAction);

    auto* fitAllAction = new QAction("Fit All", this);
    connect(fitAllAction, &QAction::triggered, viewer_, &CadViewer::fitAll);
    viewMenu->addAction(fitAllAction);
    toolBar->addAction(fitAllAction);
}

void MainWindow::createBox()
{
    reportResult(modeling_.createBox());
}

void MainWindow::createCylinder()
{
    reportResult(modeling_.createCylinder());
}

void MainWindow::createRectangleSketch()
{
    const auto result = modeling_.createSketch();
    reportResult(result);
    if (result.success) enterSketchEditing(result.id);
}

void MainWindow::createSketchOnFace()
{
    const auto result = modeling_.createSketchOnFace(currentSelection_);
    reportResult(result);
    if (result.success) enterSketchEditing(result.id);
}

void MainWindow::editSelectedSketch()
{
    if (!modeling_.canEditSketch(currentSelection_)) return;
    editSketchById(QString::fromStdString(currentSelection_.items.front().featureId));
}

void MainWindow::editSketchById(const QString& sketchId)
{
    if (sketchId.isEmpty() || !activeSketchId_.empty()) return;
    const cad::application::SelectionSnapshot selection{{
        {sketchId.toStdString(), cad::application::SelectionKind::Object, std::nullopt}}};
    if (!modeling_.canEditSketch(selection)) {
        statusBar()->showMessage("Selected feature is not a Sketch", 3000);
        return;
    }
    applySelection({sketchId});
    enterSketchEditing(sketchId.toStdString());
}

void MainWindow::enterSketchEditing(const std::string& sketchId)
{
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(sketchId));
    if (!sketch) return;
    try {
        const auto frame = sketch->currentFrame();
        activeSketchId_ = sketchId;
        sketchTool_ = SketchTool::Line;
        sketchFirstPoint_.reset();
        sketchSecondPoint_.reset();
        viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Line);
        viewer_->enterSketchMode(frame.origin, frame.xDirection,
            frame.yDirection, frame.normal);
        sketchLineAction_->setEnabled(true);
        sketchCircleAction_->setEnabled(true);
        sketchArcAction_->setEnabled(true);
        sketchRectangleAction_->setEnabled(true);
        sketchTrimAction_->setEnabled(true);
        finishSketchAction_->setEnabled(true);
        updateActionState();
        statusBar()->showMessage("Sketch mode: select a drawing tool");
    } catch (const std::exception& error) {
        QMessageBox::warning(this, "Sketch editing failed", error.what());
    }
}

void MainWindow::finishSketch()
{
    viewer_->exitSketchMode();
    activeSketchId_.clear();
    sketchTool_ = SketchTool::None;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    sketchLineAction_->setEnabled(false);
    sketchCircleAction_->setEnabled(false);
    sketchArcAction_->setEnabled(false);
    sketchRectangleAction_->setEnabled(false);
    sketchTrimAction_->setEnabled(false);
    finishSketchAction_->setEnabled(false);
    updateActionState();
    statusBar()->showMessage("Ready");
    refreshModelView(false);
}

void MainWindow::selectSketchLineTool()
{
    sketchTool_ = SketchTool::Line;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Line);
}

void MainWindow::selectSketchCircleTool()
{
    sketchTool_ = SketchTool::Circle;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Circle);
}

void MainWindow::selectSketchArcTool()
{
    sketchTool_ = SketchTool::Arc;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Arc: select center, start, then end");
}

void MainWindow::selectSketchRectangleTool()
{
    sketchTool_ = SketchTool::Rectangle;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Rectangle);
}

void MainWindow::selectSketchTrimTool()
{
    sketchTool_ = SketchTool::Trim;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Trim);
    statusBar()->showMessage("Trim: click a Line, Arc, or Circle segment");
}

void MainWindow::handleSketchPoint(const gp_Pnt2d& point)
{
    if (activeSketchId_.empty() || sketchTool_ == SketchTool::None) return;
    if (!sketchFirstPoint_) {
        sketchFirstPoint_ = point;
        return;
    }
    if (sketchTool_ == SketchTool::Arc && !sketchSecondPoint_) {
        sketchSecondPoint_ = point;
        return;
    }
    const auto first = *sketchFirstPoint_;
    sketchFirstPoint_.reset();
    const auto second = sketchSecondPoint_;
    sketchSecondPoint_.reset();
    cad::application::ModelingResult result;
    if (sketchTool_ == SketchTool::Line) {
        result = modeling_.addSketchLine(activeSketchId_, first, point);
    } else if (sketchTool_ == SketchTool::Circle) {
        result = modeling_.addSketchCircle(activeSketchId_, first, first.Distance(point));
    } else if (sketchTool_ == SketchTool::Arc) {
        result = modeling_.addSketchArc(activeSketchId_, first, *second, point);
    } else {
        result = modeling_.addSketchLine(activeSketchId_, first,
            {point.X(), first.Y()});
        if (result.success) result = modeling_.addSketchLine(activeSketchId_,
            {point.X(), first.Y()}, point);
        if (result.success) result = modeling_.addSketchLine(activeSketchId_,
            point, {first.X(), point.Y()});
        if (result.success) result = modeling_.addSketchLine(activeSketchId_,
            {first.X(), point.Y()}, first);
    }
    if (!result.success) {
        QMessageBox::warning(this, "Sketch entity failed",
            QString::fromStdString(result.error));
    } else {
        refreshModelView(false);
    }
}

void MainWindow::createFace()
{
    reportResult(modeling_.createFace(selectedIds()));
}

void MainWindow::createExtrude()
{
    const auto ids = selectedIds();
    if (ids.size() == 1) {
        const auto feature = modeling_.body().findFeature(ids.front());
        if (feature && feature->role() == cad::parametric::FeatureRole::Sketch) {
            bool accepted = false;
            const double distance = QInputDialog::getDouble(
                this, "Extrude", "Distance:", 20.0, 0.001, 1'000'000.0,
                3, &accepted);
            if (!accepted) return;
            const auto reverse = QMessageBox::question(
                this, "Extrude", "Reverse direction?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                == QMessageBox::Yes;
            reportResult(modeling_.createExtrudeFromSketch(
                currentSelection_, distance, reverse));
            return;
        }
    }
    reportResult(modeling_.createExtrude(ids));
}

void MainWindow::createPocket()
{
    bool accepted = false;
    const double depth = QInputDialog::getDouble(
        this, "Pocket", "Depth:", 10.0, 0.001, 1'000'000.0,
        3, &accepted);
    if (!accepted) return;
    reportResult(modeling_.createPocketFromSketch(currentSelection_, depth));
}

void MainWindow::createLinearPattern()
{
    reportResult(modeling_.createLinearPattern(selectedIds()));
}

void MainWindow::createPathPattern()
{
    reportResult(modeling_.createPathPattern(selectedIds()));
}

void MainWindow::createFillet()
{
    reportResult(modeling_.createFillet(currentSelection_));
}

void MainWindow::createChamfer()
{
    reportResult(modeling_.createChamfer(currentSelection_));
}

void MainWindow::deleteFeature()
{
    featureEditorPanel_->commitPendingEdits();
    const auto ids = selectedIds();
    if (ids.size() != 1) return;
    const auto id = ids.front();
    const auto names = modeling_.dependentNames(id);
    if (!names.isEmpty() && QMessageBox::question(this, "Delete Feature",
        QString("Delete %1?\nDependent features will also be deleted:\n%2")
            .arg(QString::fromStdString(id), names.join("\n")),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
        != QMessageBox::Yes) return;
    reportResult(modeling_.deleteFeature(id));
}

void MainWindow::clearDocument()
{
    modeling_.clearProject();
    applySelection({});
    featureEditorPanel_->refresh();
}

void MainWindow::updateTitle()
{
    setWindowTitle((currentFile_.isEmpty() ? QStringLiteral("Untitled") : currentFile_)
                   + QStringLiteral("[*] — ParametricCAD"));
    setWindowModified(!modeling_.undoStack().isClean());
}

bool MainWindow::saveTo(const QString& path)
{
    featureEditorPanel_->commitPendingEdits();
    QString error;
    if (!project_.save(path, error)) {
        QMessageBox::critical(this, "Save failed", path + "\n" + error);
        return false;
    }
    currentFile_ = path;
    updateTitle();
    statusBar()->showMessage("Saved: " + path, 3000);
    return true;
}

bool MainWindow::saveDocument()
{
    if (!currentFile_.isEmpty()) return saveTo(currentFile_);
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        QMessageBox::critical(this, "Save failed", "Cannot create application data directory: " + directory);
        return false;
    }
    return saveTo(QDir(directory).filePath("autosave.pcad"));
}

bool MainWindow::saveDocumentAs()
{
    QFileDialog dialog(this, "Save project", currentFile_, "ParametricCAD (*.pcad)");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix("pcad");
    if (dialog.exec() != QDialog::Accepted) return false;
    return saveTo(dialog.selectedFiles().first());
}

void MainWindow::newDocument()
{
    if (!confirmReplacement()) return;
    project_.newProject();
    presenter_->clear();
    applySelection({});
    featureEditorPanel_->refresh();
    currentFile_.clear();
    updateTitle();
}

void MainWindow::openDocument()
{
    const auto path = QFileDialog::getOpenFileName(this, "Open project", currentFile_, "ParametricCAD (*.pcad)");
    if (path.isEmpty()) return;
    if (!confirmReplacement()) return;
    QString error;
    if (!project_.open(path, error)) {
        QMessageBox::critical(this, "Open failed", path + "\n" + error);
        return;
    }
    currentFile_ = path;
    presenter_->clear();
    applySelection({});
    refreshModelView(true);
    updateTitle();
    statusBar()->showMessage("Opened: " + path, 3000);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (saveDocument()) event->accept();
    else event->ignore();
}

bool MainWindow::confirmReplacement()
{
    featureEditorPanel_->commitPendingEdits();
    if (!project_.isDirty()) return true;
    const auto choice = QMessageBox::question(this, "Current project",
        "Save the current project before replacing it?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return choice == QMessageBox::Discard || (choice == QMessageBox::Save && saveDocument());
}
