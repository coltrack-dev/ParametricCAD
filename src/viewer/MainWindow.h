#pragma once

#include <QMainWindow>
#include "application/ModelingController.h"
#include "application/ProjectController.h"

#include <memory>

class CadViewer;
class FeatureEditorPanel;
namespace cad::viewer { class ModelPresenter; }

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
    void createActions();
    void createParametricPanel();
    void refreshModelView(bool fitView = false);
    void applySelection(const QStringList& featureIds, bool updateViewer = true);
    void updateActionState();
    void reportResult(const cad::application::ModelingResult& result);
    std::vector<std::string> selectedIds() const;
    void createBox();
    void createCylinder();
    void createRectangleSketch();
    void createFace();
    void createExtrude();
    void deleteFeature();
    void clearDocument();

    cad::application::ModelingController modeling_;
    cad::application::ProjectController project_;
    QString currentFile_;
    QAction* deleteAction_{nullptr};
    QAction* faceAction_{nullptr};
    QAction* extrudeAction_{nullptr};
    QStringList selectedObjectIds_;
    CadViewer* viewer_{nullptr};
    FeatureEditorPanel* featureEditorPanel_{nullptr};
    std::unique_ptr<cad::viewer::ModelPresenter> presenter_;
};
