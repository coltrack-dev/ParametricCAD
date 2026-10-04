#pragma once

#include <QMainWindow>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QTimer>
#include "application/ModelingController.h"
#include "application/ProjectController.h"

#include <atomic>
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
    void startProjectLoad(const QString& path);
    void updateProjectLoadProgress();
    void finishProjectLoad();
    void processProjectLoadRecomputeChunk();
    void processProjectLoadPresentationChunk();
    void sampleProjectLoadEventLoop();
    void abortProjectLoad(const QString& error);
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
    void editSelectedSketch();
    void editSketchById(const QString& sketchId);
    void finishSketch();
    void selectSketchLineTool();
    void selectSketchCircleTool();
    void selectSketchArcTool();
    void selectSketchRectangleTool();
    void selectSketchTrimTool();
    void selectSketchExtendTool();
    void selectSketchCoincidentTool();
    void selectSketchHorizontalTool();
    void selectSketchVerticalTool();
    void selectSketchDistanceTool();
    void selectSketchRadiusTool();
    void selectSketchHorizontalDistanceTool();
    void selectSketchVerticalDistanceTool();
    void selectSketchAngleTool();
    void selectSketchParallelTool();
    void selectSketchPerpendicularTool();
    void selectSketchAngleBetweenLinesTool();
    void selectSketchTangentTool();
    void selectSketchEqualTool();
    void enterSketchEditing(const std::string& sketchId);
    void refreshConstraintManager();
    void selectSketchConstraint(const QString& constraintId);
    void editSketchConstraint(const QString& constraintId);
    void deleteSketchConstraint(const QString& constraintId);
    void handleSketchPoint(const gp_Pnt2d& point, double hitTolerance);
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
    QAction* editSketchAction_{nullptr};
    QAction* sketchLineAction_{nullptr};
    QAction* sketchCircleAction_{nullptr};
    QAction* sketchArcAction_{nullptr};
    QAction* sketchRectangleAction_{nullptr};
    QAction* sketchTrimAction_{nullptr};
    QAction* sketchExtendAction_{nullptr};
    QAction* sketchCoincidentAction_{nullptr};
    QAction* sketchHorizontalAction_{nullptr};
    QAction* sketchVerticalAction_{nullptr};
    QAction* sketchDistanceAction_{nullptr};
    QAction* sketchRadiusAction_{nullptr};
    QAction* sketchHorizontalDistanceAction_{nullptr};
    QAction* sketchVerticalDistanceAction_{nullptr};
    QAction* sketchAngleAction_{nullptr};
    QAction* sketchParallelAction_{nullptr};
    QAction* sketchPerpendicularAction_{nullptr};
    QAction* sketchAngleBetweenLinesAction_{nullptr};
    QAction* sketchTangentAction_{nullptr};
    QAction* sketchEqualAction_{nullptr};
    QAction* finishSketchAction_{nullptr};
    enum class SketchTool { None, Line, Circle, Arc, Rectangle, Trim, Extend,
                            Coincident, Horizontal, Vertical, Distance, Radius,
                            HorizontalDistance, VerticalDistance, Angle, Parallel, Perpendicular,
                            AngleBetweenLines, Tangent, Equal };
    SketchTool sketchTool_{SketchTool::None};
    std::string activeSketchId_;
    std::optional<gp_Pnt2d> sketchFirstPoint_;
    std::optional<gp_Pnt2d> sketchSecondPoint_;
    std::optional<cad::parametric::SketchPointRef> constraintFirstPoint_;
    QString selectedConstraintId_;
    // Compatibility projection of viewer/tree feature-ID selection. OCCT and
    // CadViewer::SelectionState remain the selection source of truth; this
    // mirror supplies MainWindow actions and model operation inputs without
    // coupling MainWindow to viewer internals.
    QStringList selectedObjectIds_;
    cad::application::SelectionSnapshot currentSelection_;
    CadViewer* viewer_{nullptr};
    FeatureEditorPanel* featureEditorPanel_{nullptr};
    std::unique_ptr<cad::viewer::ModelPresenter> presenter_;
    QFutureWatcher<std::shared_ptr<cad::application::ProjectLoadResult>> projectLoadWatcher_;
    QTimer projectLoadProgressTimer_;
    QTimer projectLoadHeartbeatTimer_;
    QProgressDialog* projectLoadDialog_{nullptr};
    std::shared_ptr<std::atomic<int>> projectLoadLoaded_;
    std::shared_ptr<std::atomic<int>> projectLoadTotal_;
    QString pendingProjectPath_;
    bool projectLoading_{false};
    std::shared_ptr<cad::application::ProjectLoadResult> projectLoadResult_;
    ProjectLoadMetrics projectLoadMetrics_;
    std::size_t projectLoadRecomputeIndex_{0};
    std::size_t projectLoadPresentationIndex_{0};
    QElapsedTimer projectLoadRecomputeTimer_;
    QElapsedTimer projectLoadPresentationTimer_;
    QElapsedTimer projectLoadEventLoopClock_;
    QElapsedTimer projectLoadTotalTimer_;
    std::int64_t projectLoadMaxGuiStallMilliseconds_{0};
    std::int64_t projectLoadYieldCount_{0};
};
