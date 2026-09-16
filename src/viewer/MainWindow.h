#pragma once

#include <QMainWindow>
#include <QUndoStack>

#include <string>
#include "model/Document.h"
#include "model/Body.h"

class CadViewer;
class FeatureEditorPanel;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void closeEvent(QCloseEvent* event) override;
    void newDocument();
    void openDocument();
    bool saveDocument();
    bool confirmReplacement();
    bool saveDocumentAs();
    bool saveTo(const QString& path);
    void updateTitle();
    void restoreViewer(bool fitView = true);
    QString currentFile_;
    void createActions();
    void createParametricPanel();
    void refreshParametricModel();
    void updateParametricVisibility();
    void selectParametricFeatures(const QStringList& featureIds);
    void createBox();
    void createCylinder();
    void createRectangleSketch();
    void createFace();
    void createExtrude();
    void deleteFeature();
    void addParametricFeature(const cad::parametric::ParametricFeature::Ptr& feature);
    void clearDocument();

    Document document_;
    cad::parametric::Body parametricBody_;
    QUndoStack undoStack_;
    bool resettingProject_{false};
    CadViewer* viewer_{nullptr};
    FeatureEditorPanel* featureEditorPanel_{nullptr};
};
