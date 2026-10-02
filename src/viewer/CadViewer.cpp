#include "viewer/CadViewer.h"

#include <cmath>
#include <algorithm>

#include <QAction>
#include <QDebug>
#include <QElapsedTimer>
#include <QActionGroup>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>

#include <AIS_DisplayMode.hxx>
#include <AIS_SelectionScheme.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GProp_GProps.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>

#if defined(Q_OS_MACOS) || defined(__APPLE__)
#include <Cocoa_Window.hxx>
#elif defined(_WIN32)
#include <WNT_Window.hxx>
#else
#include <Xw_Window.hxx>
#endif

using cad::viewer::SnapCandidate;
using cad::viewer::SnapKind;
using cad::viewer::TransformHandle;
using cad::viewer::ViewRay;

namespace
{
constexpr double PushPullTolerance = 1.0e-6;
constexpr double TransformPreviewTolerance = 1.0e-9;
constexpr Standard_Real XRayTransparency = 0.65;
constexpr int DetectedCyclePositionTolerance = 3;

bool makeViewRay(
    const Handle(V3d_View)& view,
    const QPoint& position,
    gp_Pnt& origin,
    gp_Dir& direction
)
{
    Standard_Real x = 0.0;
    Standard_Real y = 0.0;
    Standard_Real z = 0.0;
    Standard_Real vx = 0.0;
    Standard_Real vy = 0.0;
    Standard_Real vz = 0.0;
    view->ConvertWithProj(
        position.x(), position.y(),
        x, y, z, vx, vy, vz
    );
    const gp_Vec rayDirection(vx, vy, vz);
    if (rayDirection.Magnitude() <= 1.0e-9) {
        return false;
    }
    origin.SetCoord(x, y, z);
    direction = gp_Dir(rayDirection);
    return true;
}

QPointF projectWorldPoint(
    const Handle(V3d_View)& view,
    const gp_Pnt& point,
    const Standard_Integer width,
    const Standard_Integer height
)
{
    const gp_Pnt ndc = view->Camera()->Project(point);
    return QPointF(
        (ndc.X() + 1.0) * 0.5 * width,
        (1.0 - ndc.Y()) * 0.5 * height
    );
}

gp_Pnt shapeCenter(const TopoDS_Shape& shape)
{
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    Standard_Real xmin = 0.0;
    Standard_Real ymin = 0.0;
    Standard_Real zmin = 0.0;
    Standard_Real xmax = 0.0;
    Standard_Real ymax = 0.0;
    Standard_Real zmax = 0.0;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    return gp_Pnt(
        (xmin + xmax) * 0.5,
        (ymin + ymax) * 0.5,
        (zmin + zmax) * 0.5
    );
}

bool transformsClose(const gp_Trsf& first, const gp_Trsf& second)
{
    for (int row = 1; row <= 3; ++row) {
        for (int column = 1; column <= 4; ++column) {
            if (std::abs(first.Value(row, column) - second.Value(row, column))
                > TransformPreviewTolerance) {
                return false;
            }
        }
    }
    return true;
}
}

CadViewer::CadViewer(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);

    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Expanding
    );

    // Force Qt to create the native platform window.
    winId();

    setupToolBar();
}


void CadViewer::setupToolBar()
{
    toolBar_ = new QToolBar(this);
    toolBar_->setObjectName("cadToolBar");
    toolBar_->setMovable(false);
    toolBar_->setFloatable(false);
    toolBar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolBar_->setStyleSheet(
        "QToolBar#cadToolBar {"
        "  spacing: 4px;"
        "  padding: 5px;"
        "  background: rgba(38, 38, 42, 230);"
        "  border: 1px solid rgba(255, 255, 255, 45);"
        "  border-radius: 6px;"
        "}"
        "QToolBar#cadToolBar QToolButton {"
        "  color: #eeeeee;"
        "  padding: 6px 10px;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "}"
        "QToolBar#cadToolBar QToolButton:hover {"
        "  background: rgba(255, 255, 255, 28);"
        "}"
        "QToolBar#cadToolBar QToolButton:checked {"
        "  color: white;"
        "  background: #3569b8;"
        "  border-color: #6e9ee8;"
        "}"
    );

    auto* selectionActions = new QActionGroup(toolBar_);
    selectionActions->setExclusive(true);

    selectObjectAction_ = toolBar_->addAction("Object [1]");
    selectObjectAction_->setCheckable(true);
    selectionActions->addAction(selectObjectAction_);
    connect(selectObjectAction_, &QAction::triggered, this, [this]() {
        setSelectionMode(SelectionMode::Object);
    });

    selectEdgeAction_ = toolBar_->addAction("Edge [2]");
    selectEdgeAction_->setCheckable(true);
    selectionActions->addAction(selectEdgeAction_);
    connect(selectEdgeAction_, &QAction::triggered, this, [this]() {
        setSelectionMode(SelectionMode::Edge);
    });

    selectFaceAction_ = toolBar_->addAction("Face [3]");
    selectFaceAction_->setCheckable(true);
    selectionActions->addAction(selectFaceAction_);
    connect(selectFaceAction_, &QAction::triggered, this, [this]() {
        setSelectionMode(SelectionMode::Face);
    });

    toolBar_->addSeparator();

    pushPullAction_ = toolBar_->addAction("Push/Pull [P]");
    pushPullAction_->setCheckable(true);
    connect(pushPullAction_, &QAction::triggered, this, [this](bool checked) {
        if (checked) {
            setSelectionMode(SelectionMode::Face);
        }
        setPushPullArmed(checked);
    });

    transformAction_ = toolBar_->addAction("Move/Rotate [M]");
    transformAction_->setCheckable(true);
    connect(transformAction_, &QAction::triggered, this, [this](bool checked) {
        setTransformMode(checked);
    });

    xRayAction_ = toolBar_->addAction("X-Ray [X]");
    xRayAction_->setCheckable(true);
    connect(xRayAction_, &QAction::triggered, this, [this](bool checked) {
        setXRayEnabled(checked);
    });

    xRayStatusLabel_ = new QLabel("X-RAY ON", toolBar_);
    xRayStatusLabel_->setStyleSheet(
        "QLabel {"
        "  color: #1f1400;"
        "  background: #ffb020;"
        "  font-weight: 700;"
        "  padding: 5px 8px;"
        "  border-radius: 4px;"
        "}"
    );
    xRayStatusLabel_->hide();
    toolBar_->addWidget(xRayStatusLabel_);

    toolBar_->addSeparator();

    QAction* fitAction = toolBar_->addAction("Fit [F]");
    connect(fitAction, &QAction::triggered, this, [this]() {
        fitAll();
    });

    toolBar_->move(8, 8);
    toolBar_->raise();
    syncToolBarState();
}

void CadViewer::syncToolBarState()
{
    if (selectObjectAction_ != nullptr) {
        selectObjectAction_->setChecked(selectionMode_ == SelectionMode::Object);
    }
    if (selectEdgeAction_ != nullptr) {
        selectEdgeAction_->setChecked(selectionMode_ == SelectionMode::Edge);
    }
    if (selectFaceAction_ != nullptr) {
        selectFaceAction_->setChecked(selectionMode_ == SelectionMode::Face);
    }
    if (pushPullAction_ != nullptr) {
        pushPullAction_->setChecked(pushPullArmed_);
    }
    if (transformAction_ != nullptr) {
        transformAction_->setChecked(transformMode_);
    }
    if (xRayAction_ != nullptr) {
        xRayAction_->setChecked(xRayEnabled_);
    }
    if (xRayStatusLabel_ != nullptr) {
        xRayStatusLabel_->setVisible(xRayEnabled_);
    }
}

void CadViewer::setPushPullArmed(bool armed)
{
    if (!armed && pushPullActive_) {
        cancelPushPull();
    }

    pushPullArmed_ = armed;

    if (pushPullArmed_) {
        setCursor(Qt::CrossCursor);
    } else {
        unsetCursor();
    }

    syncToolBarState();
}

void CadViewer::setTransformCommittedHandler(
    std::function<void(const QString&, const gp_Trsf&)> handler
)
{
    transformCommittedHandler_ = std::move(handler);
}

void CadViewer::setTransformMode(const bool enabled)
{
    if (transformDragging_) {
        cancelTransform();
    }
    transformMode_ = enabled;
    if (!transformMode_ && transformGizmo_) {
        transformGizmo_->hide();
    } else {
        updateTransformGizmo();
    }
    syncToolBarState();
}

bool CadViewer::makeViewRay(const QPoint& position, ViewRay& ray) const
{
    return ::makeViewRay(view_, position, ray.origin, ray.direction);
}

void CadViewer::updateTransformGizmo()
{
    if (!initialized_ || !transformMode_ || !transformGizmo_ || transformDragging_) {
        return;
    }
    context_->InitSelected();
    if (!context_->MoreSelected()) {
        transformGizmo_->hide();
        return;
    }
    const auto object = Handle(AIS_Shape)::DownCast(context_->SelectedInteractive());
    if (object.IsNull()) {
        transformGizmo_->hide();
        return;
    }
    transformGizmo_->show(shapeCenter(object->Shape()), view_);
}

void CadViewer::beginTransform(
    const TransformHandle handle,
    const QPoint& position
)
{
    if (!transformGizmo_ || handle == TransformHandle::None) return;
    context_->InitSelected();
    if (!context_->MoreSelected()) return;
    transformObject_ = Handle(AIS_Shape)::DownCast(context_->SelectedInteractive());
    if (transformObject_.IsNull()) return;

    for (const auto& [id, object] : featureObjects_) {
        if (object == transformObject_) {
            transformFeatureId_ = id;
            break;
        }
    }
    if (transformFeatureId_.isEmpty()) return;

    transformHandle_ = handle;
    transformPivot_ = transformGizmo_->pivot();
    transformOriginalShape_ = transformObject_->Shape();
    transformDelta_ = gp_Trsf();
    activeSnap_.reset();
    auto cachedReferences = [this](const QString& id, const TopoDS_Shape& shape)
        -> const std::vector<cad::viewer::SnapReference>& {
        const auto found = snapReferenceCache_.find(id);
        if (found != snapReferenceCache_.end()) return found->second;
        return snapReferenceCache_.emplace(
            id, snapManager_.collectReferences(id, shape)).first->second;
    };
    transformSourceReferences_ = cachedReferences(
        transformFeatureId_, transformOriginalShape_);
    transformTargetReferences_.clear();
    for (const auto& [id, object] : featureObjects_) {
        if (object == transformObject_ || object.IsNull()) continue;
        const auto& references = cachedReferences(id, object->Shape());
        transformTargetReferences_.insert(
            transformTargetReferences_.end(), references.begin(), references.end());
    }
    transformSnapCandidates_ = snapManager_.buildCandidates(
        transformSourceReferences_, transformTargetReferences_);
    Standard_Integer windowWidth = 0;
    Standard_Integer windowHeight = 0;
    view_->Window()->Size(windowWidth, windowHeight);
    for (auto& candidate : transformSnapCandidates_) {
        candidate.target.screenPoint = projectWorldPoint(
            view_, candidate.targetPoint, windowWidth, windowHeight);
    }
    if (!makeViewRay(position, transformStartRay_)) return;

    const gp_Dir axis = transformGizmo_->axis(handle);
    if (handle == TransformHandle::TranslateX
        || handle == TransformHandle::TranslateY
        || handle == TransformHandle::TranslateZ) {
        const auto drag = cad::viewer::makePushPullDragState(
            transformPivot_, axis,
            view_->Camera()->Direction(),
            view_->Camera()->Up(),
            view_->Camera()->SideRight()
        );
        if (!drag) return;
        transformTranslationDrag_ = *drag;
    } else if (handle == TransformHandle::Center) {
        transformTranslationDrag_.anchor = transformPivot_;
        transformTranslationDrag_.normal = view_->Camera()->Direction();
        transformTranslationDrag_.dragPlane = gp_Pln(
            transformPivot_, view_->Camera()->Direction());
    } else {
        const auto start = cad::viewer::intersectRayWithPlane(
            transformStartRay_.origin,
            transformStartRay_.direction,
            gp_Pln(transformPivot_, axis));
        if (!start || gp_Vec(transformPivot_, *start).Magnitude() <= 1.0e-9) return;
        transformRotationStartPoint_ = *start;
    }
    transformDragging_ = true;
    if (transformGizmo_) transformGizmo_->setHovered(handle);
}

void CadViewer::invalidateSnapReferenceCache()
{
    snapReferenceCache_.clear();
    transformSnapCandidates_.clear();
}

void CadViewer::updateTransformSnap(gp_Trsf& delta, gp_Pnt& pivot)
{
    Standard_Integer width = 0;
    Standard_Integer height = 0;
    view_->Window()->Size(width, height);
    const auto project = [this, width, height](const gp_Pnt& point) {
        return projectWorldPoint(view_, point, width, height);
    };
    QElapsedTimer snapTimer;
    snapTimer.start();
    activeSnap_ = snapManager_.findCandidate(
        transformSnapCandidates_,
        delta,
        project,
        activeSnap_
    );
    if (snapTimer.elapsed() > 2) {
        qWarning() << "SnapManager::findCandidate took" << snapTimer.elapsed() << "ms";
    }
    if (activeSnap_) {
        const gp_Trsf correction = activeSnap_->correction;
        delta = correction;
        delta.Multiply(transformDelta_);
        pivot = transformPivot_;
        pivot.Transform(delta);
        if (transformGizmo_) transformGizmo_->setSnapActive(true);
        if (transformGizmo_) {
            transformGizmo_->setSnapTarget(activeSnap_->targetPoint, view_);
        }
    } else if (transformGizmo_) {
        transformGizmo_->setSnapActive(false);
        transformGizmo_->setSnapTarget(std::nullopt, view_);
    }
}

void CadViewer::updateTransformPreview(const QPoint& position)
{
    if (!transformDragging_ || transformObject_.IsNull()) return;
    ViewRay currentRay;
    if (!makeViewRay(position, currentRay)) return;

    gp_Trsf delta;
    if (transformHandle_ == TransformHandle::TranslateX
        || transformHandle_ == TransformHandle::TranslateY
        || transformHandle_ == TransformHandle::TranslateZ) {
        const auto distance = cad::viewer::translationDelta(
            transformTranslationDrag_, transformStartRay_, currentRay);
        if (!distance) return;
        delta = cad::viewer::translationTransform(
            transformGizmo_->axis(transformHandle_), *distance);
    } else if (transformHandle_ == TransformHandle::Center) {
        const auto start = cad::viewer::intersectRayWithPlane(
            transformStartRay_.origin,
            transformStartRay_.direction,
            transformTranslationDrag_.dragPlane);
        const auto current = cad::viewer::intersectRayWithPlane(
            currentRay.origin,
            currentRay.direction,
            transformTranslationDrag_.dragPlane);
        if (!start || !current) return;
        delta.SetTranslation(gp_Vec(*start, *current));
    } else {
        const auto angle = cad::viewer::rotationDelta(
            transformPivot_,
            transformGizmo_->axis(transformHandle_),
            transformStartRay_,
            currentRay);
        if (!angle) return;
        delta = cad::viewer::rotationTransform(
            transformPivot_,
            transformGizmo_->axis(transformHandle_),
            *angle);
    }

    const gp_Trsf previousDelta = transformDelta_;
    transformDelta_ = delta;
    gp_Pnt movedPivot = transformPivot_;
    updateTransformSnap(delta, movedPivot);
    if (transformsClose(previousDelta, delta)) {
        return;
    }
    transformDelta_ = delta;
    QElapsedTimer locationTimer;
    locationTimer.start();
    context_->SetLocation(transformObject_, TopLoc_Location(delta));
    if (locationTimer.elapsed() > 2) {
        qWarning() << "Transform preview SetLocation took"
                   << locationTimer.elapsed() << "ms";
    }
    QElapsedTimer redrawTimer;
    redrawTimer.start();
    context_->UpdateCurrentViewer();
    if (redrawTimer.elapsed() > 2) {
        qWarning() << "Transform preview viewer update took" << redrawTimer.elapsed() << "ms";
    }
}

void CadViewer::commitTransform()
{
    if (!transformDragging_) return;
    const QString id = transformFeatureId_;
    const gp_Trsf delta = transformDelta_;
    transformDragging_ = false;
    transformHandle_ = TransformHandle::None;
    activeSnap_.reset();
    if (transformGizmo_) {
        transformGizmo_->setSnapActive(false);
        transformGizmo_->setSnapTarget(std::nullopt, view_);
    }
    if (!transformObject_.IsNull()) {
        context_->ResetLocation(transformObject_);
    }
    if (delta.Form() != gp_Identity && transformCommittedHandler_) {
        transformCommittedHandler_(id, delta);
    } else if (!transformObject_.IsNull()) {
        context_->ResetLocation(transformObject_);
    }
    transformObject_.Nullify();
    transformFeatureId_.clear();
    transformSourceReferences_.clear();
    transformTargetReferences_.clear();
    transformSnapCandidates_.clear();
    updateTransformGizmo();
}

void CadViewer::cancelTransform()
{
    if (!transformDragging_) return;
    if (!transformObject_.IsNull()) {
        context_->ResetLocation(transformObject_);
    }
    transformDragging_ = false;
    transformHandle_ = TransformHandle::None;
    transformObject_.Nullify();
    transformFeatureId_.clear();
    transformSourceReferences_.clear();
    transformTargetReferences_.clear();
    transformSnapCandidates_.clear();
    activeSnap_.reset();
    if (transformGizmo_) {
        transformGizmo_->setSnapActive(false);
        transformGizmo_->setSnapTarget(std::nullopt, view_);
    }
    context_->UpdateCurrentViewer();
    updateTransformGizmo();
}

QPaintEngine* CadViewer::paintEngine() const
{
    return nullptr;
}

void CadViewer::initializeOcc()
{
    if (initialized_) {
        return;
    }

    // Aspect_DisplayConnection is required by the OCCT graphic driver. It is
    // a no-op on Cocoa, while it owns the X11 connection on Linux.
    displayConnection_ = new Aspect_DisplayConnection();

    Handle(OpenGl_GraphicDriver) graphicDriver =
        new OpenGl_GraphicDriver(displayConnection_);

    viewer_ = new V3d_Viewer(graphicDriver);
    viewer_->SetDefaultLights();
    viewer_->SetLightOn();

    context_ = new AIS_InteractiveContext(viewer_);
    selectionAdapter_ = std::make_unique<cad::viewer::OcctSelectionAdapter>(
        context_, featureObjects_);
    transformGizmo_ = std::make_unique<cad::viewer::TransformGizmo>(context_);
    view_ = viewer_->CreateView();

    bindWindow();

    view_->SetBackgroundColor(Quantity_NOC_GRAY20);

    view_->TriedronDisplay(
        Aspect_TOTP_LEFT_LOWER,
        Quantity_NOC_WHITE,
        0.08,
        V3d_ZBUFFER
    );

    view_->MustBeResized();

    initialized_ = true;
    applySelectionMode();
}

void CadViewer::bindWindow()
{
#if defined(Q_OS_MACOS) || defined(__APPLE__)

    Handle(Cocoa_Window) window =
        new Cocoa_Window(
            reinterpret_cast<NSView*>(winId())
        );

#elif defined(_WIN32)

    Handle(WNT_Window) window =
        new WNT_Window(
            reinterpret_cast<Aspect_Handle>(winId())
        );

#else

    const WId nativeWindowId = winId();

    Handle(Xw_Window) window =
        new Xw_Window(
            displayConnection_,
            static_cast<Aspect_Drawable>(nativeWindowId)
        );

#endif

    view_->SetWindow(window);

    if (!window->IsMapped()) {
        window->Map();
    }
}

void CadViewer::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    initializeOcc();
}

void CadViewer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (!initialized_) {
        initializeOcc();
    }

    view_->Redraw();
}

void CadViewer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (initialized_) {
        view_->MustBeResized();
    }

    if (toolBar_ != nullptr) {
        toolBar_->move(8, 8);
        toolBar_->raise();
    }
}

void CadViewer::display(const TopoDS_Shape& shape, const QString& featureId, bool fitView)
{
    initializeOcc();

    Handle(AIS_Shape) interactiveShape =
        new AIS_Shape(shape);

    if (!shape.IsNull()) {
        if (shape.ShapeType() == TopAbs_FACE || TopExp_Explorer(shape, TopAbs_FACE).More()) {
            interactiveShape->SetDisplayMode(AIS_Shaded);
            const auto& drawer = interactiveShape->Attributes();
            drawer->SetFaceBoundaryDraw(Standard_True);
            drawer->SetFaceBoundaryAspect(
                new Prs3d_LineAspect(Quantity_NOC_BLACK, Aspect_TOL_SOLID, 1.2));
        } else if (shape.ShapeType() == TopAbs_WIRE) {
            interactiveShape->SetDisplayMode(AIS_WireFrame);
        }
    }

    if (xRayEnabled_) {
        interactiveShape->SetTransparency(XRayTransparency);
    }

    context_->Display(
        interactiveShape,
        Standard_True
    );
    displayedShapes_.push_back(interactiveShape);
    if (!featureId.isEmpty()) {
        featureObjects_[featureId] = interactiveShape;
    }
    invalidateSnapReferenceCache();

    applySelectionMode();
    if (fitView) {
        fitAll();
    }
}

void CadViewer::updateFeature(const TopoDS_Shape& shape, const QString& featureId)
{
    if (shape.IsNull()) return;
    const auto found = featureObjects_.find(featureId);
    if (found == featureObjects_.end()) {
        display(shape, featureId, false);
        return;
    }
    const auto& object = found->second;
    if (object->Shape().IsEqual(shape)) return;

    cancelPushPull();
    resetDetectedCycle();
    invalidateSnapReferenceCache();
    object->SetShape(shape);
    context_->Redisplay(object, Standard_True);
}

void CadViewer::setHiddenFeatures(const QStringList& featureIds)
{
    if (!initialized_) return;
    bool changed = false;
    for (const auto& [id, object] : featureObjects_) {
        const bool visible = !featureIds.contains(id);
        if (visible == bool(context_->IsDisplayed(object))) continue;
        if (visible) {
            context_->Display(object, Standard_False);
        } else {
            context_->Erase(object, Standard_False);
        }
        changed = true;
    }
    if (changed) {
        invalidateSnapReferenceCache();
        resetDetectedCycle();
        context_->UpdateCurrentViewer();
    }
}

void CadViewer::retainFeatures(const QStringList& featureIds)
{
    if (!initialized_) return;
    bool changed = false;
    for (auto it = featureObjects_.begin(); it != featureObjects_.end();) {
        if (featureIds.contains(it->first)) {
            ++it;
            continue;
        }
        cancelPushPull();
        context_->Remove(it->second, Standard_False);
        std::erase(displayedShapes_, it->second);
        it = featureObjects_.erase(it);
        changed = true;
    }
    if (changed) {
        invalidateSnapReferenceCache();
        resetDetectedCycle();
        context_->UpdateCurrentViewer();
    }
}

void CadViewer::clear()
{
    if (!initialized_) {
        return;
    }

    cancelPushPull();
    context_->RemoveAll(Standard_True);
    displayedShapes_.clear();
    featureObjects_.clear();
    invalidateSnapReferenceCache();
    resetDetectedCycle();
}

bool CadViewer::hasDisplayedShapes() const
{
    return !displayedShapes_.empty();
}

void CadViewer::fitAll()
{
    if (!initialized_) {
        return;
    }

    view_->FitAll();
    view_->ZFitAll();
    view_->Redraw();
}

void CadViewer::setSelectionMode(SelectionMode mode)
{
    if (pushPullActive_) {
        cancelPushPull();
    }

    pushPullArmed_ = false;
    unsetCursor();
    selectionMode_ = mode;

    if (initialized_) {
        clearSelection();
        applySelectionMode();
    }

    syncToolBarState();
}

CadViewer::SelectionMode CadViewer::selectionMode() const
{
    return selectionMode_;
}

TopoDS_Shape CadViewer::selectedShape() const
{
    if (!initialized_) {
        return {};
    }

    context_->InitSelected();

    if (!context_->MoreSelected() || !context_->HasSelectedShape()) {
        return {};
    }

    return context_->SelectedShape();
}

void CadViewer::setPushPullCommittedHandler(
    std::function<void(const QString&, int, const gp_Vec&, double)> handler
)
{
    pushPullCommittedHandler_ = std::move(handler);
}

void CadViewer::clearSelection()
{
    if (!initialized_) {
        return;
    }

    context_->ClearSelected(Standard_True);
    notifyFeatureSelection();
}

void CadViewer::applySelectionMode()
{
    if (!initialized_) {
        return;
    }

    context_->Deactivate();

    Standard_Integer mode = 0;

    switch (selectionMode_) {
    case SelectionMode::Object:
        mode = 0;
        break;
    case SelectionMode::Edge:
        mode = AIS_Shape::SelectionMode(TopAbs_EDGE);
        break;
    case SelectionMode::Face:
        mode = AIS_Shape::SelectionMode(TopAbs_FACE);
        break;
    }

    context_->Activate(mode, Standard_True);
    if (transformGizmo_) {
        transformGizmo_->deactivateSelection();
    }
    context_->UpdateCurrentViewer();
}

void CadViewer::resetDetectedCycle()
{
    detectedCycleActive_ = false;
}

void CadViewer::setXRayEnabled(bool enabled)
{
    xRayEnabled_ = enabled;
    resetDetectedCycle();

    if (!initialized_) {
        return;
    }

    for (const Handle(AIS_Shape)& shape : displayedShapes_) {
        if (shape.IsNull()) {
            continue;
        }

        if (xRayEnabled_) {
            shape->SetTransparency(XRayTransparency);
        } else {
            shape->UnsetTransparency();
        }

        context_->Redisplay(shape, Standard_False);
    }

    if (!pushPullPreview_.IsNull()) {
        if (xRayEnabled_) {
            pushPullPreview_->SetTransparency(XRayTransparency);
        } else {
            pushPullPreview_->UnsetTransparency();
        }

        context_->Redisplay(pushPullPreview_, Standard_False);
    }

    context_->UpdateCurrentViewer();
    syncToolBarState();
}

void CadViewer::updateHover(const QPoint& position)
{
    if (!initialized_ || pushPullActive_) {
        return;
    }

    resetDetectedCycle();

    if (selectionAdapter_) {
        selectionAdapter_->moveTo(position, view_, true);
        const auto detected = selectionAdapter_->detectedHit();
        if (!detected) {
            hoveredSelectionHit_.reset();
        } else if (!hoveredSelectionHit_
                   || !hoveredSelectionHit_->hasSameTransientIdentity(*detected)) {
            hoveredSelectionHit_ = detected;
        }
    }
}

void CadViewer::selectAt(
    const QPoint& position,
    bool toggleSelection,
    bool cycleDetected)
{
    if (!initialized_ || pushPullActive_) {
        return;
    }

    if (cycleDetected && xRayEnabled_) {
        const QPoint delta = position - detectedCyclePosition_;
        const bool samePickPosition =
            detectedCycleActive_ &&
            std::abs(delta.x()) <= DetectedCyclePositionTolerance &&
            std::abs(delta.y()) <= DetectedCyclePositionTolerance;

        if (!samePickPosition) {
            if (selectionAdapter_) {
                selectionAdapter_->moveTo(position, view_, true);
            }

            detectedCyclePosition_ = position;
            detectedCycleActive_ = true;
        }

        // MoveTo() highlights the nearest entity. Advance to the next
        // detected entity so Alt+Click reaches geometry behind it.
        context_->HilightNextDetected(view_, Standard_True);
    } else {
        updateHover(position);
    }

    if (selectionAdapter_) {
        selectionAdapter_->selectDetected(
            toggleSelection
                ? AIS_SelectionScheme_XOR
                : AIS_SelectionScheme_Replace
        );
    }
    context_->UpdateCurrentViewer();
    notifyFeatureSelection();
}

bool CadViewer::beginPushPull()
{
    if (!initialized_) {
        return false;
    }

    const TopoDS_Shape selected = selectedShape();

    if (selected.IsNull() || selected.ShapeType() != TopAbs_FACE) {
        return false;
    }

    context_->InitSelected();
    if (!context_->MoreSelected()) {
        return false;
    }

    Handle(AIS_InteractiveObject) selectedInteractive =
        context_->SelectedInteractive();

    Handle(AIS_Shape) selectedObject =
        Handle(AIS_Shape)::DownCast(selectedInteractive);

    if (selectedObject.IsNull()) {
        return false;
    }

    QString featureId;
    for (const auto& [id, object] : featureObjects_) {
        if (object == selectedObject) {
            featureId = id;
            break;
        }
    }
    if (featureId.isEmpty()) {
        return false;
    }

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(selectedObject->Shape(), TopAbs_FACE, faces);
    const int faceIndex = faces.FindIndex(selected);
    if (faceIndex <= 0) {
        return false;
    }

    const TopoDS_Face selectedFace = TopoDS::Face(selected);
    BRepAdaptor_Surface surface(selectedFace, Standard_True);

    if (surface.GetType() != GeomAbs_Plane) {
        return false;
    }

    gp_Dir normal = surface.Plane().Axis().Direction();

    if (selectedFace.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }

    gp_Pnt anchor;
    bool anchorFound = false;
    gp_Pnt rayOrigin;
    gp_Dir rayDirection;
    if (::makeViewRay(view_, lastMousePosition_, rayOrigin, rayDirection)) {
        const auto hit = cad::viewer::intersectRayWithPlane(
            rayOrigin,
            rayDirection,
            surface.Plane()
        );
        if (hit) {
            anchor = *hit;
            anchorFound = true;
        }
    }
    if (!anchorFound) {
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(selectedFace, properties);
        anchor = properties.CentreOfMass();
    }

    const auto dragState = cad::viewer::makePushPullDragState(
        anchor,
        normal,
        view_->Camera()->Direction(),
        view_->Camera()->Up(),
        view_->Camera()->SideRight()
    );
    if (!dragState) {
        return false;
    }

    pushPullFace_ = selectedFace;
    pushPullBaseShape_ = selectedObject->Shape();
    pushPullDragState_ = *dragState;
    pushPullNormal_ = gp_Vec(normal);
    pushPullFeatureId_ = featureId;
    pushPullFaceIndex_ = faceIndex;
    pushPullObject_ = selectedObject;
    pushPullDistance_ = 0.0;
    pushPullActive_ = true;

    context_->UpdateCurrentViewer();

    setCursor(Qt::SizeVerCursor);
    return true;
}

TopoDS_Shape CadViewer::buildPushPullResult(double distance) const
{
    if (!pushPullActive_ || std::abs(distance) <= PushPullTolerance) {
        return pushPullBaseShape_;
    }

    const gp_Vec extrusionVector = pushPullNormal_ * distance;
    const TopoDS_Shape prism =
        BRepPrimAPI_MakePrism(pushPullFace_, extrusionVector).Shape();

    if (distance > 0.0) {
        BRepAlgoAPI_Fuse fuse(pushPullBaseShape_, prism);
        fuse.Build();
        return fuse.IsDone() ? fuse.Shape() : TopoDS_Shape();
    }

    BRepAlgoAPI_Cut cut(pushPullBaseShape_, prism);
    cut.Build();
    return cut.IsDone() ? cut.Shape() : TopoDS_Shape();
}

void CadViewer::updatePushPullPreview(const QPoint& position)
{
    if (!pushPullActive_) {
        return;
    }

    gp_Pnt rayOrigin;
    gp_Dir rayDirection;
    if (!::makeViewRay(view_, position, rayOrigin, rayDirection)) {
        return;
    }

    const auto distance = cad::viewer::computePushPullDistance(
        pushPullDragState_,
        rayOrigin,
        rayDirection
    );
    if (!distance) {
        return;
    }
    pushPullDistance_ = *distance;

    if (std::abs(pushPullDistance_) <= PushPullTolerance) {
        if (!pushPullPreview_.IsNull()) {
            context_->Remove(pushPullPreview_, Standard_False);
            pushPullPreview_.Nullify();
        }

        if (!pushPullObject_.IsNull()) {
            context_->Display(pushPullObject_, Standard_False);
        }

        context_->UpdateCurrentViewer();
        return;
    }

    const TopoDS_Shape previewShape =
        buildPushPullResult(pushPullDistance_);

    if (previewShape.IsNull()) {
        return;
    }

    if (pushPullPreview_.IsNull()) {
        // Keep the original model visible until a real Push/Pull distance
        // exists. Only then replace it with the preview result.
        context_->Erase(pushPullObject_, Standard_False);
        pushPullPreview_ = new AIS_Shape(previewShape);
        if (xRayEnabled_) {
            pushPullPreview_->SetTransparency(XRayTransparency);
        }
        context_->Display(pushPullPreview_, Standard_False);
    } else {
        pushPullPreview_->SetShape(previewShape);
        context_->Redisplay(pushPullPreview_, Standard_False);
    }

    context_->UpdateCurrentViewer();
}

void CadViewer::commitPushPull()
{
    if (!pushPullActive_) {
        return;
    }

    const bool hasChange = std::abs(pushPullDistance_) > PushPullTolerance;
    const QString featureId = pushPullFeatureId_;
    const int faceIndex = pushPullFaceIndex_;
    const gp_Vec normal = pushPullNormal_;
    const double distance = pushPullDistance_;

    if (!pushPullPreview_.IsNull()) {
        context_->Remove(pushPullPreview_, Standard_False);
        pushPullPreview_.Nullify();
    }

    if (!pushPullObject_.IsNull()) {
        context_->Display(pushPullObject_, Standard_False);
    }

    pushPullActive_ = false;
    pushPullDistance_ = 0.0;
    pushPullFace_.Nullify();
    pushPullBaseShape_.Nullify();
    pushPullFeatureId_.clear();
    pushPullFaceIndex_ = 0;
    pushPullObject_.Nullify();
    if (pushPullArmed_) {
        setCursor(Qt::CrossCursor);
    } else {
        unsetCursor();
    }

    applySelectionMode();
    context_->UpdateCurrentViewer();
    syncToolBarState();

    if (hasChange && pushPullCommittedHandler_) {
        pushPullCommittedHandler_(featureId, faceIndex, normal, distance);
    }
}

void CadViewer::cancelPushPull()
{
    if (!pushPullActive_) {
        return;
    }

    if (!pushPullPreview_.IsNull()) {
        context_->Remove(pushPullPreview_, Standard_False);
        pushPullPreview_.Nullify();
    }

    if (!pushPullObject_.IsNull()) {
        context_->Display(pushPullObject_, Standard_False);
    }

    pushPullActive_ = false;
    pushPullDistance_ = 0.0;
    pushPullFace_.Nullify();
    pushPullBaseShape_.Nullify();
    pushPullFeatureId_.clear();
    pushPullFaceIndex_ = 0;
    pushPullObject_.Nullify();

    if (pushPullArmed_) {
        setCursor(Qt::CrossCursor);
    } else {
        unsetCursor();
    }

    applySelectionMode();
    context_->UpdateCurrentViewer();
    syncToolBarState();
}

void CadViewer::mousePressEvent(QMouseEvent* event)
{
    lastMousePosition_ =
        event->position().toPoint();
    mousePressPosition_ = lastMousePosition_;

    if (transformDragging_) {
        if (event->button() == Qt::RightButton) {
            cancelTransform();
            return;
        }
        if (event->button() == Qt::LeftButton) return;
    }

    if (transformMode_ && event->button() == Qt::LeftButton
        && transformGizmo_) {
        const auto handle = transformGizmo_->hitTest(lastMousePosition_, view_);
        if (handle != TransformHandle::None) {
            beginTransform(handle, lastMousePosition_);
            return;
        }
    }

    if (pushPullActive_) {
        if (event->button() == Qt::LeftButton) {
            updatePushPullPreview(lastMousePosition_);
            commitPushPull();
            return;
        }

        if (event->button() == Qt::RightButton) {
            cancelPushPull();
            return;
        }
    }

    if (pushPullArmed_ && event->button() == Qt::LeftButton) {
        selectAt(
            lastMousePosition_,
            false,
            event->modifiers().testFlag(Qt::AltModifier)
        );

        if (beginPushPull()) {
            return;
        }
    }

    if (initialized_ &&
        event->button() == Qt::MiddleButton &&
        !event->modifiers().testFlag(Qt::ShiftModifier)) {

        view_->StartRotation(
            lastMousePosition_.x(),
            lastMousePosition_.y()
        );
    }

    QWidget::mousePressEvent(event);
}

void CadViewer::mouseMoveEvent(QMouseEvent* event)
{
    if (!initialized_) {
        return;
    }

    const QPoint currentPosition =
        event->position().toPoint();

    if (transformDragging_) {
        QElapsedTimer gizmoTimer;
        gizmoTimer.start();
        updateTransformPreview(currentPosition);
        if (gizmoTimer.elapsed() > 2) {
            qWarning() << "TransformGizmo mouse move handling took"
                       << gizmoTimer.elapsed() << "ms";
        }
        lastMousePosition_ = currentPosition;
        return;
    }

    if (pushPullActive_) {
        updatePushPullPreview(currentPosition);
        lastMousePosition_ = currentPosition;
        return;
    }

    if (event->buttons().testFlag(Qt::MiddleButton)) {

        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            const int deltaX =
                currentPosition.x() -
                lastMousePosition_.x();

            const int deltaY =
                lastMousePosition_.y() -
                currentPosition.y();

            view_->Pan(
                deltaX,
                deltaY
            );
        } else {
            view_->Rotation(
                currentPosition.x(),
                currentPosition.y()
            );
        }

    } else if (event->buttons() == Qt::NoButton) {
        if (transformMode_ && transformGizmo_) {
            QElapsedTimer gizmoTimer;
            gizmoTimer.start();
            transformGizmo_->setHovered(
                transformGizmo_->hitTest(currentPosition, view_));
            if (gizmoTimer.elapsed() > 2) {
                qWarning() << "TransformGizmo mouse move handling took"
                           << gizmoTimer.elapsed() << "ms";
            }
        } else {
            updateHover(currentPosition);
        }
    }

    lastMousePosition_ = currentPosition;
}

void CadViewer::mouseReleaseEvent(QMouseEvent* event)
{
    if (transformDragging_) {
        if (event->button() == Qt::LeftButton) {
            commitTransform();
        }
        return;
    }
    if (pushPullActive_) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    if (initialized_ &&
        event->button() == Qt::LeftButton &&
        event->position().toPoint() == mousePressPosition_) {

        selectAt(
            event->position().toPoint(),
            event->modifiers().testFlag(Qt::ControlModifier),
            event->modifiers().testFlag(Qt::AltModifier)
        );
    }

    QWidget::mouseReleaseEvent(event);
}

void CadViewer::wheelEvent(QWheelEvent* event)
{
    if (!initialized_) {
        return;
    }

    const double factor =
        event->angleDelta().y() > 0
            ? 0.8
            : 1.25;

    view_->SetZoom(factor);
    view_->Redraw();
}

void CadViewer::keyPressEvent(QKeyEvent* event)
{
    if (pushPullActive_) {
        if (event->key() == Qt::Key_Escape) {
            cancelPushPull();
            return;
        }

        if (event->key() == Qt::Key_Return ||
            event->key() == Qt::Key_Enter) {
            commitPushPull();
            return;
        }
    }

    switch (event->key()) {
    case Qt::Key_1:
        setSelectionMode(SelectionMode::Object);
        return;
    case Qt::Key_2:
        setSelectionMode(SelectionMode::Edge);
        return;
    case Qt::Key_3:
        setSelectionMode(SelectionMode::Face);
        return;
    case Qt::Key_P:
        setSelectionMode(SelectionMode::Face);
        setPushPullArmed(true);
        return;
    case Qt::Key_M:
        setTransformMode(!transformMode_);
        return;
    case Qt::Key_X:
        setXRayEnabled(!xRayEnabled_);
        return;
    case Qt::Key_Escape:
        if (transformDragging_) {
            cancelTransform();
            return;
        }
        if (transformMode_) {
            setTransformMode(false);
            return;
        }
        if (pushPullArmed_) {
            setPushPullArmed(false);
            clearSelection();
            return;
        }
        clearSelection();
        return;
    case Qt::Key_F:
        fitAll();
        return;
    default:
        break;
    }

    QWidget::keyPressEvent(event);
}

void CadViewer::selectFeatures(const QStringList& featureIds)
{
    if (!initialized_) return;

    // Only change AIS selection. Keep presentations, camera and selection mode.
    // This is the tree-to-viewer path; do not echo a selection notification.
    context_->ClearSelected(Standard_False);
    for (const auto& [id, object] : featureObjects_) {
        if (featureIds.contains(id) && context_->IsDisplayed(object)) {
            context_->AddOrRemoveSelected(object, Standard_False);
        }
    }
    context_->UpdateCurrentViewer();
    updateTransformGizmo();
}

void CadViewer::notifyFeatureSelection()
{
    QStringList ids;
    if (initialized_ && selectionAdapter_) {
        for (const auto& hit : selectionAdapter_->selectedHits()) {
            if (!ids.contains(hit.item.featureId)) {
                ids.append(hit.item.featureId);
            }
        }
    }
    emit featureSelectionChanged(ids);
    updateTransformGizmo();
}
