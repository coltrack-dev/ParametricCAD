#pragma once

#include <QWidget>
#include <QStringList>
#include <map>
#include <functional>

#include <vector>
#include <memory>
#include <optional>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Vec.hxx>

#include "viewer/PushPullDrag.h"
#include "viewer/SnapManager.h"
#include "viewer/TransformGizmo.h"
#include "viewer/TransformMath.h"
#include "viewer/SelectionAdapter.h"

class QAction;
class QLabel;
class QToolBar;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

class CadViewer final : public QWidget
{
    Q_OBJECT

public:
    enum class SelectionMode
    {
        Object,
        Edge,
        Face
    };

    explicit CadViewer(QWidget* parent = nullptr);

    void display(const TopoDS_Shape& shape, const QString& featureId = {}, bool fitView = true);
    void updateFeature(const TopoDS_Shape& shape, const QString& featureId);
    void selectFeatures(const QStringList& featureIds);
    void setHiddenFeatures(const QStringList& featureIds);
    void retainFeatures(const QStringList& featureIds);
    void clear();
    bool hasDisplayedShapes() const;
    void fitAll();

    void setSelectionMode(SelectionMode mode);
    SelectionMode selectionMode() const;
    TopoDS_Shape selectedShape() const;
    void clearSelection();
    void setPushPullCommittedHandler(
        std::function<void(const QString&, int, const gp_Vec&, double)> handler
    );
    void setTransformCommittedHandler(
        std::function<void(const QString&, const gp_Trsf&)> handler
    );

signals:
    void featureSelectionChanged(const QStringList& featureIds);

protected:
    QPaintEngine* paintEngine() const override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void initializeOcc();
    void notifyFeatureSelection();
    void setupToolBar();
    void syncToolBarState();
    void setPushPullArmed(bool armed);
    void setTransformMode(bool enabled);
    void updateTransformGizmo();
    void beginTransform(cad::viewer::TransformHandle handle, const QPoint& position);
    void updateTransformPreview(const QPoint& position);
    void commitTransform();
    void cancelTransform();
    void updateTransformSnap(gp_Trsf& delta, gp_Pnt& pivot);
    void invalidateSnapReferenceCache();
    bool makeViewRay(const QPoint& position, cad::viewer::ViewRay& ray) const;
    void bindWindow();
    void updateHover(const QPoint& position);
    void selectAt(
        const QPoint& position,
        bool toggleSelection,
        bool cycleDetected = false
    );
    void resetDetectedCycle();
    void setXRayEnabled(bool enabled);
    void applySelectionMode();

    bool beginPushPull();
    void updatePushPullPreview(const QPoint& position);
    TopoDS_Shape buildPushPullResult(double distance) const;
    void commitPushPull();
    void cancelPushPull();

    QToolBar* toolBar_{nullptr};
    QLabel* xRayStatusLabel_{nullptr};
    QAction* selectObjectAction_{nullptr};
    QAction* selectEdgeAction_{nullptr};
    QAction* selectFaceAction_{nullptr};
    QAction* pushPullAction_{nullptr};
    QAction* transformAction_{nullptr};
    QAction* xRayAction_{nullptr};

    Handle(V3d_Viewer) viewer_;
    Handle(V3d_View) view_;
    Handle(AIS_InteractiveContext) context_;

    QPoint lastMousePosition_;
    QPoint mousePressPosition_;
    bool initialized_{false};
    SelectionMode selectionMode_{SelectionMode::Object};

    bool xRayEnabled_{false};
    bool detectedCycleActive_{false};
    QPoint detectedCyclePosition_;
    std::vector<Handle(AIS_Shape)> displayedShapes_;
    std::map<QString, Handle(AIS_Shape)> featureObjects_;

    bool pushPullArmed_{false};
    bool pushPullActive_{false};
    double pushPullDistance_{0.0};
    TopoDS_Face pushPullFace_;
    TopoDS_Shape pushPullBaseShape_;
    cad::viewer::PushPullDragState pushPullDragState_;
    gp_Vec pushPullNormal_;
    QString pushPullFeatureId_;
    int pushPullFaceIndex_{0};
    Handle(AIS_Shape) pushPullObject_;
    Handle(AIS_Shape) pushPullPreview_;
    std::function<void(const QString&, int, const gp_Vec&, double)> pushPullCommittedHandler_;
    std::function<void(const QString&, const gp_Trsf&)> transformCommittedHandler_;

    std::unique_ptr<cad::viewer::TransformGizmo> transformGizmo_;
    cad::viewer::SnapManager snapManager_;
    bool transformMode_{false};
    bool transformDragging_{false};
    cad::viewer::TransformHandle transformHandle_{cad::viewer::TransformHandle::None};
    QString transformFeatureId_;
    Handle(AIS_Shape) transformObject_;
    TopoDS_Shape transformOriginalShape_;
    gp_Pnt transformPivot_;
    gp_Trsf transformDelta_;
    cad::viewer::ViewRay transformStartRay_;
    cad::viewer::PushPullDragState transformTranslationDrag_;
    std::optional<gp_Pnt> transformRotationStartPoint_;
    std::optional<cad::viewer::SnapCandidate> activeSnap_;
    std::vector<cad::viewer::SnapReference> transformSourceReferences_;
    std::vector<cad::viewer::SnapReference> transformTargetReferences_;
    std::vector<cad::viewer::SnapCandidate> transformSnapCandidates_;
    std::map<QString, std::vector<cad::viewer::SnapReference>> snapReferenceCache_;

    std::unique_ptr<cad::viewer::OcctSelectionAdapter> selectionAdapter_;
    std::optional<cad::viewer::SelectionHit> hoveredSelectionHit_;

    Handle(Aspect_DisplayConnection) displayConnection_;
};
