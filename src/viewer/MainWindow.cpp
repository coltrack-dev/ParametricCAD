#include "viewer/MainWindow.h"
#include "viewer/CadViewer.h"
#include "viewer/FeatureEditorPanel.h"
#include "viewer/ModelPresenter.h"
#include "operations/SketchTrimService.h"
#include "operations/SketchExtendService.h"
#include "operations/SketchConstraintSolver.h"

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

std::string pointRoleText(const cad::parametric::SketchPointRole role)
{
    switch (role) {
    case cad::parametric::SketchPointRole::LineStart: return "Start";
    case cad::parametric::SketchPointRole::LineEnd: return "End";
    case cad::parametric::SketchPointRole::ArcStart: return "Start";
    case cad::parametric::SketchPointRole::ArcEnd: return "End";
    case cad::parametric::SketchPointRole::CircleCenter: return "Center";
    case cad::parametric::SketchPointRole::ArcCenter: return "Center";
    }
    return "Point";
}
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
    featureEditorPanel_->setSketchConstraintSelectionHandler(
        [this](const QString& id) { selectSketchConstraint(id); });
    featureEditorPanel_->setSketchConstraintEditHandler(
        [this](const QString& id) { editSketchConstraint(id); });
    featureEditorPanel_->setSketchConstraintDeleteHandler(
        [this](const QString& id) { deleteSketchConstraint(id); });
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
            if (sketchTool_ == SketchTool::Trim || sketchTool_ == SketchTool::Extend)
                viewer_->clearSketchTrimPreview();
            if (activeSketchId_.empty()
                || (sketchTool_ != SketchTool::Trim && sketchTool_ != SketchTool::Extend)) {
                handleSketchPoint(point, tolerance);
                return;
            }
            const auto result = sketchTool_ == SketchTool::Trim
                ? modeling_.trimSketchEntity(activeSketchId_, point, tolerance)
                : modeling_.extendSketchEntity(activeSketchId_, point, tolerance);
            if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
            else refreshModelView(false);
        });
    viewer_->setSketchMouseMovedHandler(
        [this](const gp_Pnt2d& point, const double tolerance) {
            if (activeSketchId_.empty()
                || (sketchTool_ != SketchTool::Trim && sketchTool_ != SketchTool::Extend)) return;
            const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
                modeling_.body().findFeature(activeSketchId_));
            if (!sketch) { viewer_->clearSketchTrimPreview(); return; }
            if (sketchTool_ == SketchTool::Trim) {
                const auto plan = cad::operations::SketchTrimService::previewTrim(
                    *sketch, point, tolerance);
                if (plan) viewer_->setSketchTrimPreview(plan->removedEntities);
                else viewer_->clearSketchTrimPreview();
            } else {
                const auto plan = cad::operations::SketchExtendService::previewExtend(
                    *sketch, point, tolerance);
                if (plan) viewer_->setSketchExtendPreview(plan->extensionSpan);
                else viewer_->clearSketchTrimPreview();
            }
        });
    viewer_->setSketchCancelHandler(
        [this]() {
            sketchFirstPoint_.reset(); sketchSecondPoint_.reset();
            if (sketchTool_ == SketchTool::Trim || sketchTool_ == SketchTool::Extend
                || sketchTool_ == SketchTool::Coincident
                || sketchTool_ == SketchTool::Horizontal
                || sketchTool_ == SketchTool::Vertical
                || sketchTool_ == SketchTool::Distance
                || sketchTool_ == SketchTool::Radius
                || sketchTool_ == SketchTool::HorizontalDistance
                || sketchTool_ == SketchTool::VerticalDistance
                || sketchTool_ == SketchTool::Angle
                || sketchTool_ == SketchTool::Parallel
                || sketchTool_ == SketchTool::Perpendicular
                || sketchTool_ == SketchTool::AngleBetweenLines
                || sketchTool_ == SketchTool::Tangent || sketchTool_ == SketchTool::Equal) selectSketchLineTool();
            constraintFirstPoint_.reset();
        });
    viewer_->setSketchConstraintMarkerClickedHandler(
        [this](const std::string& id) { selectSketchConstraint(QString::fromStdString(id)); });
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
    if (!activeSketchId_.empty()) {
        const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            modeling_.body().findFeature(activeSketchId_));
        if (sketch) viewer_->setSketchConstraintMarkers(*sketch, selectedConstraintId_.toStdString());
    }
    refreshConstraintManager();
    statusBar()->showMessage(result.rebuilt ? "Model updated" : QString::fromStdString(result.error), 3000);
}

void MainWindow::refreshConstraintManager()
{
    if (activeSketchId_.empty()) {
        selectedConstraintId_.clear();
        featureEditorPanel_->setSketchConstraints({}, false);
        viewer_->clearSketchConstraintHighlight();
        return;
    }
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    if (!sketch) return;
    std::vector<SketchConstraintListItem> items;
    const auto solved = cad::operations::SketchConstraintSolver::solve(
        sketch->entities(), sketch->constraints());
    int dof = 0;
    for (const auto& entity : sketch->entities()) dof += std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, cad::parametric::SketchLine>) return 4;
        if constexpr (std::is_same_v<T, cad::parametric::SketchCircle>) return 3;
        return 5;
    }, entity);
    dof -= static_cast<int>(sketch->constraints().size());
    dof = std::max(0, dof);
    const QString sketchStatus = solved.status == cad::operations::SolveStatus::Solved
        ? (dof == 0 ? QString("Fully constrained") : QString("Under-constrained"))
        : QString("Conflicting");
    statusBar()->showMessage(QString("Constraints: %1 | Status: %2 | DOF: %3%4")
        .arg(sketch->constraintCount()).arg(sketchStatus).arg(dof)
        .arg(solved.status == cad::operations::SolveStatus::Solved ? QString() : QString(" — %1").arg(QString::fromStdString(solved.error))));
    const QString status = solved.status == cad::operations::SolveStatus::Solved
        ? QString() : QString(" [conflict: %1]").arg(QString::fromStdString(solved.error));
    for (const auto& constraint : sketch->constraints()) {
        SketchConstraintListItem item;
        item.id = std::visit([](const auto& value) { return value.id; }, constraint);
        if (const auto* value = std::get_if<cad::parametric::HorizontalConstraint>(&constraint)) {
            item.label = QString("H  %1").arg(QString::fromStdString(value->entityId));
        } else if (const auto* value = std::get_if<cad::parametric::VerticalConstraint>(&constraint)) {
            item.label = QString("V  %1").arg(QString::fromStdString(value->entityId));
        } else if (const auto* value = std::get_if<cad::parametric::CoincidentConstraint>(&constraint)) {
            item.label = QString("Coincident  %1.%2 ↔ %3.%4")
                .arg(QString::fromStdString(value->a.entityId),
                     QString::fromStdString(pointRoleText(value->a.role)),
                     QString::fromStdString(value->b.entityId),
                     QString::fromStdString(pointRoleText(value->b.role)));
        } else if (const auto* value = std::get_if<cad::parametric::DistanceConstraint>(&constraint)) {
            item.label = QString("Distance  %1 = %2")
                .arg(QString::fromStdString(value->entityId)).arg(value->value, 0, 'f', 2);
            item.editable = true;
        } else if (const auto* value = std::get_if<cad::parametric::RadiusConstraint>(&constraint)) {
            item.label = QString("Radius  %1 = %2")
                .arg(QString::fromStdString(value->entityId)).arg(value->value, 0, 'f', 2);
            item.editable = true;
        } else if (const auto* value = std::get_if<cad::parametric::HorizontalDistanceConstraint>(&constraint)) {
            item.label = QString("X Distance  %1 → %2 = %3")
                .arg(QString::fromStdString(value->first.entityId),
                     QString::fromStdString(value->second.entityId)).arg(value->value, 0, 'f', 2);
            item.editable = true;
        } else if (const auto* value = std::get_if<cad::parametric::VerticalDistanceConstraint>(&constraint)) {
            item.label = QString("Y Distance  %1 → %2 = %3")
                .arg(QString::fromStdString(value->first.entityId),
                     QString::fromStdString(value->second.entityId)).arg(value->value, 0, 'f', 2);
            item.editable = true;
        } else if (const auto* value = std::get_if<cad::parametric::AngleConstraint>(&constraint)) {
            item.label = QString("Angle  %1 = %2°")
                .arg(QString::fromStdString(value->entityId))
                .arg(value->radians * 180.0 / 3.14159265358979323846, 0, 'f', 2);
            item.editable = true;
        } else if (const auto* value = std::get_if<cad::parametric::ParallelConstraint>(&constraint)) {
            item.label = QString("Parallel  %1 ∥ %2")
                .arg(QString::fromStdString(value->firstLineId), QString::fromStdString(value->secondLineId));
        } else if (const auto* value = std::get_if<cad::parametric::PerpendicularConstraint>(&constraint)) {
            item.label = QString("Perpendicular  %1 ⟂ %2")
                .arg(QString::fromStdString(value->firstLineId), QString::fromStdString(value->secondLineId));
        } else if (const auto* value = std::get_if<cad::parametric::AngleBetweenLinesConstraint>(&constraint)) {
            item.label = QString("Angle  %1 → %2 = %3°")
                .arg(QString::fromStdString(value->referenceLineId),
                     QString::fromStdString(value->dependentLineId))
                .arg(value->angleRadians * 180.0 / 3.14159265358979323846, 0, 'f', 2);
            item.editable = true;
        } else if (const auto* value = std::get_if<cad::parametric::TangentConstraint>(&constraint)) {
            item.label = QString("Tangent  %1 ↔ %2").arg(QString::fromStdString(value->firstEntityId), QString::fromStdString(value->secondEntityId));
        } else if (const auto* value = std::get_if<cad::parametric::EqualConstraint>(&constraint)) {
            item.label = QString("Equal  %1 = %2").arg(QString::fromStdString(value->referenceEntityId), QString::fromStdString(value->dependentEntityId));
        }
        item.label += status;
        items.push_back(std::move(item));
    }
    if (!selectedConstraintId_.isEmpty() && std::none_of(items.begin(), items.end(),
        [this](const auto& item) { return QString::fromStdString(item.id) == selectedConstraintId_; }))
        selectedConstraintId_.clear();
    featureEditorPanel_->setSketchConstraints(std::move(items), !activeSketchId_.empty());
    if (!selectedConstraintId_.isEmpty()) {
        featureEditorPanel_->setSketchConstraintSelected(selectedConstraintId_);
        viewer_->setSketchConstraintHighlight(*sketch, selectedConstraintId_.toStdString());
    } else {
        viewer_->clearSketchConstraintHighlight();
    }
}

void MainWindow::selectSketchConstraint(const QString& constraintId)
{
    selectedConstraintId_ = constraintId;
    featureEditorPanel_->setSketchConstraintSelected(constraintId);
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    if (sketch) {
        viewer_->setSketchConstraintMarkers(*sketch, constraintId.toStdString());
        viewer_->setSketchConstraintHighlight(*sketch, constraintId.toStdString());
    }
}

void MainWindow::editSketchConstraint(const QString& constraintId)
{
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    if (!sketch) return;
    for (const auto& constraint : sketch->constraints()) {
        if (std::visit([&](const auto& value) { return value.id == constraintId.toStdString(); }, constraint)) {
            const auto* distance = std::get_if<cad::parametric::DistanceConstraint>(&constraint);
            const auto* radius = std::get_if<cad::parametric::RadiusConstraint>(&constraint);
            const auto* horizontalDistance = std::get_if<cad::parametric::HorizontalDistanceConstraint>(&constraint);
            const auto* verticalDistance = std::get_if<cad::parametric::VerticalDistanceConstraint>(&constraint);
            const auto* angle = std::get_if<cad::parametric::AngleConstraint>(&constraint);
            const auto* angleBetween = std::get_if<cad::parametric::AngleBetweenLinesConstraint>(&constraint);
            if (!distance && !radius && !horizontalDistance && !verticalDistance && !angle && !angleBetween) return;
            bool ok = false;
            const double current = distance ? distance->value : radius ? radius->value
                : horizontalDistance ? horizontalDistance->value : verticalDistance ? verticalDistance->value
                : (angle ? angle->radians : angleBetween->angleRadians) * 180.0 / 3.14159265358979323846;
            const double value = QInputDialog::getDouble(this,
                distance || horizontalDistance || verticalDistance ? "Distance constraint"
                    : angle || angleBetween ? "Angle constraint" : "Radius constraint",
                "Value:", current,
                angle || angleBetween ? -360.0 : horizontalDistance || verticalDistance ? -1.0e6 : 0.001,
                1.0e6, 3, &ok);
            if (!ok) return;
            cad::application::ModelingResult result;
            if (distance) result = modeling_.updateSketchDistance(activeSketchId_, constraintId.toStdString(), value);
            else if (radius) result = modeling_.updateSketchRadius(activeSketchId_, constraintId.toStdString(), value);
            else if (horizontalDistance) result = modeling_.updateSketchHorizontalDistance(activeSketchId_, constraintId.toStdString(), value);
            else if (verticalDistance) result = modeling_.updateSketchVerticalDistance(activeSketchId_, constraintId.toStdString(), value);
            else if (angle) result = modeling_.updateSketchAngle(activeSketchId_, constraintId.toStdString(), value * 3.14159265358979323846 / 180.0);
            else result = modeling_.updateSketchAngleBetweenLines(activeSketchId_, constraintId.toStdString(), value * 3.14159265358979323846 / 180.0);
            if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
            else { selectedConstraintId_ = constraintId; refreshModelView(false); }
            return;
        }
    }
}

void MainWindow::deleteSketchConstraint(const QString& constraintId)
{
    if (activeSketchId_.empty()) return;
    const auto result = modeling_.removeSketchConstraint(
        activeSketchId_, constraintId.toStdString());
    if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
    else {
        selectedConstraintId_.clear();
        refreshModelView(false);
    }
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
    sketchExtendAction_ = modelingMenu->addAction("Extend");
    sketchExtendAction_->setEnabled(false);
    connect(sketchExtendAction_, &QAction::triggered, this, &MainWindow::selectSketchExtendTool);
    sketchCoincidentAction_ = modelingMenu->addAction("Coincident");
    sketchCoincidentAction_->setEnabled(false);
    connect(sketchCoincidentAction_, &QAction::triggered, this, &MainWindow::selectSketchCoincidentTool);
    sketchHorizontalAction_ = modelingMenu->addAction("Horizontal");
    sketchHorizontalAction_->setEnabled(false);
    connect(sketchHorizontalAction_, &QAction::triggered, this, &MainWindow::selectSketchHorizontalTool);
    sketchVerticalAction_ = modelingMenu->addAction("Vertical");
    sketchVerticalAction_->setEnabled(false);
    connect(sketchVerticalAction_, &QAction::triggered, this, &MainWindow::selectSketchVerticalTool);
    sketchDistanceAction_ = modelingMenu->addAction("Distance");
    sketchDistanceAction_->setEnabled(false);
    connect(sketchDistanceAction_, &QAction::triggered, this, &MainWindow::selectSketchDistanceTool);
    sketchRadiusAction_ = modelingMenu->addAction("Radius");
    sketchRadiusAction_->setEnabled(false);
    connect(sketchRadiusAction_, &QAction::triggered, this, &MainWindow::selectSketchRadiusTool);
    sketchHorizontalDistanceAction_ = modelingMenu->addAction("Horizontal Distance");
    sketchHorizontalDistanceAction_->setEnabled(false);
    connect(sketchHorizontalDistanceAction_, &QAction::triggered, this, &MainWindow::selectSketchHorizontalDistanceTool);
    sketchVerticalDistanceAction_ = modelingMenu->addAction("Vertical Distance");
    sketchVerticalDistanceAction_->setEnabled(false);
    connect(sketchVerticalDistanceAction_, &QAction::triggered, this, &MainWindow::selectSketchVerticalDistanceTool);
    sketchAngleAction_ = modelingMenu->addAction("Angle");
    sketchAngleAction_->setEnabled(false);
    connect(sketchAngleAction_, &QAction::triggered, this, &MainWindow::selectSketchAngleTool);
    sketchParallelAction_ = modelingMenu->addAction("Parallel");
    sketchParallelAction_->setEnabled(false);
    connect(sketchParallelAction_, &QAction::triggered, this, &MainWindow::selectSketchParallelTool);
    sketchPerpendicularAction_ = modelingMenu->addAction("Perpendicular");
    sketchPerpendicularAction_->setEnabled(false);
    connect(sketchPerpendicularAction_, &QAction::triggered, this, &MainWindow::selectSketchPerpendicularTool);
    sketchAngleBetweenLinesAction_ = modelingMenu->addAction("Angle Between Lines");
    sketchAngleBetweenLinesAction_->setEnabled(false);
    connect(sketchAngleBetweenLinesAction_, &QAction::triggered, this, &MainWindow::selectSketchAngleBetweenLinesTool);
    sketchTangentAction_ = modelingMenu->addAction("Tangent");
    sketchTangentAction_->setEnabled(false);
    connect(sketchTangentAction_, &QAction::triggered, this, &MainWindow::selectSketchTangentTool);
    sketchEqualAction_ = modelingMenu->addAction("Equal");
    sketchEqualAction_->setEnabled(false);
    connect(sketchEqualAction_, &QAction::triggered, this, &MainWindow::selectSketchEqualTool);
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
        constraintFirstPoint_.reset();
        selectedConstraintId_.clear();
        viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Line);
        viewer_->enterSketchMode(frame.origin, frame.xDirection,
            frame.yDirection, frame.normal);
        viewer_->setSketchConstraintMarkers(*sketch);
        sketchLineAction_->setEnabled(true);
        sketchCircleAction_->setEnabled(true);
        sketchArcAction_->setEnabled(true);
        sketchRectangleAction_->setEnabled(true);
        sketchTrimAction_->setEnabled(true);
        sketchExtendAction_->setEnabled(true);
        sketchCoincidentAction_->setEnabled(true);
        sketchHorizontalAction_->setEnabled(true);
        sketchVerticalAction_->setEnabled(true);
        sketchDistanceAction_->setEnabled(true);
        sketchRadiusAction_->setEnabled(true);
        sketchHorizontalDistanceAction_->setEnabled(true);
        sketchVerticalDistanceAction_->setEnabled(true);
        sketchAngleAction_->setEnabled(true);
        sketchParallelAction_->setEnabled(true);
        sketchPerpendicularAction_->setEnabled(true);
        sketchAngleBetweenLinesAction_->setEnabled(true);
        sketchTangentAction_->setEnabled(true);
        sketchEqualAction_->setEnabled(true);
        finishSketchAction_->setEnabled(true);
        refreshConstraintManager();
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
    constraintFirstPoint_.reset();
    selectedConstraintId_.clear();
    sketchLineAction_->setEnabled(false);
    sketchCircleAction_->setEnabled(false);
    sketchArcAction_->setEnabled(false);
    sketchRectangleAction_->setEnabled(false);
    sketchTrimAction_->setEnabled(false);
    sketchExtendAction_->setEnabled(false);
    sketchCoincidentAction_->setEnabled(false);
    sketchHorizontalAction_->setEnabled(false);
    sketchVerticalAction_->setEnabled(false);
    sketchDistanceAction_->setEnabled(false);
    sketchRadiusAction_->setEnabled(false);
    sketchHorizontalDistanceAction_->setEnabled(false);
    sketchVerticalDistanceAction_->setEnabled(false);
    sketchAngleAction_->setEnabled(false);
    sketchParallelAction_->setEnabled(false);
    sketchPerpendicularAction_->setEnabled(false);
    sketchAngleBetweenLinesAction_->setEnabled(false);
    sketchTangentAction_->setEnabled(false);
    sketchEqualAction_->setEnabled(false);
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
    refreshConstraintManager();
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

void MainWindow::selectSketchExtendTool()
{
    sketchTool_ = SketchTool::Extend;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Extend);
    statusBar()->showMessage("Extend: hover and click a Line or Arc endpoint");
}

void MainWindow::selectSketchCoincidentTool()
{
    sketchTool_ = SketchTool::Coincident;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Coincident: click two Line or Arc endpoints");
}

void MainWindow::selectSketchHorizontalTool()
{
    sketchTool_ = SketchTool::Horizontal;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Horizontal: click a Line");
}

void MainWindow::selectSketchVerticalTool()
{
    sketchTool_ = SketchTool::Vertical;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Vertical: click a Line");
}

void MainWindow::selectSketchDistanceTool()
{
    sketchTool_ = SketchTool::Distance;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Distance: click a Line");
}

void MainWindow::selectSketchRadiusTool()
{
    sketchTool_ = SketchTool::Radius;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Radius: click a Circle or Arc");
}

void MainWindow::selectSketchHorizontalDistanceTool()
{
    sketchTool_ = SketchTool::HorizontalDistance;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Horizontal Distance: click two points");
}

void MainWindow::selectSketchVerticalDistanceTool()
{
    sketchTool_ = SketchTool::VerticalDistance;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Vertical Distance: click two points");
}

void MainWindow::selectSketchAngleTool()
{
    sketchTool_ = SketchTool::Angle;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Angle: click a Line");
}

void MainWindow::selectSketchParallelTool()
{
    sketchTool_ = SketchTool::Parallel;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Parallel: click two Lines");
}

void MainWindow::selectSketchPerpendicularTool()
{
    sketchTool_ = SketchTool::Perpendicular;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Perpendicular: click two Lines");
}

void MainWindow::selectSketchAngleBetweenLinesTool()
{
    sketchTool_ = SketchTool::Tangent;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Angle Between Lines: click reference and dependent Lines");
}

void MainWindow::selectSketchTangentTool()
{
    sketchTool_ = SketchTool::Equal;
    constraintFirstPoint_.reset();
    statusBar()->showMessage("Tangent: click Line, then Circle or Arc");
}

void MainWindow::selectSketchEqualTool()
{
    sketchTool_ = SketchTool::AngleBetweenLines;
    constraintFirstPoint_.reset();
    statusBar()->showMessage("Equal: click two compatible entities");
}

void MainWindow::handleSketchPoint(const gp_Pnt2d& point, const double hitTolerance)
{
    if (activeSketchId_.empty() || sketchTool_ == SketchTool::None) return;
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    if (!sketch) return;
    if (sketchTool_ == SketchTool::Horizontal || sketchTool_ == SketchTool::Vertical) {
        const auto lineId = cad::operations::SketchConstraintSolver::lineAt(
            sketch->entities(), point, hitTolerance);
        if (!lineId) { statusBar()->showMessage("Select a Line", 2000); return; }
        const auto result = sketchTool_ == SketchTool::Horizontal
            ? modeling_.addSketchHorizontal(activeSketchId_, *lineId)
            : modeling_.addSketchVertical(activeSketchId_, *lineId);
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::Coincident) {
        const auto pointRef = cad::operations::SketchConstraintSolver::pointAt(
            sketch->entities(), point, hitTolerance);
        if (!pointRef) { statusBar()->showMessage("Select a sketch endpoint", 2000); return; }
        if (!constraintFirstPoint_) {
            constraintFirstPoint_ = pointRef;
            statusBar()->showMessage("Coincident: select second endpoint");
            return;
        }
        const auto result = modeling_.addSketchCoincident(
            activeSketchId_, *constraintFirstPoint_, *pointRef);
        constraintFirstPoint_.reset();
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::HorizontalDistance
        || sketchTool_ == SketchTool::VerticalDistance) {
        const auto pointRef = cad::operations::SketchConstraintSolver::pointAt(
            sketch->entities(), point, hitTolerance);
        if (!pointRef) { statusBar()->showMessage("Select a sketch point", 2000); return; }
        if (!constraintFirstPoint_) {
            constraintFirstPoint_ = pointRef;
            statusBar()->showMessage("Select second point");
            return;
        }
        bool ok = false;
        const double value = QInputDialog::getDouble(this,
            sketchTool_ == SketchTool::HorizontalDistance ? "Horizontal distance" : "Vertical distance",
            "Value:", 10.0, -1.0e6, 1.0e6, 3, &ok);
        if (!ok) return;
        const auto result = sketchTool_ == SketchTool::HorizontalDistance
            ? modeling_.addSketchHorizontalDistance(activeSketchId_, *constraintFirstPoint_, *pointRef, value)
            : modeling_.addSketchVerticalDistance(activeSketchId_, *constraintFirstPoint_, *pointRef, value);
        constraintFirstPoint_.reset();
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::Angle) {
        const auto lineId = cad::operations::SketchConstraintSolver::lineAt(
            sketch->entities(), point, hitTolerance);
        if (!lineId) { statusBar()->showMessage("Select a Line", 2000); return; }
        bool ok = false;
        const double degrees = QInputDialog::getDouble(this, "Angle constraint",
            "Degrees:", 45.0, -360.0, 360.0, 3, &ok);
        if (!ok) return;
        const auto result = modeling_.addSketchAngle(activeSketchId_, *lineId,
            degrees * 3.14159265358979323846 / 180.0);
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::Parallel || sketchTool_ == SketchTool::Perpendicular) {
        const auto lineId = cad::operations::SketchConstraintSolver::lineAt(
            sketch->entities(), point, hitTolerance);
        if (!lineId) { statusBar()->showMessage("Select a Line", 2000); return; }
        if (!constraintFirstPoint_) {
            constraintFirstPoint_ = cad::parametric::SketchPointRef{
                *lineId, cad::parametric::SketchPointRole::LineStart};
            statusBar()->showMessage("Select second Line");
            return;
        }
        const auto result = sketchTool_ == SketchTool::Parallel
            ? modeling_.addSketchParallel(activeSketchId_, constraintFirstPoint_->entityId, *lineId)
            : modeling_.addSketchPerpendicular(activeSketchId_, constraintFirstPoint_->entityId, *lineId);
        constraintFirstPoint_.reset();
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::AngleBetweenLines) {
        const auto lineId = cad::operations::SketchConstraintSolver::lineAt(
            sketch->entities(), point, hitTolerance);
        if (!lineId) { statusBar()->showMessage("Select a Line", 2000); return; }
        if (!constraintFirstPoint_) {
            constraintFirstPoint_ = cad::parametric::SketchPointRef{
                *lineId, cad::parametric::SketchPointRole::LineStart};
            statusBar()->showMessage("Angle Between Lines: select dependent Line");
            return;
        }
        bool ok = false;
        const double degrees = QInputDialog::getDouble(this, "Angle Between Lines",
            "Degrees:", 30.0, -360.0, 360.0, 3, &ok);
        if (!ok) return;
        const auto result = modeling_.addSketchAngleBetweenLines(activeSketchId_,
            constraintFirstPoint_->entityId, *lineId,
            degrees * 3.14159265358979323846 / 180.0);
        constraintFirstPoint_.reset();
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::Tangent || sketchTool_ == SketchTool::Equal) {
        std::optional<std::string> target;
        if (sketchTool_ == SketchTool::Tangent && !constraintFirstPoint_)
            target = cad::operations::SketchConstraintSolver::lineAt(sketch->entities(), point, hitTolerance);
        else if (sketchTool_ == SketchTool::Tangent)
            target = cad::operations::SketchConstraintSolver::circleOrArcAt(sketch->entities(), point, hitTolerance);
        else
            target = cad::operations::SketchConstraintSolver::lineAt(sketch->entities(), point, hitTolerance);
        if (!target) { statusBar()->showMessage("Select a compatible sketch entity", 2000); return; }
        if (!constraintFirstPoint_) {
            constraintFirstPoint_ = cad::parametric::SketchPointRef{*target, cad::parametric::SketchPointRole::LineStart};
            statusBar()->showMessage("Select second compatible entity");
            return;
        }
        const auto result = sketchTool_ == SketchTool::Tangent
            ? modeling_.addSketchTangent(activeSketchId_, constraintFirstPoint_->entityId, *target)
            : modeling_.addSketchEqual(activeSketchId_, constraintFirstPoint_->entityId, *target);
        constraintFirstPoint_.reset();
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::Distance || sketchTool_ == SketchTool::Radius) {
        const auto target = sketchTool_ == SketchTool::Distance
            ? cad::operations::SketchConstraintSolver::lineAt(sketch->entities(), point, hitTolerance)
            : cad::operations::SketchConstraintSolver::circleOrArcAt(sketch->entities(), point, hitTolerance);
        if (!target) {
            statusBar()->showMessage(sketchTool_ == SketchTool::Distance
                ? "Select a Line" : "Select a Circle or Arc", 2000);
            return;
        }
        bool ok = false;
        const double value = QInputDialog::getDouble(this,
            sketchTool_ == SketchTool::Distance ? "Distance constraint" : "Radius constraint",
            "Value:", 10.0, 0.001, 1.0e6, 3, &ok);
        if (!ok) return;
        std::optional<std::string> existingConstraint;
        for (const auto& constraint : sketch->constraints()) {
            if (sketchTool_ == SketchTool::Distance) {
                if (const auto* item = std::get_if<cad::parametric::DistanceConstraint>(&constraint);
                    item && item->entityId == *target) existingConstraint = item->id;
            } else if (const auto* item = std::get_if<cad::parametric::RadiusConstraint>(&constraint);
                       item && item->entityId == *target) existingConstraint = item->id;
        }
        const auto result = existingConstraint
            ? (sketchTool_ == SketchTool::Distance
                ? modeling_.updateSketchDistance(activeSketchId_, *existingConstraint, value)
                : modeling_.updateSketchRadius(activeSketchId_, *existingConstraint, value))
            : (sketchTool_ == SketchTool::Distance
                ? modeling_.addSketchDistance(activeSketchId_, *target, value)
                : modeling_.addSketchRadius(activeSketchId_, *target, value));
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
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
