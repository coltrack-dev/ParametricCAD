#include "viewer/CadViewer.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include <Aspect_DisplayConnection.hxx>
#include <OpenGl_GraphicDriver.hxx>

#ifdef _WIN32
#include <WNT_Window.hxx>
#else
#include <Xw_Window.hxx>
#endif

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

    fitAll();
}

void CadViewer::clear()
{
    if (!initialized_) {
        return;
    }

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

void CadViewer::mousePressEvent(QMouseEvent* event)
{
    lastMousePosition_ =
        event->position().toPoint();

    if (initialized_ &&
        event->button() == Qt::LeftButton) {

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

    if (event->buttons().testFlag(Qt::LeftButton)) {

        view_->Rotation(
            currentPosition.x(),
            currentPosition.y()
        );

    } else if (
        event->buttons().testFlag(Qt::MiddleButton)) {

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
    }

    lastMousePosition_ = currentPosition;
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
