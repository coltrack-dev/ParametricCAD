#include "viewer/MainWindow.h"
#include "model/ProjectFile.h"
#include "model/FeatureVisibility.h"
#include "commands/FeatureCommands.h"

#include "operations/BoxFeature.h"
#include "operations/CylinderFeature.h"
#include "operations/ParametricFeatures.h"
#include "viewer/CadViewer.h"
#include "viewer/FeatureEditorPanel.h"

#include <QAction>
#include <QScopedValueRollback>
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
#include <QUuid>

#include <memory>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      viewer_(new CadViewer(this))
{
    updateTitle();
    setCentralWidget(viewer_);

    createActions();
    createParametricPanel();
    connect(&undoStack_, &QUndoStack::indexChanged, this, [this]() {
        if (resettingProject_) return;
        refreshParametricModel();
        featureEditorPanel_->scheduleRefresh();
    });
    connect(&undoStack_, &QUndoStack::cleanChanged, this, [this](bool) { updateTitle(); });
    statusBar()->showMessage("Ready");
}


MainWindow::~MainWindow()
{
    disconnect(&undoStack_, nullptr, this, nullptr);
    featureEditorPanel_->setUndoStack(nullptr);
    featureEditorPanel_->setBody(nullptr);
}

void MainWindow::createParametricPanel()
{
    auto* dockWidget =
        new QDockWidget("Model", this);

    dockWidget->setObjectName("ParametricModelDock");

    featureEditorPanel_ =
        new FeatureEditorPanel(dockWidget);

    featureEditorPanel_->setUndoStack(&undoStack_);
    featureEditorPanel_->setBody(
        &parametricBody_
    );

    featureEditorPanel_->setModelChangedHandler(
        [this]() {
            refreshParametricModel();
        }
    );

    featureEditorPanel_->setFeatureSelectedHandler(
        [this](const QStringList& featureIds) {
            selectParametricFeatures(featureIds);
        }
    );

    connect(viewer_, &CadViewer::featureSelectionChanged, this, [this](const QStringList& ids) {
        featureEditorPanel_->selectFeatures(ids);
        selectedObjectIds_ = ids;
        deleteAction_->setEnabled(ids.size() == 1);
    });

    dockWidget->setWidget(featureEditorPanel_);

    addDockWidget(
        Qt::LeftDockWidgetArea,
        dockWidget
    );
}

void MainWindow::refreshParametricModel()
{
    const bool rebuilt = parametricBody_.recompute();
    QStringList present;
    std::size_t index = 0;
    for (const auto& feature : document_.features()) {
        const auto id = QString("legacy:%1").arg(index++);
        present.append(id);
        viewer_->updateFeature(feature->shape(), id);
    }
    for (const auto& feature : parametricBody_.features()) {
        if (feature->state() != cad::parametric::FeatureState::UpToDate || feature->shape().IsNull()) continue;
        const auto id = QString::fromStdString(feature->id());
        present.append(id);
        viewer_->updateFeature(feature->shape(), id);
    }
    viewer_->retainFeatures(present);
    updateParametricVisibility();
    QStringList surviving;
    for (const auto& id : selectedObjectIds_) {
        if (present.contains(id) || parametricBody_.findFeature(id.toStdString())) surviving.append(id);
    }
    if (surviving != selectedObjectIds_) featureEditorPanel_->selectFeatures(surviving);
    selectParametricFeatures(surviving);
    statusBar()->showMessage(rebuilt ? "Model updated" : QString::fromStdString(parametricBody_.lastError()), 3000);
}

void MainWindow::updateParametricVisibility()
{
    QStringList hidden;
    for (const auto& id : cad::parametric::hiddenFeatureIds(parametricBody_)) {
        hidden.append(QString::fromStdString(id));
    }
    viewer_->setHiddenFeatures(hidden);
}

void MainWindow::selectParametricFeatures(const QStringList& featureIds)
{
    viewer_->selectFeatures(featureIds);
    selectedObjectIds_ = featureIds;
    deleteAction_->setEnabled(featureIds.size() == 1);
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
    auto* undoAction = undoStack_.createUndoAction(this, "Undo");
    auto undoKeys = QKeySequence::keyBindings(QKeySequence::Undo);
    if (!undoKeys.contains(QKeySequence(Qt::CTRL | Qt::Key_Z))) undoKeys.append(QKeySequence(Qt::CTRL | Qt::Key_Z));
    undoAction->setShortcuts(undoKeys);
    editMenu->addAction(undoAction);
    auto* redoAction = undoStack_.createRedoAction(this, "Redo");
    auto redoKeys = QKeySequence::keyBindings(QKeySequence::Redo);
    if (!redoKeys.contains(QKeySequence(Qt::CTRL | Qt::Key_Y))) redoKeys.append(QKeySequence(Qt::CTRL | Qt::Key_Y));
    redoAction->setShortcuts(redoKeys);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    deleteAction_ = editMenu->addAction("Delete");
    deleteAction_->setShortcut(QKeySequence(Qt::Key_Delete));
    deleteAction_->setEnabled(false);
    connect(deleteAction_, &QAction::triggered, this, &MainWindow::deleteFeature);

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
    auto* faceAction = modelingMenu->addAction("Create Face");
    connect(faceAction, &QAction::triggered, this, &MainWindow::createFace);
    auto* extrudeAction = modelingMenu->addAction("Extrude Face");
    connect(extrudeAction, &QAction::triggered, this, &MainWindow::createExtrude);
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
    undoStack_.push(new cad::commands::AddDocumentFeatureCommand(document_,
        std::make_unique<BoxFeature>(100.0, 70.0, 30.0), "Create Box"));
}

void MainWindow::createCylinder()
{
    undoStack_.push(new cad::commands::AddDocumentFeatureCommand(document_,
        std::make_unique<CylinderFeature>(25.0, 60.0), "Create Cylinder"));
}

void MainWindow::createRectangleSketch()
{
    const auto id = "sketch-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    addParametricFeature(std::make_shared<cad::parametric::SketchFeature>(id, 100.0, 60.0));
}

void MainWindow::createFace()
{
    const auto ids = featureEditorPanel_->selectedFeatureIds();
    const auto sketch = ids.size() == 1
        ? std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
              parametricBody_.findFeature(ids.front().toStdString()))
        : nullptr;
    if (!sketch) {
        QMessageBox::information(this, "Create Face",
            "Select exactly one Rectangle Sketch in the model tree or viewport, then choose Create Face.");
        return;
    }
    const auto id = "face-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    addParametricFeature(std::make_shared<cad::parametric::FaceFeature>(id, sketch));
}

void MainWindow::createExtrude()
{
    const auto ids = featureEditorPanel_->selectedFeatureIds();
    const auto face = ids.size() == 1
        ? std::dynamic_pointer_cast<cad::parametric::FaceFeature>(parametricBody_.findFeature(ids.front().toStdString()))
        : nullptr;
    if (!face) {
        QMessageBox::information(this, "Extrude Face", "Select exactly one Face feature to extrude.");
        return;
    }
    const auto id = "extrude-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    addParametricFeature(std::make_shared<cad::parametric::ExtrudeFeature>(id, face, gp_Vec(0, 0, 20)));
}

void MainWindow::deleteFeature()
{
    featureEditorPanel_->commitPendingEdits();
    if (selectedObjectIds_.size() != 1) return;
    const auto id = selectedObjectIds_.front();
    try {
        std::unique_ptr<QUndoCommand> command;
        if (parametricBody_.findFeature(id.toStdString())) {
            auto removal = std::make_unique<cad::commands::RemoveFeatureCommand>(parametricBody_, id.toStdString());
            const auto names = removal->dependentNames();
            if (!names.isEmpty() && QMessageBox::question(this, "Delete Feature",
                QString("Delete %1?\nDependent features will also be deleted:\n%2")
                    .arg(id, names.join("\n")), QMessageBox::Yes | QMessageBox::Cancel,
                    QMessageBox::Cancel) != QMessageBox::Yes) return;
            command = std::move(removal);
        } else {
            bool valid = false;
            const auto position = id.mid(7).toULongLong(&valid);
            if (!id.startsWith("legacy:") || !valid) throw std::invalid_argument("Cannot delete unknown feature");
            command = std::make_unique<cad::commands::RemoveDocumentFeatureCommand>(document_, position);
        }
        featureEditorPanel_->selectFeatures({});
        selectParametricFeatures({});
        undoStack_.push(command.release());
        featureEditorPanel_->refresh();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, "Delete Feature", QString::fromUtf8(error.what()));
    }
}

void MainWindow::addParametricFeature(const cad::parametric::ParametricFeature::Ptr& feature)
{
    try {
        undoStack_.push(new cad::commands::AddFeatureCommand(parametricBody_, feature,
            "Create " + QString::fromStdString(feature->name())));
        const auto id = QString::fromStdString(feature->id());
        featureEditorPanel_->refresh();
        featureEditorPanel_->selectFeatures({id});
        selectParametricFeatures({id});
    } catch (const std::exception& error) {
        QMessageBox::warning(this, "Cannot create feature", QString::fromUtf8(error.what()));
    }
}

void MainWindow::clearDocument()
{
    if (document_.features().empty() && parametricBody_.features().empty()) return;
    undoStack_.push(new cad::commands::ClearProjectCommand(document_, parametricBody_));
}

void MainWindow::restoreViewer(bool fitView)
{
    selectParametricFeatures({});
    viewer_->clear();
    // Create feature presentations only when the model changes or is loaded.
    refreshParametricModel();
    if (fitView) viewer_->fitAll();
}

void MainWindow::updateTitle()
{
    setWindowTitle((currentFile_.isEmpty() ? QStringLiteral("Untitled") : currentFile_)
                   + QStringLiteral("[*] — ParametricCAD"));
    setWindowModified(!undoStack_.isClean());
}

bool MainWindow::saveTo(const QString& path)
{
    featureEditorPanel_->commitPendingEdits();
    if (!parametricBody_.recompute()) {
        QMessageBox::critical(this, "Save failed", QString::fromStdString(parametricBody_.lastError()));
        return false;
    }
    QString error;
    if (!ProjectFile::save(path, document_, parametricBody_, error)) {
        QMessageBox::critical(this, "Save failed", path + "\n" + error);
        return false;
    }
    currentFile_ = path;
    undoStack_.setClean();
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
    const QScopedValueRollback resetting(resettingProject_, true);
    undoStack_.clear();
    document_.clear();
    parametricBody_ = {};
    featureEditorPanel_->setBody(&parametricBody_);
    refreshParametricModel();
    currentFile_.clear();
    updateTitle();
}

void MainWindow::openDocument()
{
    const auto path = QFileDialog::getOpenFileName(this, "Open project", currentFile_, "ParametricCAD (*.pcad)");
    if (path.isEmpty()) return;
    // Validate first; a failed open must leave the active project intact.
    Document loaded;
    cad::parametric::Body loadedBody;
    QString error;
    if (!ProjectFile::load(path, loaded, loadedBody, error)) {
        QMessageBox::critical(this, "Open failed", path + "\n" + error);
        return;
    }
    if (!confirmReplacement()) return;
    // Saving the active project may have updated the file being opened.
    if (path == currentFile_ && !ProjectFile::load(path, loaded, loadedBody, error)) {
        QMessageBox::critical(this, "Open failed", path + "\n" + error);
        return;
    }
    const QScopedValueRollback resetting(resettingProject_, true);
    undoStack_.clear();
    document_ = std::move(loaded);
    parametricBody_ = std::move(loadedBody);
    currentFile_ = path;
    featureEditorPanel_->setBody(&parametricBody_);
    restoreViewer();
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
    if (undoStack_.isClean()) return true;
    const auto choice = QMessageBox::question(this, "Current project",
        "Save the current project before replacing it?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return choice == QMessageBox::Discard || (choice == QMessageBox::Save && saveDocument());
}
