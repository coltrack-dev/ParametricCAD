#include "viewer/MainWindow.h"

#include "operations/BoxFeature.h"
#include "operations/CylinderFeature.h"
#include "viewer/CadViewer.h"
#include "viewer/FeatureEditorPanel.h"

#include <QAction>
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
    setWindowTitle("ParametricCAD 0.1");
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

    viewer_->clear();

    if (!parametricBody_.shape().IsNull()) {
        viewer_->display(
            parametricBody_.shape()
        );
    }

    statusBar()->showMessage(
        "Parametric model recomputed",
        2000
    );
}

void MainWindow::createActions()
{
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
    viewer_->clear();
    statusBar()->showMessage("Document cleared", 2000);
}
