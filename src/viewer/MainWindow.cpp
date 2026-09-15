#include "viewer/MainWindow.h"
#include "model/ProjectFile.h"

#include "operations/BoxFeature.h"
#include "operations/CylinderFeature.h"
#include "viewer/CadViewer.h"
#include "viewer/FeatureEditorPanel.h"

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

#include <memory>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      viewer_(new CadViewer(this))
{
    updateTitle();
    setCentralWidget(viewer_);

    createActions();
    createParametricPanel();
    statusBar()->showMessage("Ready");
}


void MainWindow::createParametricPanel()
{
    auto* dockWidget =
        new QDockWidget("Model", this);

    dockWidget->setObjectName("ParametricModelDock");

    featureEditorPanel_ =
        new FeatureEditorPanel(dockWidget);

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

    connect(viewer_, &CadViewer::featureSelectionChanged, featureEditorPanel_,
        &FeatureEditorPanel::selectFeatures);

    dockWidget->setWidget(featureEditorPanel_);

    addDockWidget(
        Qt::LeftDockWidgetArea,
        dockWidget
    );
}

void MainWindow::refreshParametricModel()
{
    if (!parametricBody_.recompute()) {
        statusBar()->showMessage(
            QString::fromStdString(
                parametricBody_.lastError()
            ),
            5000
        );
        return;
    }

    restoreViewer();

    statusBar()->showMessage(
        "Parametric model recomputed",
        2000
    );
}


void MainWindow::selectParametricFeatures(const QStringList& featureIds)
{
    viewer_->selectFeatures(featureIds);
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

    auto* modelingMenu = menuBar()->addMenu("&Modeling");
    auto* viewMenu = menuBar()->addMenu("&View");

    auto* toolBar = addToolBar("Modeling");

    auto* boxAction = new QAction("Box", this);
    connect(boxAction, &QAction::triggered, this, &MainWindow::createBox);
    modelingMenu->addAction(boxAction);
    toolBar->addAction(boxAction);

    auto* cylinderAction = new QAction("Cylinder", this);
    connect(cylinderAction, &QAction::triggered, this, &MainWindow::createCylinder);
    modelingMenu->addAction(cylinderAction);
    toolBar->addAction(cylinderAction);

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
    auto feature = std::make_unique<BoxFeature>(100.0, 70.0, 30.0);
    Feature& addedFeature = document_.addFeature(std::move(feature));
    viewer_->display(addedFeature.shape());
    statusBar()->showMessage("Box created", 2000);
}

void MainWindow::createCylinder()
{
    auto feature = std::make_unique<CylinderFeature>(25.0, 60.0);
    Feature& addedFeature = document_.addFeature(std::move(feature));
    viewer_->display(addedFeature.shape());
    statusBar()->showMessage("Cylinder created", 2000);
}

void MainWindow::clearDocument()
{
    document_.clear();
    parametricBody_ = {};
    featureEditorPanel_->setBody(&parametricBody_);
    viewer_->clear();
    statusBar()->showMessage("Document cleared", 2000);
}

void MainWindow::restoreViewer()
{
    viewer_->clear();
    for (const auto& feature : document_.features()) {
        viewer_->display(feature->shape());
    }
    // Create feature presentations only when the model changes or is loaded.
    for (const auto& feature : parametricBody_.features()) {
        if (!feature->shape().IsNull()) {
            viewer_->display(feature->shape(), QString::fromStdString(feature->id()));
        }
    }
    viewer_->selectFeatures(featureEditorPanel_->selectedFeatureIds());
    viewer_->fitAll();
}

void MainWindow::updateTitle()
{
    setWindowTitle((currentFile_.isEmpty() ? QStringLiteral("Untitled") : currentFile_)
                   + QStringLiteral(" — ParametricCAD"));
}

bool MainWindow::saveTo(const QString& path)
{
    QString error;
    if (!ProjectFile::save(path, document_, parametricBody_, error)) {
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
    clearDocument();
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
    if (document_.features().empty() && parametricBody_.features().empty() && currentFile_.isEmpty()) return true;
    const auto choice = QMessageBox::question(this, "Current project",
        "Save the current project before replacing it?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return choice == QMessageBox::Discard || (choice == QMessageBox::Save && saveDocument());
}
