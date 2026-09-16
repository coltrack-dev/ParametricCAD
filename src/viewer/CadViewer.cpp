#include "viewer/CadViewer.h"

#include <cmath>
#include <algorithm>

#include <QAction>
#include <QActionGroup>
#include <QCursor>
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
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>

#ifdef _WIN32
#include <WNT_Window.hxx>
#else
#include <Xw_Window.hxx>
#endif

namespace
{
constexpr double PushPullUnitsPerPixel = 0.5;
constexpr double PushPullTolerance = 1.0e-6;
constexpr Standard_Real XRayTransparency = 0.65;
constexpr int DetectedCyclePositionTolerance = 3;
}

CadViewer::CadViewer(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);

    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

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

QPaintEngine* CadViewer::paintEngine() const
{
    return nullptr;
}

void CadViewer::initializeOcc()
{
    if (initialized_) {
        return;
    }

    // The graphic driver and Xw_Window must use the same display connection.
    displayConnection_ = new Aspect_DisplayConnection();

    Handle(OpenGl_GraphicDriver) graphicDriver =
        new OpenGl_GraphicDriver(displayConnection_);

    viewer_ = new V3d_Viewer(graphicDriver);
    viewer_->SetDefaultLights();
    viewer_->SetLightOn();

    context_ = new AIS_InteractiveContext(viewer_);
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
#ifdef _WIN32

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

    context_->MoveTo(
        position.x(),
        position.y(),
        view_,
        Standard_True
    );
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
            context_->MoveTo(
                position.x(),
                position.y(),
                view_,
                Standard_True
            );

            detectedCyclePosition_ = position;
            detectedCycleActive_ = true;
        }

        // MoveTo() highlights the nearest entity. Advance to the next
        // detected entity so Alt+Click reaches geometry behind it.
        context_->HilightNextDetected(view_, Standard_True);
    } else {
        updateHover(position);
    }

    context_->SelectDetected(
        toggleSelection
            ? AIS_SelectionScheme_XOR
            : AIS_SelectionScheme_Replace
    );
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

    const TopoDS_Face selectedFace = TopoDS::Face(selected);
    BRepAdaptor_Surface surface(selectedFace, Standard_True);

    if (surface.GetType() != GeomAbs_Plane) {
        return false;
    }

    gp_Dir normal = surface.Plane().Axis().Direction();

    if (selectedFace.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }

    pushPullFace_ = selectedFace;
    pushPullBaseShape_ = selectedObject->Shape();
    pushPullNormal_ = gp_Vec(normal);
    pushPullObject_ = selectedObject;
    pushPullStartPosition_ = mapFromGlobal(QCursor::pos());
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

    pushPullDistance_ =
        static_cast<double>(pushPullStartPosition_.y() - position.y()) *
        PushPullUnitsPerPixel;

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

    const TopoDS_Shape result =
        buildPushPullResult(pushPullDistance_);

    if (!result.IsNull() &&
        std::abs(pushPullDistance_) > PushPullTolerance) {

        pushPullObject_->SetShape(result);
    }

    if (!pushPullPreview_.IsNull()) {
        context_->Remove(pushPullPreview_, Standard_False);
        pushPullPreview_.Nullify();
    }

    context_->Display(pushPullObject_, Standard_False);
    context_->Redisplay(pushPullObject_, Standard_False);

    pushPullActive_ = false;
    pushPullDistance_ = 0.0;
    pushPullFace_.Nullify();
    pushPullBaseShape_.Nullify();
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
        updateHover(currentPosition);
    }

    lastMousePosition_ = currentPosition;
}

void CadViewer::mouseReleaseEvent(QMouseEvent* event)
{
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
    case Qt::Key_X:
        setXRayEnabled(!xRayEnabled_);
        return;
    case Qt::Key_Escape:
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
}

void CadViewer::notifyFeatureSelection()
{
    QStringList ids;
    if (initialized_) {
        for (context_->InitSelected(); context_->MoreSelected(); context_->NextSelected()) {
            // SelectedInteractive also identifies the parent of a picked face/edge.
            const auto object = context_->SelectedInteractive();
            for (const auto& [id, presentation] : featureObjects_) {
                if (presentation == object && !ids.contains(id)) {
                    ids.append(id);
                    break;
                }
            }
        }
    }
    emit featureSelectionChanged(ids);
}
