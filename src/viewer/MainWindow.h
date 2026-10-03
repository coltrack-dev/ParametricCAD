#pragma once

#include <QMainWindow>
#include "application/ModelingController.h"
#include "application/ProjectController.h"

#include <memory>
#include <optional>
#include <gp_Pnt2d.hxx>

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
    void applySelectionSnapshot(
        const cad::application::SelectionSnapshot& selection,
        bool updateViewer = false
    );
    void updateActionState();
    void reportResult(const cad::application::ModelingResult& result);
    std::vector<std::string> selectedIds() const;
    void createBox();
    void createCylinder();
    void createRectangleSketch();
    void createSketchOnFace();
    void finishSketch();
    void selectSketchLineTool();
    void selectSketchCircleTool();
    void selectSketchRectangleTool();
    void enterSketchEditing(const std::string& sketchId);
    void handleSketchPoint(const gp_Pnt2d& point);
    void createFace();
    void createExtrude();
    void createPocket();
    void createLinearPattern();
    void createPathPattern();
    void createFillet();
    void createChamfer();
    void deleteFeature();
    void clearDocument();

    cad::application::ModelingController modeling_;
    cad::application::ProjectController project_;
    QString currentFile_;
    QAction* deleteAction_{nullptr};
    QAction* duplicateAction_{nullptr};
    QAction* faceAction_{nullptr};
    QAction* extrudeAction_{nullptr};
    QAction* pocketAction_{nullptr};
    QAction* linearPatternAction_{nullptr};
    QAction* pathPatternAction_{nullptr};
    QAction* filletAction_{nullptr};
    QAction* chamferAction_{nullptr};
    QAction* sketchOnFaceAction_{nullptr};
    QAction* sketchLineAction_{nullptr};
    QAction* sketchCircleAction_{nullptr};
    QAction* sketchRectangleAction_{nullptr};
    QAction* finishSketchAction_{nullptr};
    enum class SketchTool { None, Line, Circle, Rectangle };
    SketchTool sketchTool_{SketchTool::None};
    std::string activeSketchId_;
    std::optional<gp_Pnt2d> sketchFirstPoint_;
    // Compatibility projection of viewer/tree feature-ID selection. OCCT and
    // CadViewer::SelectionState remain the selection source of truth; this
    // mirror supplies MainWindow actions and model operation inputs without
    // coupling MainWindow to viewer internals.
    QStringList selectedObjectIds_;
    cad::application::SelectionSnapshot currentSelection_;
    CadViewer* viewer_{nullptr};
    FeatureEditorPanel* featureEditorPanel_{nullptr};
    std::unique_ptr<cad::viewer::ModelPresenter> presenter_;
};
