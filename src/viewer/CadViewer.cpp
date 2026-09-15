#include "viewer/CadViewer.h"

#include <cmath>

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include <AIS_SelectionScheme.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <OpenGl_GraphicDriver.hxx>
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
}

void CadViewer::display(const TopoDS_Shape& shape)
{
    initializeOcc();

    Handle(AIS_Shape) interactiveShape =
        new AIS_Shape(shape);

    context_->Display(
        interactiveShape,
        Standard_True
    );

    applySelectionMode();
    fitAll();
}

void CadViewer::clear()
{
    if (!initialized_) {
        return;
    }

    cancelPushPull();
    context_->RemoveAll(Standard_True);
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

    selectionMode_ = mode;

    if (initialized_) {
        clearSelection();
        applySelectionMode();
    }
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

void CadViewer::updateHover(const QPoint& position)
{
    if (!initialized_ || pushPullActive_) {
        return;
    }

    context_->MoveTo(
        position.x(),
        position.y(),
        view_,
        Standard_True
    );
}

void CadViewer::selectAt(const QPoint& position, bool toggleSelection)
{
    updateHover(position);

    context_->SelectDetected(
        toggleSelection
            ? AIS_SelectionScheme_XOR
            : AIS_SelectionScheme_Replace
    );
    context_->UpdateCurrentViewer();
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
    unsetCursor();

    applySelectionMode();
    context_->UpdateCurrentViewer();
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
    unsetCursor();

    applySelectionMode();
    context_->UpdateCurrentViewer();
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
            event->modifiers().testFlag(Qt::ControlModifier)
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
        if (selectionMode_ != SelectionMode::Face) {
            setSelectionMode(SelectionMode::Face);
        } else {
            beginPushPull();
        }
        return;
    case Qt::Key_Escape:
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
