#include "viewer/MainWindow.h"
#include "viewer/CadViewer.h"
#include "viewer/FeatureEditorPanel.h"
#include "viewer/ModelPresenter.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QKeySequence>
#include <QMessageBox>
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
    connect(&modeling_.undoStack(), &QUndoStack::indexChanged, this, [this]() {
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

    connect(viewer_, &CadViewer::featureSelectionChanged, this, [this](const QStringList& ids) {
        applySelection(ids, false);
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
    }
    selectedObjectIds_ = featureIds;
    featureEditorPanel_->selectFeatures(featureIds);
    updateActionState();
}

std::vector<std::string> MainWindow::selectedIds() const
{
    std::vector<std::string> ids;
    for (const auto& id : selectedObjectIds_) ids.push_back(id.toStdString());
    return ids;
}

void MainWindow::updateActionState()
{
    const auto state = modeling_.actionState(selectedIds());
    deleteAction_->setEnabled(state.canDelete);
    if (duplicateAction_) duplicateAction_->setEnabled(selectedObjectIds_.size() == 1);
    faceAction_->setEnabled(state.canCreateFace);
    extrudeAction_->setEnabled(state.canExtrude);
    if (linearPatternAction_) linearPatternAction_->setEnabled(selectedObjectIds_.size() == 1);
    if (pathPatternAction_) pathPatternAction_->setEnabled(selectedObjectIds_.size() == 2);
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
        if (selectedObjectIds_.size() != 1) return;
        reportResult(modeling_.duplicateFeature(
            selectedObjectIds_.front().toStdString()));
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
    faceAction_ = modelingMenu->addAction("Create Face");
    faceAction_->setEnabled(false);
    connect(faceAction_, &QAction::triggered, this, &MainWindow::createFace);
    extrudeAction_ = modelingMenu->addAction("Extrude Face");
    extrudeAction_->setEnabled(false);
    connect(extrudeAction_, &QAction::triggered, this, &MainWindow::createExtrude);
    linearPatternAction_ = modelingMenu->addAction("Linear Pattern");
    linearPatternAction_->setEnabled(false);
    connect(linearPatternAction_, &QAction::triggered, this, &MainWindow::createLinearPattern);
    pathPatternAction_ = modelingMenu->addAction("Path Pattern");
    pathPatternAction_->setEnabled(false);
    connect(pathPatternAction_, &QAction::triggered, this, &MainWindow::createPathPattern);
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
    reportResult(modeling_.createSketch());
}

void MainWindow::createFace()
{
    reportResult(modeling_.createFace(selectedIds()));
}

void MainWindow::createExtrude()
{
    reportResult(modeling_.createExtrude(selectedIds()));
}

void MainWindow::createLinearPattern()
{
    reportResult(modeling_.createLinearPattern(selectedIds()));
}

void MainWindow::createPathPattern()
{
    reportResult(modeling_.createPathPattern(selectedIds()));
}

void MainWindow::deleteFeature()
{
    featureEditorPanel_->commitPendingEdits();
    if (selectedObjectIds_.size() != 1) return;
    const auto id = selectedObjectIds_.front().toStdString();
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
