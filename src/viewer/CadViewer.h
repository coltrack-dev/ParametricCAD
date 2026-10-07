#pragma once

#include <QWidget>
#include <QPointF>
#include <QStringList>
#include <array>
#include <map>
#include <functional>

#include <vector>
#include <memory>
#include <optional>
#include <cstdint>
#include <fstream>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_TextLabel.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <Bnd_Box.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Pnt.hxx>

#include "viewer/PushPullDrag.h"
#include "viewer/SnapManager.h"
#include "viewer/TransformGizmo.h"
#include "viewer/TransformMath.h"
#include "viewer/SelectionAdapter.h"
#include "application/VisibilityMode.h"
#include "application/InteractiveOperation.h"
#include "application/VisibilityManager.h"
#include "operations/ParametricFeatures.h"
#include "model/TopologicalReference.h"

namespace cad::parametric {
class Body;
}

class QAction;
class QLabel;
class QToolBar;
class QKeyEvent;
class QFocusEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

class CadViewer final : public QWidget
{
    Q_OBJECT

public:
    using SelectionMode = cad::viewer::SelectionMode;

    enum class InteractionMode
    {
        None,
        PendingEmptyPan,
        Pan
    };

    enum class OrbitMode
    {
        Turntable,
        Free
    };

    enum class DisplayMode
    {
        Shaded,
        ShadedWithEdges,
        Wireframe
    };

    enum class SectionAxis
    {
        X,
        Y,
        Z
    };

    struct SectionState
    {
        bool active{false};
        SectionAxis axis{SectionAxis::Z};
        bool flipped{false};
        gp_Pnt origin;
    };

    struct CameraState
    {
        bool valid{false};
        gp_Pnt eye;
        gp_Pnt center;
        gp_Dir up{0, 0, 1};
        double scale{1.0};
    };

    enum class StandardView
    {
        Right,
        Front,
        Top
    };

    explicit CadViewer(
        QWidget* parent = nullptr,
        cad::application::InteractiveOperationSession* operationSession = nullptr);

    void display(const TopoDS_Shape& shape, const QString& featureId = {}, bool fitView = true);
    void beginBulkUpdate();
    void endBulkUpdate();
    std::int64_t lastBulkViewerUpdateMilliseconds() const noexcept;
    std::int64_t lastViewerUpdateMilliseconds() const noexcept;
    std::int64_t lastSelectionActivationMilliseconds() const noexcept;
    void updateFeature(const TopoDS_Shape& shape, const QString& featureId);
    void selectFeatures(const QStringList& featureIds);
    void setHiddenFeatures(const QStringList& featureIds);
    void setFeatureVisibilityModes(
        const std::map<QString, cad::application::VisibilityMode>& modes);
    void applyVisibilityChanges(
        const std::vector<cad::application::VisibilityChange>& changes);
    void setSpatialBox(const gp_Pnt& min, const gp_Pnt& max);
    void clearSpatialBox();
    void setSpatialBoxChangedHandler(
        std::function<void(const gp_Pnt&, const gp_Pnt&)> handler);
    void activateSection(SectionAxis axis);
    void flipSection();
    void clearSection();
    const SectionState& sectionState() const noexcept;
    bool restoreSection(SectionAxis axis, const gp_Pnt& origin, bool flipped);
    CameraState cameraState() const;
    void restoreCamera(const CameraState& state);
    cad::application::VisibilityMode featureVisibilityMode(const QString& featureId) const noexcept;
    void retainFeatures(const QStringList& featureIds);
    void clear();
    bool hasDisplayedShapes() const;
    void fitAll();

    void setSelectionMode(SelectionMode mode);
    SelectionMode selectionMode() const;
    void setOrbitMode(OrbitMode mode);
    OrbitMode orbitMode() const noexcept;
    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const noexcept;
    cad::application::SelectionSnapshot selectionSnapshot() const;
    std::vector<cad::topology::TopologicalReference> captureTopologySelection(
        const cad::parametric::Body& body) const;
    void restoreSelection(
        const cad::parametric::Body& body,
        const std::vector<cad::topology::TopologicalReference>& topology,
        const std::vector<std::string>& objectFeatureIds);
    void clearSelection();
    void enterSketchMode(const gp_Pnt& origin, const gp_Dir& xDirection,
                         const gp_Dir& yDirection, const gp_Dir& normal,
                         const QString& featureId);
    void exitSketchMode();
    bool sketchMode() const noexcept;
    enum class SketchPreviewTool { None, Line, Circle, Rectangle, Trim, Extend };
    void setSketchPreviewTool(SketchPreviewTool tool);
    void setSketchPointClickedHandler(std::function<void(const gp_Pnt2d&, double)> handler);
    void setSketchMouseMovedHandler(std::function<void(const gp_Pnt2d&, double)> handler);
    void setSketchCancelHandler(std::function<void()> handler);
    void setSketchConstraintMarkerClickedHandler(std::function<void(const std::string&)> handler);
    void setSketchConstraintMarkerHoveredHandler(std::function<void(const std::string&)> handler);
    std::optional<gp_Pnt2d> sketchPointAtScreen(const QPoint& position) const;
    double sketchLocalToleranceFromPixels(const QPoint& position, double pixels) const;
    void setSketchTrimPreview(const std::vector<cad::parametric::SketchEntity>& entities);
    void setSketchExtendPreview(const std::vector<cad::parametric::SketchEntity>& entities);
    void clearSketchTrimPreview();
    void setSketchConstraintMarkers(const cad::parametric::SketchFeature& sketch,
                                    const std::string& selectedConstraintId = {});
    void clearSketchConstraintMarkers();
    void setSketchConstraintHighlight(const cad::parametric::SketchFeature& sketch,
                                      const std::string& constraintId);
    void clearSketchConstraintHighlight();
    void setPushPullCommittedHandler(
        std::function<void(const QString&, const cad::topology::TopologicalReference&,
                           const gp_Vec&, double)> handler
    );
    void cancelActiveOperation();
    void setTransformCommittedHandler(
        std::function<void(const QString&, const gp_Trsf&)> handler
    );
    void setTransformCopyCommittedHandler(
        std::function<void(const QString&, const gp_Trsf&)> handler
    );
    void setSectionInteractionStatusHandler(
        std::function<void(const QString&)> handler);
    void setAxisPickCancelHandler(std::function<void()> handler);

signals:
    void selectionChanged(const cad::application::SelectionSnapshot& selection);
    void featureSelectionChanged(const QStringList& featureIds);

protected:
    QPaintEngine* paintEngine() const override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    friend class MainWindowTests;
    void traceSelectionLifecycle(const QString& message) const;
    void initializeOcc();
    void notifyFeatureSelection();
    void setupToolBar();
    void syncToolBarState();
    void setPushPullArmed(bool armed);
    void setTransformMode(bool enabled);
    void updateTransformGizmo();
    void beginTransform(
        cad::viewer::TransformHandle handle,
        const QPoint& position,
        bool copyMode = false
    );
    void updateTransformPreview(const QPoint& position);
    void commitTransform();
    void cancelTransform();
    void updateTransformSnap(gp_Trsf& delta, gp_Pnt& pivot, const gp_Trsf& rawDelta);
    std::optional<gp_Pnt> worldAnchorAtScreenPoint(const QPoint& position) const;
    void zoomAtCursor(const QPoint& position, double factor);
    void invalidateSnapReferenceCache(const char* reason);
    void invalidateSnapProjectionCache(const char* reason);
    bool makeViewRay(const QPoint& position, cad::viewer::ViewRay& ray) const;
    void bindWindow();
    void updateHover(const QPoint& position);
    void syncSelectionStateFromOcct();
    void selectAt(
        const QPoint& position,
        bool toggleSelection,
        bool cycleDetected = false
    );
    void resetDetectedCycle();
    void setXRayEnabled(bool enabled);
    void applyDisplayMode(const Handle(AIS_Shape)& object,
                          const TopoDS_Shape& shape) const;
    void applySelectionMode();
    void releaseManagedSelection(const QString& featureId);
    void updateManagedShape(const QString& featureId,
                            const Handle(AIS_Shape)& object,
                            const TopoDS_Shape& shape);
    bool featureIsSelectable(const QString& featureId) const noexcept;
    void applyFeaturePresentation(
        const QString& featureId, const Handle(AIS_Shape)& object);
    void setFeatureTransparency(
        const QString& featureId, const Handle(AIS_Shape)& object);
    void recordPerformanceSample(const char* operation, std::int64_t milliseconds);

    bool beginPushPull();
    void updatePushPullPreview(const QPoint& position);
    TopoDS_Shape buildPushPullResult(double distance) const;
    void commitPushPull();
    void cancelPushPull();
    void stopMousePan();
    std::optional<std::array<QPointF, 3>> currentTrihedronAxisPositions() const;
    std::optional<int> axisIndicatorHitTest(const QPoint& position) const;
    void updateAxisHover(const QPoint& position);
    void clearAxisHover();
    void setStandardView(StandardView view);
    void updateOrbitStateFromCamera();
    void applyTurntableCamera();
    void orbitTurntable(const QPoint& currentPosition);
    int spatialBoxHandleAt(const QPoint& position) const;
    void updateSpatialBoxDrag(const QPoint& position);
    void updateSectionPresentation();
    int sectionHandleAt(const QPoint& position) const;
    void updateSectionDrag(const QPoint& position);

    QToolBar* toolBar_{nullptr};
    QLabel* xRayStatusLabel_{nullptr};
    QAction* selectObjectAction_{nullptr};
    QAction* selectEdgeAction_{nullptr};
    QAction* selectFaceAction_{nullptr};
    QAction* selectVertexAction_{nullptr};
    QAction* pushPullAction_{nullptr};
    QAction* transformAction_{nullptr};
    QAction* xRayAction_{nullptr};
    QAction* turntableOrbitAction_{nullptr};
    QAction* freeOrbitAction_{nullptr};

    Handle(V3d_Viewer) viewer_;
    Handle(V3d_View) view_;
    Handle(AIS_InteractiveContext) context_;

    QPoint lastMousePosition_;
    QPoint mousePressPosition_;
    InteractionMode interactionMode_{InteractionMode::None};
    OrbitMode orbitMode_{OrbitMode::Turntable};
    DisplayMode displayMode_{DisplayMode::ShadedWithEdges};
    double orbitAzimuth_{0.0};
    double orbitElevation_{0.0};
    bool initialized_{false};
    SelectionMode selectionMode_{SelectionMode::Object};
    bool selectionActivationDirty_{true};
    mutable std::uint64_t selectionTraceSequence_{0};
    mutable std::ofstream selectionTraceFile_;

    bool xRayEnabled_{false};
    bool detectedCycleActive_{false};
    QPoint detectedCyclePosition_;
    std::vector<Handle(AIS_Shape)> displayedShapes_;
    std::map<QString, Handle(AIS_Shape)> featureObjects_;
    std::map<QString, Standard_Integer> managedSelectionModes_;
    std::map<QString, cad::application::VisibilityMode> featureVisibility_;
    Handle(AIS_Shape) spatialBoxObject_;
    Handle(AIS_Shape) sectionPlaneObject_;
    Handle(AIS_Shape) sectionHandleObject_;
    Handle(AIS_Shape) sectionNormalObject_;
    Handle(Graphic3d_ClipPlane) sectionClipPlane_;
    SectionState sectionState_;
    Bnd_Box sectionBounds_;
    bool sectionDragging_{false};
    bool sectionHandleHovered_{false};
    QPoint sectionDragStart_;
    double sectionDragPosition_{0.0};
    std::function<void(const QString&)> sectionInteractionStatusHandler_;
    std::function<void()> axisPickCancelHandler_;
    int bulkUpdateDepth_{0};
    bool bulkCachesInvalidated_{false};
    std::int64_t lastBulkViewerUpdateMilliseconds_{0};
    std::int64_t lastViewerUpdateMilliseconds_{0};
    std::int64_t lastSelectionActivationMilliseconds_{0};
    struct PerformanceMetric
    {
        std::size_t count{0};
        std::int64_t totalMilliseconds{0};
        std::int64_t maximumMilliseconds{0};
    };
    std::map<QString, PerformanceMetric> performanceMetrics_;
    bool performanceDiagnostics_{false};
    bool snapDisabled_{false};
    bool selectionDisabled_{false};

    bool pushPullArmed_{false};
    bool pushPullActive_{false};
    cad::application::InteractiveOperationSession localOperationSession_;
    cad::application::InteractiveOperationSession* operationSession_{nullptr};
    double pushPullDistance_{0.0};
    TopoDS_Face pushPullFace_;
    TopoDS_Shape pushPullBaseShape_;
    cad::viewer::PushPullDragState pushPullDragState_;
    gp_Vec pushPullNormal_;
    QString pushPullFeatureId_;
    int pushPullFaceIndex_{0};
    std::optional<cad::topology::TopologicalReference> pushPullFaceReference_;
    Handle(AIS_Shape) pushPullObject_;
    Handle(AIS_Shape) pushPullPreview_;
    std::function<void(const QString&, const cad::topology::TopologicalReference&,
                       const gp_Vec&, double)> pushPullCommittedHandler_;
    std::function<void(const QString&, const gp_Trsf&)> transformCommittedHandler_;
    std::function<void(const QString&, const gp_Trsf&)> transformCopyCommittedHandler_;
    std::function<void(const gp_Pnt&, const gp_Pnt&)> spatialBoxChangedHandler_;
    gp_Pnt spatialBoxMin_;
    gp_Pnt spatialBoxMax_;
    int spatialBoxHandle_{-1};
    QPoint spatialBoxDragStart_;
    gp_Pnt spatialBoxDragMin_;
    gp_Pnt spatialBoxDragMax_;
    bool sketchMode_{false};
    QString editingSketchFeatureId_;
    SelectionMode sketchPreviousSelectionMode_{SelectionMode::Object};
    gp_Pnt sketchOrigin_;
    gp_Dir sketchXDirection_;
    gp_Dir sketchYDirection_;
    gp_Dir sketchNormal_;
    SketchPreviewTool sketchPreviewTool_{SketchPreviewTool::None};
    std::optional<gp_Pnt> sketchPreviewFirstPoint_;
    Handle(AIS_Shape) sketchPreviewObject_;
    std::function<void(const gp_Pnt2d&, double)> sketchPointClickedHandler_;
    std::function<void(const gp_Pnt2d&, double)> sketchMouseMovedHandler_;
    std::function<void()> sketchCancelHandler_;
    Handle(AIS_Shape) sketchTrimPreviewObject_;
    struct SketchConstraintMarker
    {
        cad::parametric::SketchConstraintId constraintId;
        Handle(AIS_TextLabel) presentation;
    };
    std::vector<SketchConstraintMarker> sketchConstraintMarkers_;
    Handle(AIS_Shape) sketchConstraintHighlightObject_;
    std::function<void(const std::string&)> sketchConstraintMarkerClickedHandler_;
    std::function<void(const std::string&)> sketchConstraintMarkerHoveredHandler_;

    std::unique_ptr<cad::viewer::TransformGizmo> transformGizmo_;
    cad::viewer::SnapManager snapManager_;
    bool transformMode_{false};
    bool transformDragging_{false};
    bool transformCopyMode_{false};
    cad::viewer::TransformHandle transformHandle_{cad::viewer::TransformHandle::None};
    QString transformFeatureId_;
    Handle(AIS_Shape) transformObject_;
    Handle(AIS_Shape) transformPreviewObject_;
    TopoDS_Shape transformOriginalShape_;
    gp_Pnt transformPivot_;
    gp_Trsf transformDelta_;
    cad::viewer::ViewRay transformStartRay_;
    cad::viewer::PushPullDragState transformTranslationDrag_;
    std::optional<gp_Pnt> transformRotationStartPoint_;
    std::optional<cad::viewer::SnapCandidate> activeSnap_;
    std::vector<cad::viewer::SnapReference> transformSourceReferences_;
    std::vector<cad::viewer::SnapReference> transformTargetReferences_;
    std::shared_ptr<std::vector<cad::viewer::SnapCandidate>> transformSnapCandidates_;
    std::map<QString, std::vector<cad::viewer::SnapReference>> snapReferenceCache_;
    std::map<QString, std::shared_ptr<std::vector<cad::viewer::SnapCandidate>>> snapCandidateCache_;
    std::map<QString, QPointF> snapScreenProjectionCache_;
    std::shared_ptr<cad::viewer::SnapScreenIndex> snapScreenIndex_;

    std::unique_ptr<cad::viewer::OcctSelectionAdapter> selectionAdapter_;
    cad::viewer::SelectionState selectionState_;
    bool axisHoverActive_{false};

    Handle(Aspect_DisplayConnection) displayConnection_;
};
