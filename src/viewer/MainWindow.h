#pragma once

#include <QMainWindow>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QTimer>
#include "application/ModelingController.h"
#include "application/ProjectController.h"
#include "application/VisibilityManager.h"
#include "application/BimNavigationModel.h"
#include "application/InteractiveOperation.h"
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
#include "application/IfcImporter.h"
#endif

#include <atomic>
#include <memory>
#include <optional>
#include <gp_Pnt2d.hxx>

class CadViewer;
class FeatureEditorPanel;
class BimNavigationPanel;
class BimInspectorPanel;
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
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    void importIfc();
    void updateIfcImportProgress();
    void finishIfcImport();
    void abortIfcImport(const QString& error = {});
#endif
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
    void createBimPanel();
    void updateBimInspector(const QStringList& featureIds);
    void refreshBimNavigation();
    void refreshModelView(bool fitView = false);
    void refreshVisibilityView();
    void activateSpatialVisibility(
        cad::application::VisibilityMode outsideMode =
            cad::application::VisibilityMode::Hidden);
    void clearSpatialVisibility();
    void saveCurrentView();
    void restoreSavedView();
    void renameSavedView();
    void deleteSavedView();
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
    void toggleSelectedSketchConstruction();
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
    void createShell();
    void createRevolve();
    void beginRevolveAxisPick(const QString& featureId, const QString& axisType,
                              bool editingExisting = false);
    void cancelRevolveAxisPick();
    bool handleRevolveAxisSelection(
        const cad::application::SelectionSnapshot& selection);
    void handleSketchLineAxisPicked(const cad::parametric::SketchEntityId& entityId);
    bool beginOperation(
        cad::application::InteractiveOperationKind kind,
        const std::vector<std::string>& sourceFeatureIds);
    void cancelOperation();
    bool commitOperation();
    void deleteFeature();
    void clearDocument();
    void commitVisibilityGroups(
        std::vector<cad::application::VisibilityGroup> before,
        std::vector<cad::application::VisibilityGroup> after,
        const QString& text);
    void commitVisibilityFilters(
        cad::application::VisibilityFilterState before,
        cad::application::VisibilityFilterState after,
        const QString& text);
    void commitVisibilityPresets(
        std::vector<cad::application::VisibilityPreset> before,
        std::vector<cad::application::VisibilityPreset> after,
        const QString& text);
    void commitVisibilityConfiguration(
        cad::application::VisibilityConfiguration before,
        cad::application::VisibilityConfiguration after,
        const QString& text);

    cad::application::ModelingController modeling_;
    cad::application::InteractiveOperationSession operationSession_;
    cad::application::VisibilityManager visibilityManager_;
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
    QAction* shellAction_{nullptr};
    QAction* revolveAction_{nullptr};
    bool revolveAxisPicking_{false};
    bool revolveRestoringSelectionMode_{false};
    bool revolveAxisPickEditing_{false};
    int revolvePreviousSelectionMode_{0};
    QString revolveAxisFeatureId_;
    QString revolveProfileFeatureId_;
    double revolvePendingAngleDegrees_{360.0};
    cad::parametric::RevolveAxisType revolvePendingAxisType_{
        cad::parametric::RevolveAxisType::GlobalY};
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    QAction* importIfcAction_{nullptr};
#endif
    QAction* sketchOnFaceAction_{nullptr};
    QAction* editSketchAction_{nullptr};
    QAction* sketchLineAction_{nullptr};
    QAction* sketchConstructionAction_{nullptr};
    QAction* toggleSketchConstructionAction_{nullptr};
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
    BimNavigationPanel* bimNavigationPanel_{nullptr};
    BimInspectorPanel* bimInspectorPanel_{nullptr};
    cad::application::BimNavigationModel bimNavigationModel_;
    std::unique_ptr<cad::viewer::ModelPresenter> presenter_;
    QFutureWatcher<std::shared_ptr<cad::application::ProjectLoadResult>> projectLoadWatcher_;
    QTimer projectLoadProgressTimer_;
    QTimer projectLoadHeartbeatTimer_;
    QProgressDialog* projectLoadDialog_{nullptr};
    std::shared_ptr<std::atomic<int>> projectLoadLoaded_;
    std::shared_ptr<std::atomic<int>> projectLoadTotal_;
    QString pendingProjectPath_;
    bool projectLoading_{false};
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    QFutureWatcher<std::shared_ptr<cad::import::IfcImportResult>> ifcImportWatcher_;
    QTimer ifcImportProgressTimer_;
    QProgressDialog* ifcImportDialog_{nullptr};
    std::shared_ptr<std::atomic<int>> ifcImportProcessed_;
    std::shared_ptr<std::atomic<int>> ifcImportTotal_;
    std::shared_ptr<std::atomic<int>> ifcImportStage_;
    std::shared_ptr<std::atomic_bool> ifcImportCancelRequested_;
    QString pendingIfcPath_;
    bool ifcImporting_{false};
#endif
    int lastUndoStackIndex_{0};
    bool historyRefreshScheduled_{false};
    bool historyVisibilityRefreshPending_{false};
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
