#include "viewer/CadViewer.h"
#include "viewer/SketchEntityPicker.h"

#include "model/Body.h"

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdio>

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QFocusEvent>
#include <QActionGroup>
#include <QLabel>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <StdSelect_BRepOwner.hxx>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>

#include <AIS_DisplayMode.hxx>
#include <TCollection_ExtendedString.hxx>
#include <AIS_SelectionScheme.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GProp_GProps.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Circ.hxx>
#include <gp_Ax2.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>

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
using cad::application::VisibilityMode;

Q_LOGGING_CATEGORY(pcadViewerLog, "parametric.viewer")

namespace
{
constexpr double PushPullTolerance = 1.0e-6;
constexpr double TransformPreviewTolerance = 1.0e-9;
constexpr Standard_Real XRayTransparency = 0.65;
constexpr int DetectedCyclePositionTolerance = 3;

constexpr double AxisIndicatorScale = 0.12;
constexpr double AxisIndicatorArm = 0.65;
constexpr double AxisIndicatorHitRadius = 0.30;
constexpr double SketchTrimHitPixels = 8.0;
constexpr Standard_Real GhostTransparency = 0.78;

const char* selectionModeName(const CadViewer::SelectionMode mode)
{
    switch (mode) {
    case CadViewer::SelectionMode::Object: return "Object";
    case CadViewer::SelectionMode::Edge: return "Edge";
    case CadViewer::SelectionMode::Face: return "Face";
    case CadViewer::SelectionMode::Vertex: return "Vertex";
    }
    return "Unknown";
}

QString selectionObjectPointer(const Handle(AIS_InteractiveObject)& object)
{
    if (object.IsNull()) return "null";
    return QString::asprintf("%p", static_cast<const void*>(object.operator->()));
}

QString activeSelectionModes(const Handle(AIS_InteractiveContext)& context,
                             const Handle(AIS_InteractiveObject)& object)
{
    if (context.IsNull() || object.IsNull()) return "none";
    TColStd_ListOfInteger modes;
    context->ActivatedModes(object, modes);
    if (modes.IsEmpty()) return "none";

    QStringList result;
    for (TColStd_ListOfInteger::Iterator iterator(modes);
         iterator.More(); iterator.Next()) {
        result.push_back(QString::number(iterator.Value()));
    }
    return result.join(',');
}
constexpr double Pi = 3.14159265358979323846;
constexpr double OrbitElevationLimit = 89.0 * Pi / 180.0;
constexpr double OrbitRadiansPerPixel = 0.01;

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

gp_Pnt sketchWorldPoint(
    const cad::parametric::SketchFrame& frame, const gp_Pnt2d& point)
{
    gp_Pnt result = frame.origin;
    result.Translate(gp_Vec(frame.xDirection) * point.X()
        + gp_Vec(frame.yDirection) * point.Y());
    return result;
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

TopoDS_Shape makeSectionGrid(
    const CadViewer::SectionAxis axis,
    const double position,
    const Bnd_Box& bounds)
{
    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double x0 = xmin, x1 = xmax;
    const double y0 = ymin, y1 = ymax;
    const double z0 = zmin, z1 = zmax;
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    const auto add = [&builder, &compound](const gp_Pnt& first, const gp_Pnt& second) {
        builder.Add(compound, BRepBuilderAPI_MakeEdge(first, second).Edge());
    };
    const double thirds[] = {0.0, 0.5, 1.0};
    for (const double fraction : thirds) {
        if (axis == CadViewer::SectionAxis::X) {
            const double y = y0 + (y1 - y0) * fraction;
            const double z = z0 + (z1 - z0) * fraction;
            add(gp_Pnt(position, y0, z), gp_Pnt(position, y1, z));
            add(gp_Pnt(position, y, z0), gp_Pnt(position, y, z1));
        } else if (axis == CadViewer::SectionAxis::Y) {
            const double x = x0 + (x1 - x0) * fraction;
            const double z = z0 + (z1 - z0) * fraction;
            add(gp_Pnt(x0, position, z), gp_Pnt(x1, position, z));
            add(gp_Pnt(x, position, z0), gp_Pnt(x, position, z1));
        } else {
            const double x = x0 + (x1 - x0) * fraction;
            const double y = y0 + (y1 - y0) * fraction;
            add(gp_Pnt(x0, y, position), gp_Pnt(x1, y, position));
            add(gp_Pnt(x, y0, position), gp_Pnt(x, y1, position));
        }
    }
    return compound;
}

TopoDS_Shape makeSectionNormalIndicator(
    const CadViewer::SectionAxis axis,
    const gp_Pnt& origin,
    const Bnd_Box& bounds,
    const bool flipped)
{
    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double length = std::max({xmax - xmin, ymax - ymin, zmax - zmin}) * 0.35;
    gp_Vec direction(axis == CadViewer::SectionAxis::X ? gp_Vec(1, 0, 0)
        : axis == CadViewer::SectionAxis::Y ? gp_Vec(0, 1, 0) : gp_Vec(0, 0, 1));
    if (flipped) direction.Reverse();
    direction *= std::max(length, 1.0e-3);
    const gp_Pnt tip = origin.Translated(direction);
    gp_Vec side(axis == CadViewer::SectionAxis::X ? gp_Vec(0, 1, 0)
        : axis == CadViewer::SectionAxis::Y ? gp_Vec(1, 0, 0) : gp_Vec(1, 0, 0));
    side *= std::max(length * 0.12, 0.02);
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound, BRepBuilderAPI_MakeEdge(origin, tip).Edge());
    builder.Add(compound, BRepBuilderAPI_MakeEdge(tip, tip.Translated(-direction * 0.2 + side)).Edge());
    builder.Add(compound, BRepBuilderAPI_MakeEdge(tip, tip.Translated(-direction * 0.2 - side)).Edge());
    return compound;
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

void configureSketchPresentation(
    const Handle(AIS_Shape)& interactiveShape,
    const QString& featureId)
{
    if (interactiveShape.IsNull() || !featureId.startsWith("sketch-")) return;
    interactiveShape->SetDisplayMode(AIS_WireFrame);
    interactiveShape->SetColor(Quantity_NOC_RED);
    interactiveShape->SetWidth(3.0);
}

TopoDS_Shape makeSketchPreviewShape(
    const gp_Pnt& first,
    const gp_Pnt& current,
    const gp_Dir& normal,
    const CadViewer::SketchPreviewTool tool)
{
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);

    const double radius = first.Distance(current);
    if (tool == CadViewer::SketchPreviewTool::Circle && radius > 1.0e-9) {
        const gp_Circ circle(gp_Ax2(first, normal), radius);
        auto circleEdge = BRepBuilderAPI_MakeEdge(circle);
        if (circleEdge.IsDone()) {
            const TopoDS_Shape edge = circleEdge.Edge();
            builder.Add(compound, edge);
        }
    }

    auto radiusEdge = BRepBuilderAPI_MakeEdge(first, current);
    if (radiusEdge.IsDone()) {
        const TopoDS_Shape edge = radiusEdge.Edge();
        builder.Add(compound, edge);
    }

    const double markerSize = std::max(radius * 0.08, 1.0e-3);
    gp_Dir markerX(1.0, 0.0, 0.0);
    if (std::abs(markerX.Dot(normal)) > 0.95) markerX = gp_Dir(0.0, 1.0, 0.0);
    gp_Dir markerY(gp_Vec(normal).Crossed(gp_Vec(markerX)));
    markerX = gp_Dir(gp_Vec(markerY).Crossed(gp_Vec(normal)));
    for (const auto& direction : {markerX, markerY}) {
        gp_Pnt start = first;
        gp_Pnt end = first;
        start.Translate(-gp_Vec(direction) * markerSize);
        end.Translate(gp_Vec(direction) * markerSize);
        auto markerEdge = BRepBuilderAPI_MakeEdge(start, end);
        if (markerEdge.IsDone()) {
            const TopoDS_Shape edge = markerEdge.Edge();
            builder.Add(compound, edge);
        }
    }
    return compound;
}

TopoDS_Shape makeTrimPreviewShape(
    const cad::parametric::SketchFrame& frame,
    const std::vector<cad::parametric::SketchEntity>& entities)
{
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (const auto& entity : entities) {
        TopoDS_Edge edge;
        if (const auto* line = std::get_if<cad::parametric::SketchLine>(&entity)) {
            auto edgeBuilder = BRepBuilderAPI_MakeEdge(
                sketchWorldPoint(frame, line->start), sketchWorldPoint(frame, line->end));
            if (edgeBuilder.IsDone()) edge = edgeBuilder.Edge();
        } else if (const auto* arc = std::get_if<cad::parametric::SketchArc>(&entity)) {
            const gp_Circ circle(gp_Ax2(sketchWorldPoint(frame, arc->center), frame.normal), arc->radius);
            const double sweep = arc->signedSweep();
            if (sweep > 0.0) {
                auto edgeBuilder = BRepBuilderAPI_MakeEdge(circle, arc->startAngle,
                    arc->startAngle + sweep);
                if (edgeBuilder.IsDone()) edge = edgeBuilder.Edge();
            } else {
                auto edgeBuilder = BRepBuilderAPI_MakeEdge(circle, arc->startAngle + sweep,
                    arc->startAngle);
                if (edgeBuilder.IsDone()) edge = TopoDS::Edge(edgeBuilder.Edge().Reversed());
            }
        } else {
            const auto& circle = std::get<cad::parametric::SketchCircle>(entity);
            auto edgeBuilder = BRepBuilderAPI_MakeEdge(gp_Circ(
                gp_Ax2(sketchWorldPoint(frame, circle.center), frame.normal), circle.radius));
            if (edgeBuilder.IsDone()) edge = edgeBuilder.Edge();
        }
        if (!edge.IsNull()) builder.Add(compound, edge);
    }
    return compound;
}
}

CadViewer::CadViewer(
    QWidget* parent,
    cad::application::InteractiveOperationSession* operationSession)
    : QWidget(parent),
      operationSession_(operationSession ? operationSession : &localOperationSession_)
{
    selectionTraceEnabled_ = qEnvironmentVariableIsSet("PARAMETRICCAD_TRACE_SELECTION");
    if (selectionTraceEnabled_) {
        selectionTraceFile_.open("/tmp/parametriccad-selection.log",
            std::ios::out | std::ios::trunc);
    }
    traceSelectionLifecycle("CadViewer constructed");
    performanceDiagnostics_ = qEnvironmentVariableIsSet("PARAMETRIC_CAD_PERF");
    snapDisabled_ = qEnvironmentVariableIsSet("PARAMETRIC_CAD_DISABLE_SNAP");
    selectionDisabled_ = qEnvironmentVariableIsSet("PARAMETRIC_CAD_DISABLE_SELECTION");
    const auto configuredDisplayMode = qEnvironmentVariable("PARAMETRIC_CAD_DISPLAY_MODE");
    if (configuredDisplayMode.compare("wireframe", Qt::CaseInsensitive) == 0)
        displayMode_ = DisplayMode::Wireframe;
    else if (configuredDisplayMode.compare("shaded", Qt::CaseInsensitive) == 0)
        displayMode_ = DisplayMode::Shaded;
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

    if (performanceDiagnostics_) {
        qInfo().noquote() << "CadViewer performance diagnostics enabled"
                          << "snapDisabled=" << snapDisabled_
                          << "selectionDisabled=" << selectionDisabled_
                          << "displayMode=" << qEnvironmentVariable("PARAMETRIC_CAD_DISPLAY_MODE");
    }
}

void CadViewer::traceSelectionLifecycle(const QString& message) const
{
    if (!selectionTraceEnabled_) return;
    const QString line = QString("[SEL %1] %2")
        .arg(++selectionTraceSequence_, 5, 10, QLatin1Char('0'))
        .arg(message);
    const QByteArray encoded = line.toLocal8Bit();
    std::fprintf(stderr, "%s\n", encoded.constData());
    std::fflush(stderr);
    if (selectionTraceFile_.is_open()) {
        selectionTraceFile_ << encoded.constData() << '\n';
        selectionTraceFile_.flush();
    }
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

    selectVertexAction_ = toolBar_->addAction("Vertex [4]");
    selectVertexAction_->setCheckable(true);
    selectionActions->addAction(selectVertexAction_);
    connect(selectVertexAction_, &QAction::triggered, this, [this]() {
        setSelectionMode(SelectionMode::Vertex);
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

    toolBar_->addSeparator();
    auto* orbitActions = new QActionGroup(toolBar_);
    orbitActions->setExclusive(true);
    turntableOrbitAction_ = toolBar_->addAction("Locked Z");
    turntableOrbitAction_->setCheckable(true);
    orbitActions->addAction(turntableOrbitAction_);
    connect(turntableOrbitAction_, &QAction::triggered, this, [this]() {
        setOrbitMode(OrbitMode::Turntable);
    });
    freeOrbitAction_ = toolBar_->addAction("Free Orbit");
    freeOrbitAction_->setCheckable(true);
    orbitActions->addAction(freeOrbitAction_);
    connect(freeOrbitAction_, &QAction::triggered, this, [this]() {
        setOrbitMode(OrbitMode::Free);
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
    if (selectVertexAction_ != nullptr) {
        selectVertexAction_->setChecked(selectionMode_ == SelectionMode::Vertex);
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
    if (turntableOrbitAction_ != nullptr) {
        turntableOrbitAction_->setChecked(orbitMode_ == OrbitMode::Turntable);
    }
    if (freeOrbitAction_ != nullptr) {
        freeOrbitAction_->setChecked(orbitMode_ == OrbitMode::Free);
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

void CadViewer::setTransformCopyCommittedHandler(
    std::function<void(const QString&, const gp_Trsf&)> handler
)
{
    transformCopyCommittedHandler_ = std::move(handler);
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

void CadViewer::enterSketchMode(
    const gp_Pnt& origin, const gp_Dir& xDirection,
    const gp_Dir& yDirection, const gp_Dir& normal,
    const QString& featureId)
{
    traceSelectionLifecycle(QString("enterSketchMode begin mode=%1 dirty=%2 managed=%3")
        .arg(selectionModeName(selectionMode_))
        .arg(selectionActivationDirty_)
        .arg(managedSelectionModes_.size()));
    sketchPreviousSelectionMode_ = selectionMode_;
    clearSelection();
    releaseManagedSelection(featureId);
    editingSketchFeatureId_ = featureId;
    traceSelectionLifecycle(QString("Sketch edit owns feature=%1; excluded from model selection")
        .arg(featureId));
    sketchMode_ = true;
    sketchOrigin_ = origin;
    sketchXDirection_ = xDirection;
    sketchYDirection_ = yDirection;
    sketchNormal_ = normal;
    setSelectionMode(SelectionMode::Object);
    if (view_ && !view_->Camera().IsNull()) {
        // Establish a useful world-to-screen scale before changing the camera
        // orientation. Without this, a scale inherited from an unrelated 3D
        // view can make a small mouse movement produce an unexpectedly large
        // local sketch radius.
        view_->FitAll();
        view_->ZFitAll();
        const auto camera = view_->Camera();
        const double distance = std::max(camera->Distance(), 1.0);
        gp_Pnt eye = origin;
        // The camera looks from the face's outward side toward the sketch
        // plane, so its view direction is -normal.
        eye.Translate(gp_Vec(normal) * distance);
        camera->SetEyeAndCenter(eye, origin);
        camera->SetUp(yDirection);
        invalidateSnapProjectionCache("SKETCH_MODE: aligned camera");
        view_->Redraw();
    }
    traceSelectionLifecycle(QString("enterSketchMode end mode=%1 dirty=%2 managed=%3")
        .arg(selectionModeName(selectionMode_))
        .arg(selectionActivationDirty_)
        .arg(managedSelectionModes_.size()));
}

void CadViewer::exitSketchMode()
{
    traceSelectionLifecycle(QString("exitSketchMode begin mode=%1 previous=%2 dirty=%3 managed=%4")
        .arg(selectionModeName(selectionMode_))
        .arg(selectionModeName(sketchPreviousSelectionMode_))
        .arg(selectionActivationDirty_)
        .arg(managedSelectionModes_.size()));
    // Clear detection/selection and remove edit-only presentations before
    // restoring the previous AIS selection mode.  Restoring the mode first
    // asks OCCT to deactivate selectors that can still own the sketch edit
    // presentations being removed below.
    sketchMode_ = false;
    if (!context_.IsNull()) {
        traceSelectionLifecycle("ClearDetected BEFORE");
        context_->ClearDetected(Standard_False);
        traceSelectionLifecycle("ClearDetected AFTER");
        traceSelectionLifecycle("ClearSelected BEFORE");
        context_->ClearSelected(Standard_False);
        traceSelectionLifecycle("ClearSelected AFTER");
    }
    sketchPreviewFirstPoint_.reset();
    if (!sketchPreviewObject_.IsNull() && !context_.IsNull()) {
        traceSelectionLifecycle(QString("AIS Remove sketchPreview obj=%1 before")
            .arg(selectionObjectPointer(sketchPreviewObject_)));
        context_->Remove(sketchPreviewObject_, Standard_False);
        traceSelectionLifecycle("AIS Remove sketchPreview after");
    }
    sketchPreviewObject_.Nullify();
    clearSketchTrimPreview();
    clearSketchConstraintMarkers();
    clearSketchConstraintHighlight();
    traceSelectionLifecycle(QString("Sketch edit releases feature=%1; register fresh")
        .arg(editingSketchFeatureId_));
    editingSketchFeatureId_.clear();
    setSelectionMode(sketchPreviousSelectionMode_);
    traceSelectionLifecycle(QString("exitSketchMode end mode=%1 dirty=%2 managed=%3")
        .arg(selectionModeName(selectionMode_))
        .arg(selectionActivationDirty_)
        .arg(managedSelectionModes_.size()));
}

bool CadViewer::sketchMode() const noexcept
{
    return sketchMode_;
}

void CadViewer::setSketchPreviewTool(const SketchPreviewTool tool)
{
    traceSelectionLifecycle(QString("setSketchPreviewTool tool=%1 preview=%2")
        .arg(static_cast<int>(tool)).arg(selectionObjectPointer(sketchPreviewObject_)));
    sketchPreviewTool_ = tool;
    sketchPreviewFirstPoint_.reset();
    if (!sketchPreviewObject_.IsNull() && !context_.IsNull()) {
        traceSelectionLifecycle(QString("AIS Remove sketchPreview obj=%1 before")
            .arg(selectionObjectPointer(sketchPreviewObject_)));
        context_->Remove(sketchPreviewObject_, Standard_True);
        traceSelectionLifecycle("AIS Remove sketchPreview after");
    }
    sketchPreviewObject_.Nullify();
    clearSketchTrimPreview();
}

void CadViewer::setSketchPointClickedHandler(
    std::function<void(const gp_Pnt2d&, double)> handler)
{
    sketchPointClickedHandler_ = std::move(handler);
}

void CadViewer::setSketchMouseMovedHandler(
    std::function<void(const gp_Pnt2d&, double)> handler)
{
    sketchMouseMovedHandler_ = std::move(handler);
}

void CadViewer::setSketchCancelHandler(std::function<void()> handler)
{
    sketchCancelHandler_ = std::move(handler);
}

void CadViewer::setSketchConstraintMarkerClickedHandler(
    std::function<void(const std::string&)> handler)
{
    sketchConstraintMarkerClickedHandler_ = std::move(handler);
}

void CadViewer::setSketchConstraintMarkerHoveredHandler(
    std::function<void(const std::string&)> handler)
{
    sketchConstraintMarkerHoveredHandler_ = std::move(handler);
}

std::optional<gp_Pnt2d> CadViewer::sketchPointAtScreen(const QPoint& position) const
{
    if (!sketchMode_) return std::nullopt;
    gp_Pnt rayOrigin;
    gp_Dir rayDirection;
    if (!::makeViewRay(view_, position, rayOrigin, rayDirection)) return std::nullopt;
    const auto world = cad::viewer::intersectRayWithPlane(
        rayOrigin, rayDirection, gp_Pln(sketchOrigin_, sketchNormal_));
    if (!world) return std::nullopt;
    const gp_Vec offset(sketchOrigin_, *world);
    return gp_Pnt2d(offset.Dot(gp_Vec(sketchXDirection_)),
                    offset.Dot(gp_Vec(sketchYDirection_)));
}

double CadViewer::sketchLocalToleranceFromPixels(const QPoint& position, const double pixels) const
{
    if (pixels <= 0.0) return 0.0;
    const auto origin = sketchPointAtScreen(position);
    const auto horizontal = sketchPointAtScreen(position + QPoint(
        std::max(1, static_cast<int>(std::ceil(pixels))), 0));
    if (origin && horizontal) {
        const double scale = origin->Distance(*horizontal)
            / static_cast<double>(std::max(1, static_cast<int>(std::ceil(pixels))));
        return scale * pixels;
    }
    return 0.0;
}

void CadViewer::setSketchTrimPreview(
    const std::vector<cad::parametric::SketchEntity>& entities)
{
    if (entities.empty() || !sketchMode_ || context_.IsNull()) {
        clearSketchTrimPreview();
        return;
    }
    const auto shape = makeTrimPreviewShape(
        {sketchOrigin_, sketchXDirection_, sketchYDirection_, sketchNormal_}, entities);
    if (shape.IsNull()) {
        clearSketchTrimPreview();
        return;
    }
    if (sketchTrimPreviewObject_.IsNull()) {
        sketchTrimPreviewObject_ = new AIS_Shape(shape);
        sketchTrimPreviewObject_->SetDisplayMode(AIS_WireFrame);
        sketchTrimPreviewObject_->SetColor(Quantity_NOC_YELLOW);
        sketchTrimPreviewObject_->SetWidth(4.0);
        context_->Display(sketchTrimPreviewObject_, Standard_True);
    } else {
        sketchTrimPreviewObject_->SetShape(shape);
        context_->Redisplay(sketchTrimPreviewObject_, Standard_True);
    }
}

void CadViewer::setSketchExtendPreview(
    const std::vector<cad::parametric::SketchEntity>& entities)
{
    setSketchTrimPreview(entities);
}

void CadViewer::clearSketchTrimPreview()
{
    if (!sketchTrimPreviewObject_.IsNull() && !context_.IsNull()) {
        traceSelectionLifecycle(QString("AIS Remove trimPreview obj=%1 before")
            .arg(selectionObjectPointer(sketchTrimPreviewObject_)));
        context_->Remove(sketchTrimPreviewObject_, Standard_True);
        traceSelectionLifecycle("AIS Remove trimPreview after");
    }
    sketchTrimPreviewObject_.Nullify();
}

void CadViewer::clearSketchConstraintMarkers()
{
    if (!context_.IsNull()) {
        for (const auto& marker : sketchConstraintMarkers_) {
            if (!marker.presentation.IsNull()) {
                traceSelectionLifecycle(QString("AIS Remove constraintMarker id=%1 obj=%2 before")
                    .arg(QString::fromStdString(marker.constraintId))
                    .arg(selectionObjectPointer(marker.presentation)));
                context_->Remove(marker.presentation, Standard_False);
                traceSelectionLifecycle("AIS Remove constraintMarker after");
            }
        }
    }
    sketchConstraintMarkers_.clear();
    if (!context_.IsNull()) context_->CurrentViewer()->Redraw();
}

void CadViewer::setSketchConstraintMarkers(
    const cad::parametric::SketchFeature& sketch, const std::string& selectedConstraintId)
{
    clearSketchConstraintMarkers();
    if (!sketchMode_ || context_.IsNull()) return;
    const auto world = [this](const gp_Pnt2d& point) {
        gp_Pnt result = sketchOrigin_;
        result.Translate(gp_Vec(sketchXDirection_) * point.X()
            + gp_Vec(sketchYDirection_) * point.Y());
        return result;
    };
    const auto find = [&sketch](const std::string& id) -> const cad::parametric::SketchEntity* {
        for (const auto& entity : sketch.entities()) {
            if (std::visit([&](const auto& value) { return value.id == id; }, entity)) return &entity;
        }
        return nullptr;
    };
    const auto point = [&](const cad::parametric::SketchPointRef& ref) -> std::optional<gp_Pnt2d> {
        const auto* entity = find(ref.entityId);
        if (!entity) return std::nullopt;
        if (const auto* line = std::get_if<cad::parametric::SketchLine>(entity))
            return ref.role == cad::parametric::SketchPointRole::LineStart ? line->start : line->end;
        if (const auto* arc = std::get_if<cad::parametric::SketchArc>(entity))
            return ref.role == cad::parametric::SketchPointRole::ArcStart ? arc->startPoint() : arc->endPoint();
        if (const auto* circle = std::get_if<cad::parametric::SketchCircle>(entity))
            if (ref.role == cad::parametric::SketchPointRole::CircleCenter) return circle->center;
        if (const auto* arc = std::get_if<cad::parametric::SketchArc>(entity))
            if (ref.role == cad::parametric::SketchPointRole::ArcCenter) return arc->center;
        return std::nullopt;
    };
    for (const auto& constraint : sketch.constraints()) {
        std::string text;
        gp_Pnt2d position;
        bool valid = false;
        if (const auto* item = std::get_if<cad::parametric::HorizontalConstraint>(&constraint)) {
            const auto* entity = find(item->entityId);
            if (entity) {
                if (const auto* line = std::get_if<cad::parametric::SketchLine>(entity)) {
                    position = gp_Pnt2d((line->start.X() + line->end.X()) * 0.5,
                        (line->start.Y() + line->end.Y()) * 0.5 + 2.0);
                    valid = true;
                }
            }
            text = "H";
        } else if (const auto* item = std::get_if<cad::parametric::VerticalConstraint>(&constraint)) {
            const auto* entity = find(item->entityId);
            if (entity) {
                if (const auto* line = std::get_if<cad::parametric::SketchLine>(entity)) {
                    position = gp_Pnt2d((line->start.X() + line->end.X()) * 0.5 + 2.0,
                        (line->start.Y() + line->end.Y()) * 0.5);
                    valid = true;
                }
            }
            text = "V";
        } else if (const auto* item = std::get_if<cad::parametric::CoincidentConstraint>(&constraint)) {
            const auto p = point(item->a);
            if (p) { position = *p; valid = true; }
            text = "•";
        } else if (const auto* item = std::get_if<cad::parametric::DistanceConstraint>(&constraint)) {
            const auto* entity = find(item->entityId);
            if (entity) if (const auto* line = std::get_if<cad::parametric::SketchLine>(entity)) {
                position = gp_Pnt2d((line->start.X() + line->end.X()) * 0.5,
                    (line->start.Y() + line->end.Y()) * 0.5 + 3.0);
                valid = true;
            }
            text = "D " + std::to_string(item->value);
        } else if (const auto* item = std::get_if<cad::parametric::RadiusConstraint>(&constraint)) {
            const auto* entity = find(item->entityId);
            if (entity) {
                if (const auto* circle = std::get_if<cad::parametric::SketchCircle>(entity)) {
                    position = gp_Pnt2d(circle->center.X() + circle->radius, circle->center.Y()); valid = true;
                } else if (const auto* arc = std::get_if<cad::parametric::SketchArc>(entity)) {
                    position = arc->startPoint(); valid = true;
                }
            }
            text = "R " + std::to_string(item->value);
        } else if (const auto* item = std::get_if<cad::parametric::HorizontalDistanceConstraint>(&constraint)) {
            const auto a = point(item->first); const auto b = point(item->second);
            if (a && b) { position = gp_Pnt2d((a->X() + b->X()) * 0.5, (a->Y() + b->Y()) * 0.5 + 3.0); valid = true; }
            text = "X " + std::to_string(item->value);
        } else if (const auto* item = std::get_if<cad::parametric::VerticalDistanceConstraint>(&constraint)) {
            const auto a = point(item->first); const auto b = point(item->second);
            if (a && b) { position = gp_Pnt2d((a->X() + b->X()) * 0.5 + 3.0, (a->Y() + b->Y()) * 0.5); valid = true; }
            text = "Y " + std::to_string(item->value);
        } else if (const auto* item = std::get_if<cad::parametric::AngleConstraint>(&constraint)) {
            const auto* entity = find(item->entityId);
            if (entity) if (const auto* line = std::get_if<cad::parametric::SketchLine>(entity)) {
                position = gp_Pnt2d((line->start.X() + line->end.X()) * 0.5,
                    (line->start.Y() + line->end.Y()) * 0.5 + 3.0); valid = true;
            }
            text = "A " + std::to_string(item->radians * 180.0 / 3.14159265358979323846);
        } else if (const auto* item = std::get_if<cad::parametric::ParallelConstraint>(&constraint)) {
            const auto* first = find(item->firstLineId); const auto* second = find(item->secondLineId);
            const auto* lineA = first ? std::get_if<cad::parametric::SketchLine>(first) : nullptr;
            const auto* lineB = second ? std::get_if<cad::parametric::SketchLine>(second) : nullptr;
            if (lineA && lineB) {
                position = gp_Pnt2d((lineA->start.X() + lineA->end.X() + lineB->start.X() + lineB->end.X()) * 0.25,
                    (lineA->start.Y() + lineA->end.Y() + lineB->start.Y() + lineB->end.Y()) * 0.25); valid = true;
            }
            text = "∥";
        } else if (const auto* item = std::get_if<cad::parametric::PerpendicularConstraint>(&constraint)) {
            const auto* first = find(item->firstLineId); const auto* second = find(item->secondLineId);
            const auto* lineA = first ? std::get_if<cad::parametric::SketchLine>(first) : nullptr;
            const auto* lineB = second ? std::get_if<cad::parametric::SketchLine>(second) : nullptr;
            if (lineA && lineB) {
                position = gp_Pnt2d((lineA->start.X() + lineA->end.X() + lineB->start.X() + lineB->end.X()) * 0.25,
                    (lineA->start.Y() + lineA->end.Y() + lineB->start.Y() + lineB->end.Y()) * 0.25); valid = true;
            }
            text = "⟂";
        } else if (const auto* item = std::get_if<cad::parametric::AngleBetweenLinesConstraint>(&constraint)) {
            const auto* first = find(item->referenceLineId); const auto* second = find(item->dependentLineId);
            const auto* lineA = first ? std::get_if<cad::parametric::SketchLine>(first) : nullptr;
            const auto* lineB = second ? std::get_if<cad::parametric::SketchLine>(second) : nullptr;
            if (lineA && lineB) {
                position = gp_Pnt2d((lineA->start.X() + lineA->end.X() + lineB->start.X() + lineB->end.X()) * 0.25,
                    (lineA->start.Y() + lineA->end.Y() + lineB->start.Y() + lineB->end.Y()) * 0.25); valid = true;
            }
            text = "A " + std::to_string(item->angleRadians * 180.0 / 3.14159265358979323846);
        } else if (const auto* item = std::get_if<cad::parametric::TangentConstraint>(&constraint)) {
            const auto* first = find(item->firstEntityId); const auto* second = find(item->secondEntityId);
            const auto pointFor = [](const auto* entity) {
                return std::visit([](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, cad::parametric::SketchLine>)
                        return gp_Pnt2d((value.start.X() + value.end.X()) * 0.5, (value.start.Y() + value.end.Y()) * 0.5);
                    else return value.center;
                }, *entity);
            };
            if (first && second) { position = pointFor(first); valid = true; }
            text = "T";
        } else if (const auto* item = std::get_if<cad::parametric::EqualConstraint>(&constraint)) {
            const auto* first = find(item->referenceEntityId); const auto* second = find(item->dependentEntityId);
            if (first && second) {
                position = std::visit([](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, cad::parametric::SketchLine>)
                        return gp_Pnt2d((value.start.X() + value.end.X()) * 0.5, (value.start.Y() + value.end.Y()) * 0.5);
                    else return value.center;
                }, *first); valid = true;
            }
            text = "=";
        }
        if (!valid) continue;
        const auto constraintId = std::visit([](const auto& value) { return value.id; }, constraint);
        auto marker = new AIS_TextLabel();
        marker->SetText(TCollection_ExtendedString(text.c_str()));
        marker->SetPosition(world(position));
        marker->SetColor(constraintId == selectedConstraintId ? Quantity_NOC_ORANGE : Quantity_NOC_YELLOW);
        marker->SetHeight(14.0);
        context_->Display(marker, Standard_False);
        sketchConstraintMarkers_.push_back({constraintId, marker});
    }
    context_->CurrentViewer()->Redraw();
}

void CadViewer::clearSketchConstraintHighlight()
{
    if (!sketchConstraintHighlightObject_.IsNull() && !context_.IsNull()) {
        traceSelectionLifecycle(QString("AIS Remove constraintHighlight obj=%1 before")
            .arg(selectionObjectPointer(sketchConstraintHighlightObject_)));
        context_->Remove(sketchConstraintHighlightObject_, Standard_True);
        traceSelectionLifecycle("AIS Remove constraintHighlight after");
    }
    sketchConstraintHighlightObject_.Nullify();
}

void CadViewer::setSketchConstraintHighlight(
    const cad::parametric::SketchFeature& sketch, const std::string& constraintId)
{
    clearSketchConstraintHighlight();
    if (!sketchMode_ || context_.IsNull()) return;
    std::vector<cad::parametric::SketchEntity> targets;
    const auto entityId = [](const auto& item) { return item.entityId; };
    for (const auto& constraint : sketch.constraints()) {
        if (!std::visit([&](const auto& item) { return item.id == constraintId; }, constraint)) continue;
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, cad::parametric::CoincidentConstraint>) {
                for (const auto& id : {item.a.entityId, item.b.entityId}) {
                    for (const auto& entity : sketch.entities())
                        if (std::visit([&](const auto& value) { return value.id == id; }, entity)) targets.push_back(entity);
                }
            } else if constexpr (std::is_same_v<T, cad::parametric::HorizontalDistanceConstraint>
                || std::is_same_v<T, cad::parametric::VerticalDistanceConstraint>) {
                for (const auto& id : {item.first.entityId, item.second.entityId}) {
                    for (const auto& entity : sketch.entities())
                        if (std::visit([&](const auto& value) { return value.id == id; }, entity)) targets.push_back(entity);
                }
            } else if constexpr (std::is_same_v<T, cad::parametric::ParallelConstraint>
                || std::is_same_v<T, cad::parametric::PerpendicularConstraint>) {
                for (const auto& id : {item.firstLineId, item.secondLineId}) {
                    for (const auto& entity : sketch.entities())
                        if (std::visit([&](const auto& value) { return value.id == id; }, entity)) targets.push_back(entity);
                }
            } else if constexpr (std::is_same_v<T, cad::parametric::AngleBetweenLinesConstraint>) {
                for (const auto& id : {item.referenceLineId, item.dependentLineId}) {
                    for (const auto& entity : sketch.entities())
                        if (std::visit([&](const auto& value) { return value.id == id; }, entity)) targets.push_back(entity);
                }
            } else if constexpr (std::is_same_v<T, cad::parametric::TangentConstraint>) {
                for (const auto& id : {item.firstEntityId, item.secondEntityId}) {
                    for (const auto& entity : sketch.entities())
                        if (std::visit([&](const auto& value) { return value.id == id; }, entity)) targets.push_back(entity);
                }
            } else if constexpr (std::is_same_v<T, cad::parametric::EqualConstraint>) {
                for (const auto& id : {item.referenceEntityId, item.dependentEntityId}) {
                    for (const auto& entity : sketch.entities())
                        if (std::visit([&](const auto& value) { return value.id == id; }, entity)) targets.push_back(entity);
                }
            } else {
                for (const auto& entity : sketch.entities())
                    if (std::visit([&](const auto& value) { return value.id == entityId(item); }, entity)) targets.push_back(entity);
            }
        }, constraint);
        break;
    }
    if (targets.empty()) return;
    const auto shape = makeTrimPreviewShape(
        {sketchOrigin_, sketchXDirection_, sketchYDirection_, sketchNormal_}, targets);
    if (shape.IsNull()) return;
    sketchConstraintHighlightObject_ = new AIS_Shape(shape);
    sketchConstraintHighlightObject_->SetDisplayMode(AIS_WireFrame);
    sketchConstraintHighlightObject_->SetColor(Quantity_NOC_GREEN);
    sketchConstraintHighlightObject_->SetWidth(6.0);
    context_->Display(sketchConstraintHighlightObject_, Standard_True);
}

void CadViewer::updateTransformGizmo()
{
    if (!initialized_ || !transformMode_ || !transformGizmo_ || transformDragging_) {
        return;
    }
    if (!selectionState_.primary) {
        transformGizmo_->hide();
        return;
    }
    const auto selectedObject = featureObjects_.find(selectionState_.primary->featureId);
    if (selectedObject == featureObjects_.end()
        || selectedObject->second.IsNull()
        || !context_->IsDisplayed(selectedObject->second)
        || !featureIsSelectable(selectedObject->first)) {
        transformGizmo_->hide();
        return;
    }
    transformGizmo_->show(shapeCenter(selectedObject->second->Shape()), view_);
}

void CadViewer::beginTransform(
    const TransformHandle handle,
    const QPoint& position,
    const bool copyMode
)
{
    if (!transformGizmo_ || handle == TransformHandle::None) return;
    if (!selectionState_.primary) return;
    const auto selectedObject = featureObjects_.find(selectionState_.primary->featureId);
    if (selectedObject == featureObjects_.end()
        || selectedObject->second.IsNull()
        || !context_->IsDisplayed(selectedObject->second)
        || !featureIsSelectable(selectedObject->first)) return;

    transformObject_ = selectedObject->second;
    transformFeatureId_ = selectionState_.primary->featureId;
    if (transformFeatureId_.isEmpty()) return;

    transformHandle_ = handle;
    transformCopyMode_ = copyMode;
    transformPivot_ = transformGizmo_->pivot();
    transformOriginalShape_ = transformObject_->Shape();
    transformDelta_ = gp_Trsf();
    activeSnap_.reset();
    QElapsedTimer snapTimer;
    if (performanceDiagnostics_) snapTimer.start();
    auto cachedReferences = [this](const QString& id, const TopoDS_Shape& shape)
        -> const std::vector<cad::viewer::SnapReference>& {
        const auto found = snapReferenceCache_.find(id);
        if (found != snapReferenceCache_.end()) return found->second;
        return snapReferenceCache_.emplace(
            id, snapManager_.collectReferences(id, shape)).first->second;
    };
    if (!snapDisabled_) {
        transformSourceReferences_ = cachedReferences(
            transformFeatureId_, transformOriginalShape_);
    }
    transformTargetReferences_.clear();
    for (const auto& [id, object] : featureObjects_) {
        if (snapDisabled_) break;
        // Snap only against geometry that is actually visible/selectable. Pattern,
        // boolean and copy features hide their dependencies; traversing those
        // presentations here duplicates the same topology in the snap target set.
        if (object == transformObject_ || object.IsNull()
            || !context_->IsDisplayed(object)
            || !featureIsSelectable(id)) continue;
        const auto& references = cachedReferences(id, object->Shape());
        transformTargetReferences_.insert(
            transformTargetReferences_.end(), references.begin(), references.end());
    }
    Standard_Integer windowWidth = 0;
    Standard_Integer windowHeight = 0;
    view_->Window()->Size(windowWidth, windowHeight);
    for (auto& reference : transformTargetReferences_) {
        const QString key = reference.ownerId + ':' + reference.subshapeId;
        auto projection = snapScreenProjectionCache_.find(key);
        if (projection == snapScreenProjectionCache_.end()) {
            projection = snapScreenProjectionCache_.emplace(
                key, projectWorldPoint(view_, reference.point, windowWidth, windowHeight)).first;
        }
        reference.screenPoint = projection->second;
    }
    auto cachedCandidates = snapCandidateCache_.find(transformFeatureId_);
    if (snapDisabled_) {
        transformSnapCandidates_ = std::make_shared<std::vector<SnapCandidate>>();
    } else if (cachedCandidates != snapCandidateCache_.end()) {
        transformSnapCandidates_ = cachedCandidates->second;
    } else {
        transformSnapCandidates_ = std::make_shared<std::vector<SnapCandidate>>(
            snapManager_.buildCandidates(
                transformSourceReferences_, transformTargetReferences_));
        snapCandidateCache_.emplace(transformFeatureId_, transformSnapCandidates_);
    }
    for (auto& candidate : *transformSnapCandidates_) {
        const QString key = candidate.target.ownerId + ':' + candidate.target.subshapeId;
        auto projection = snapScreenProjectionCache_.find(key);
        if (projection == snapScreenProjectionCache_.end()) {
            projection = snapScreenProjectionCache_.emplace(
                key, projectWorldPoint(view_, candidate.target.point,
                                       windowWidth, windowHeight)).first;
        }
        candidate.target.screenPoint = projection->second;
    }
    snapScreenIndex_ = std::make_shared<cad::viewer::SnapScreenIndex>();
    snapScreenIndex_->rebuild(*transformSnapCandidates_);
    if (performanceDiagnostics_) {
        qInfo().noquote() << "Snap preparation: sourceRefsBeforeDedup="
                          << transformSourceReferences_.size()
                          << "targetRefsBeforeDedup=" << transformTargetReferences_.size()
                          << "candidatePairsBeforeDedup="
                          << transformSourceReferences_.size()
                              * transformTargetReferences_.size()
                          << "uniqueSources=" << cad::viewer::SnapManager::lastUniqueSourceReferenceCount()
                          << "uniqueTargets=" << cad::viewer::SnapManager::lastUniqueTargetReferenceCount()
                          << "candidatesAfterDedup=" << cad::viewer::SnapManager::lastCandidateCount()
                          << "buildMs=" << snapTimer.elapsed();
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
    if (transformCopyMode_) {
        transformPreviewObject_ = new AIS_Shape(transformOriginalShape_);
        context_->Display(transformPreviewObject_, Standard_False);
        context_->UpdateCurrentViewer();
    }
    transformDragging_ = true;
    if (transformGizmo_) transformGizmo_->setHovered(handle);
}

void CadViewer::invalidateSnapReferenceCache(const char* reason)
{
    Q_UNUSED(reason);
    if (bulkUpdateDepth_ > 0) {
        bulkCachesInvalidated_ = true;
        return;
    }
    snapReferenceCache_.clear();
    snapCandidateCache_.clear();
    transformSnapCandidates_.reset();
    snapScreenIndex_.reset();
    invalidateSnapProjectionCache(reason);
}

void CadViewer::invalidateSnapProjectionCache(const char* reason)
{
    Q_UNUSED(reason);
    snapScreenProjectionCache_.clear();
}

void CadViewer::updateTransformSnap(
    gp_Trsf& delta,
    gp_Pnt& pivot,
    const gp_Trsf& rawDelta)
{
    if (snapDisabled_) {
        if (transformGizmo_) transformGizmo_->setSnapActive(false);
        return;
    }
    Standard_Integer width = 0;
    Standard_Integer height = 0;
    view_->Window()->Size(width, height);
    const auto project = [this, width, height](const gp_Pnt& point) {
        return projectWorldPoint(view_, point, width, height);
    };
    activeSnap_ = snapManager_.findCandidate(
        *transformSnapCandidates_,
        delta,
        std::function<QPointF(const gp_Pnt&)>(project),
        activeSnap_,
        snapScreenIndex_.get()
    );
    if (activeSnap_) {
        const gp_Trsf correction = activeSnap_->correction;
        delta = correction;
        delta.Multiply(rawDelta);
        pivot = transformPivot_;
        pivot.Transform(delta);
        if (transformGizmo_) transformGizmo_->setSnapActive(true);
        if (transformGizmo_) {
            const auto markerKind = activeSnap_->kind == SnapKind::Endpoint
                ? cad::viewer::SnapMarkerKind::Endpoint
                : activeSnap_->kind == SnapKind::Midpoint
                    ? cad::viewer::SnapMarkerKind::Midpoint
                    : activeSnap_->kind == SnapKind::Intersection
                        ? cad::viewer::SnapMarkerKind::Intersection
                        : cad::viewer::SnapMarkerKind::Other;
            transformGizmo_->setSnapTarget(activeSnap_->targetPoint, view_, markerKind);
        }
    } else if (transformGizmo_) {
        transformGizmo_->setSnapActive(false);
        transformGizmo_->setSnapTarget(std::nullopt, view_);
    }
}

std::optional<gp_Pnt> CadViewer::worldAnchorAtScreenPoint(const QPoint& position) const
{
    if (!view_ || view_->Camera().IsNull()) return std::nullopt;

    ViewRay ray;
    if (!makeViewRay(position, ray)) return std::nullopt;

    const auto camera = view_->Camera();
    const gp_Pln anchorPlane(camera->Center(), camera->Direction());
    return cad::viewer::intersectRayWithPlane(
        ray.origin,
        ray.direction,
        anchorPlane
    );
}

void CadViewer::zoomAtCursor(const QPoint& position, const double factor)
{
    QElapsedTimer timer;
    timer.start();
    const auto pointBefore = worldAnchorAtScreenPoint(position);
    view_->SetZoom(factor);

    if (pointBefore) {
        const auto pointAfter = worldAnchorAtScreenPoint(position);
        if (pointAfter) {
            const gp_Vec correction = cad::viewer::zoomAnchorCorrection(
                *pointBefore,
                *pointAfter
            );
            const auto camera = view_->Camera();
            gp_Pnt eye = camera->Eye();
            gp_Pnt center = camera->Center();
            eye.Translate(correction);
            center.Translate(correction);
            camera->SetEyeAndCenter(eye, center);
        }
    }

    view_->Redraw();
    recordPerformanceSample("zoom redraw", timer.elapsed());
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
    const gp_Trsf rawDelta = delta;
    gp_Pnt movedPivot = transformPivot_;
    updateTransformSnap(delta, movedPivot, rawDelta);
    if (transformsClose(previousDelta, delta)) {
        return;
    }
    transformDelta_ = delta;
    const auto previewObject = transformCopyMode_
        ? transformPreviewObject_
        : transformObject_;
    if (!previewObject.IsNull()) {
        context_->SetLocation(previewObject, TopLoc_Location(delta));
    }
    context_->UpdateCurrentViewer();
}

void CadViewer::commitTransform()
{
    if (!transformDragging_) return;
    const QString id = transformFeatureId_;
    const gp_Trsf delta = transformDelta_;
    const bool copyMode = transformCopyMode_;
    transformDragging_ = false;
    transformHandle_ = TransformHandle::None;
    activeSnap_.reset();
    if (transformGizmo_) {
        transformGizmo_->setSnapActive(false);
        transformGizmo_->setSnapTarget(std::nullopt, view_);
    }
    if (!copyMode && !transformObject_.IsNull()) {
        context_->ResetLocation(transformObject_);
    }
    if (copyMode && !transformPreviewObject_.IsNull()) {
        context_->ResetLocation(transformPreviewObject_);
    }
    if (copyMode && transformCopyCommittedHandler_) {
        transformCopyCommittedHandler_(id, delta);
    } else if (delta.Form() != gp_Identity && transformCommittedHandler_) {
        transformCommittedHandler_(id, delta);
    } else if (!copyMode && !transformObject_.IsNull()) {
        context_->ResetLocation(transformObject_);
    }
    if (!transformPreviewObject_.IsNull()) {
        context_->Remove(transformPreviewObject_, Standard_False);
        transformPreviewObject_.Nullify();
    }
    transformObject_.Nullify();
    transformFeatureId_.clear();
    transformCopyMode_ = false;
    transformSourceReferences_.clear();
    transformTargetReferences_.clear();
    transformSnapCandidates_.reset();
    updateTransformGizmo();
}

void CadViewer::cancelTransform()
{
    if (!transformDragging_) return;
    if (!transformObject_.IsNull() && !transformCopyMode_) {
        context_->ResetLocation(transformObject_);
    }
    if (!transformPreviewObject_.IsNull()) {
        context_->Remove(transformPreviewObject_, Standard_False);
        transformPreviewObject_.Nullify();
    }
    transformDragging_ = false;
    transformHandle_ = TransformHandle::None;
    transformObject_.Nullify();
    transformCopyMode_ = false;
    transformFeatureId_.clear();
    transformSourceReferences_.clear();
    transformTargetReferences_.clear();
    transformSnapCandidates_.reset();
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

    if (performanceDiagnostics_) {
        const auto openGlContext = QOpenGLContext::currentContext();
        if (openGlContext) {
            auto* functions = openGlContext->functions();
            const auto stringValue = [functions](const GLenum value) {
                const auto* text = functions->glGetString(value);
                return text ? QString::fromLatin1(reinterpret_cast<const char*>(text))
                            : QStringLiteral("<unknown>");
            };
            qInfo().noquote() << "OpenGL renderer:" << stringValue(GL_VENDOR)
                              << "/" << stringValue(GL_RENDERER)
                              << "/" << stringValue(GL_VERSION);
        } else {
            qInfo() << "OpenGL renderer: unavailable at CadViewer initialization";
        }
    }

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
    selectionActivationDirty_ = true;
    if (bulkUpdateDepth_ == 0) applySelectionMode();
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
        invalidateSnapProjectionCache("CAMERA_CHANGED: viewport resized");
    }

    if (toolBar_ != nullptr) {
        toolBar_->move(8, 8);
        toolBar_->raise();
    }

    if (axisHoverActive_) {
        updateAxisHover(lastMousePosition_);
    }
}

void CadViewer::display(const TopoDS_Shape& shape, const QString& featureId, bool fitView)
{
    initializeOcc();

    Handle(AIS_Shape) interactiveShape =
        new AIS_Shape(shape);

    if (!shape.IsNull()) {
        if (featureId.startsWith("sketch-")) {
            configureSketchPresentation(interactiveShape, featureId);
        } else {
            applyDisplayMode(interactiveShape, shape);
        }
    }

    if (xRayEnabled_) {
        interactiveShape->SetTransparency(XRayTransparency);
    }

    traceSelectionLifecycle(QString("AIS register feature=%1 obj=%2 before Display")
        .arg(featureId).arg(selectionObjectPointer(interactiveShape)));
    // Preserve OCCT's established persistent-object display lifecycle.  The
    // selection manager immediately reconciles the resulting default mode
    // with the requested CAD mode.  Passing selection mode -1 here leaves a
    // native AIS_Shape in a different display/selection state and breaks
    // native decomposition picking on some OCCT viewer configurations.
    context_->Display(interactiveShape,
        bulkUpdateDepth_ == 0 ? Standard_True : Standard_False);
    traceSelectionLifecycle(QString("AIS register feature=%1 obj=%2 after Display")
        .arg(featureId).arg(selectionObjectPointer(interactiveShape)));
    traceSelectionLifecycle(QString("AIS state after Display feature=%1 obj=%2 displayed=%3 activeModes=%4")
        .arg(featureId).arg(selectionObjectPointer(interactiveShape))
        .arg(context_->IsDisplayed(interactiveShape) ? 1 : 0)
        .arg(activeSelectionModes(context_, interactiveShape)));
    displayedShapes_.push_back(interactiveShape);
    if (!featureId.isEmpty()) {
        featureObjects_[featureId] = interactiveShape;
        featureVisibility_[featureId] = VisibilityMode::Visible;
        setFeatureTransparency(featureId, interactiveShape);
        traceSelectionLifecycle(QString("registered selectable feature=%1 obj=%2 map=%3 dirty=%4")
            .arg(featureId).arg(selectionObjectPointer(interactiveShape))
            .arg(featureObjects_.size()).arg(selectionActivationDirty_));
    }
    invalidateSnapReferenceCache("MODEL_CHANGED: feature added");
    selectionState_.hovered.reset();
    selectionActivationDirty_ = true;

    if (bulkUpdateDepth_ == 0) applySelectionMode();
    if (fitView) {
        fitAll();
    }
}

void CadViewer::applyDisplayMode(const Handle(AIS_Shape)& object,
                                  const TopoDS_Shape& shape) const
{
    if (object.IsNull() || shape.IsNull()) return;
    if (displayMode_ == DisplayMode::Wireframe || shape.ShapeType() == TopAbs_WIRE) {
        object->SetDisplayMode(AIS_WireFrame);
        return;
    }
    object->SetDisplayMode(AIS_Shaded);
    const auto& drawer = object->Attributes();
    drawer->SetFaceBoundaryDraw(
        displayMode_ == DisplayMode::ShadedWithEdges ? Standard_True : Standard_False);
    drawer->SetFaceBoundaryAspect(
        new Prs3d_LineAspect(Quantity_NOC_BLACK, Aspect_TOL_SOLID, 1.2));
}

void CadViewer::setDisplayMode(const DisplayMode mode)
{
    displayMode_ = mode;
    for (const auto& [featureId, object] : featureObjects_) {
        Q_UNUSED(featureId);
        if (object.IsNull()) continue;
        applyDisplayMode(object, object->Shape());
        object->Redisplay(Standard_False);
    }
    if (initialized_ && !context_.IsNull()) context_->UpdateCurrentViewer();
}

CadViewer::DisplayMode CadViewer::displayMode() const noexcept
{
    return displayMode_;
}

void CadViewer::beginBulkUpdate()
{
    ++bulkUpdateDepth_;
}

void CadViewer::endBulkUpdate()
{
    if (bulkUpdateDepth_ == 0) return;
    --bulkUpdateDepth_;
    if (bulkUpdateDepth_ == 0 && initialized_ && !context_.IsNull()) {
        if (bulkCachesInvalidated_) {
            snapReferenceCache_.clear();
            snapCandidateCache_.clear();
            transformSnapCandidates_.reset();
            snapScreenIndex_.reset();
            snapScreenProjectionCache_.clear();
            bulkCachesInvalidated_ = false;
        }
        QElapsedTimer timer;
        timer.start();
        applySelectionMode();
        lastBulkViewerUpdateMilliseconds_ = timer.elapsed();
        if (performanceDiagnostics_) {
            std::size_t visible = 0;
            std::size_t selectable = 0;
            for (const auto& [id, object] : featureObjects_) {
                if (!object.IsNull() && context_->IsDisplayed(object)) ++visible;
                if (featureIsSelectable(id)) ++selectable;
            }
            qInfo().noquote() << "CadViewer bulk refresh: features=" << featureObjects_.size()
                              << "visible=" << visible << "AIS=" << featureObjects_.size()
                              << "selectable=" << selectable
                              << "selectionMs=" << lastSelectionActivationMilliseconds_
                              << "viewerMs=" << lastViewerUpdateMilliseconds_
                              << "bulkMs=" << lastBulkViewerUpdateMilliseconds_;
        }
    }
}

std::int64_t CadViewer::lastBulkViewerUpdateMilliseconds() const noexcept
{
    return lastBulkViewerUpdateMilliseconds_;
}

std::int64_t CadViewer::lastViewerUpdateMilliseconds() const noexcept
{
    return lastViewerUpdateMilliseconds_;
}

std::int64_t CadViewer::lastSelectionActivationMilliseconds() const noexcept
{
    return lastSelectionActivationMilliseconds_;
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
    configureSketchPresentation(object, featureId);
    setFeatureTransparency(featureId, object);
    if (object->Shape().IsEqual(shape)) {
        if (featureId.startsWith("sketch-")) {
            context_->Redisplay(object, bulkUpdateDepth_ == 0 ? Standard_True : Standard_False);
        }
        return;
    }

    cancelPushPull();
    resetDetectedCycle();
    invalidateSnapReferenceCache("MODEL_CHANGED: feature shape updated");
    selectionState_.hovered.reset();
    traceSelectionLifecycle(QString("AIS replace feature=%1 obj=%2 before SetShape")
        .arg(featureId).arg(selectionObjectPointer(object)));
    updateManagedShape(featureId, object, shape);
    traceSelectionLifecycle(QString("AIS replace feature=%1 obj=%2 after Redisplay")
        .arg(featureId).arg(selectionObjectPointer(object)));
    if (bulkUpdateDepth_ == 0) syncSelectionStateFromOcct();
}

void CadViewer::updateSketchConstructionGeometry(
    const cad::parametric::SketchFeature& sketch, const QString& featureId)
{
    if (!initialized_ || featureId.isEmpty()) return;
    const auto existing = sketchConstructionObjects_.find(featureId);
    if (existing != sketchConstructionObjects_.end()) {
        context_->Remove(existing->second, Standard_False);
        sketchConstructionObjects_.erase(existing);
    }

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    const auto frame = sketch.currentFrame();
    bool hasConstruction = false;
    for (const auto& entity : sketch.entities()) {
        const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
        if (!line || !line->construction) continue;
        auto edge = BRepBuilderAPI_MakeEdge(
            sketchWorldPoint(frame, line->start), sketchWorldPoint(frame, line->end));
        if (!edge.IsDone()) continue;
        builder.Add(compound, edge.Edge());
        hasConstruction = true;
    }
    if (!hasConstruction) return;

    auto object = Handle(AIS_Shape)(new AIS_Shape(compound));
    object->SetDisplayMode(AIS_WireFrame);
    object->SetColor(Quantity_NOC_GRAY70);
    object->SetWidth(1.5);
    sketchConstructionObjects_.emplace(featureId, object);
    context_->Display(object, Standard_False);
    if (featureVisibilityMode(featureId) == VisibilityMode::Hidden) {
        context_->Erase(object, Standard_False);
    }
}

void CadViewer::setHiddenFeatures(const QStringList& featureIds)
{
    std::map<QString, VisibilityMode> modes;
    for (const auto& [id, object] : featureObjects_) {
        Q_UNUSED(object);
        modes.emplace(id, featureIds.contains(id)
            ? VisibilityMode::Hidden : VisibilityMode::Visible);
    }
    setFeatureVisibilityModes(modes);
}

void CadViewer::setFeatureVisibilityModes(
    const std::map<QString, VisibilityMode>& modes)
{
    if (!initialized_) return;
    bool changed = false;
    bool selectedFeatureUnavailable = false;
    for (const auto& [id, object] : featureObjects_) {
        const auto mode = [&]() {
            const auto found = modes.find(id);
            return found == modes.end() ? VisibilityMode::Visible : found->second;
        }();
        const auto previous = featureVisibilityMode(id);
        const bool modeChanged = previous != mode;
        if (mode != VisibilityMode::Visible) releaseManagedSelection(id);
        featureVisibility_[id] = mode;
        const bool displayed = bool(context_->IsDisplayed(object));
        if (mode == VisibilityMode::Hidden) {
            if (displayed) {
                context_->Erase(object, Standard_False);
                changed = true;
            }
        } else {
            if (!displayed) {
                context_->Display(object, Standard_False);
                changed = true;
            }
            if (modeChanged) {
                setFeatureTransparency(id, object);
                context_->Redisplay(object, Standard_False);
            }
        }
        if (modeChanged) {
            changed = true;
            if (mode != VisibilityMode::Visible
                && std::any_of(selectionState_.selected.begin(), selectionState_.selected.end(),
                    [&id](const auto& item) { return item.featureId == id; })) {
                selectedFeatureUnavailable = true;
            }
        }
    }
    if (!changed) return;

    selectionActivationDirty_ = true;
    if (selectedFeatureUnavailable) {
        if (transformDragging_) cancelTransform();
        if (pushPullActive_) cancelPushPull();
        traceSelectionLifecycle("ClearSelected BEFORE (visibility change)");
        context_->ClearSelected(Standard_False);
        traceSelectionLifecycle("ClearSelected AFTER (visibility change)");
    }
    invalidateSnapReferenceCache("MODEL_CHANGED: presentation visibility changed");
    resetDetectedCycle();
    selectionState_.hovered.reset();
    if (bulkUpdateDepth_ == 0) {
        applySelectionMode();
        syncSelectionStateFromOcct();
    }
    updateTransformGizmo();
    if (bulkUpdateDepth_ == 0) context_->UpdateCurrentViewer();
}

void CadViewer::applyVisibilityChanges(
    const std::vector<cad::application::VisibilityChange>& changes)
{
    if (!initialized_ || changes.empty()) return;
    bool changed = false;
    bool selectedFeatureUnavailable = false;
    for (const auto& change : changes) {
        const QString id = QString::fromStdString(change.featureId);
        const auto object = featureObjects_.find(id);
        if (object == featureObjects_.end()) continue;

        const auto previous = featureVisibilityMode(id);
        if (previous == change.newMode) continue;
        if (change.newMode != VisibilityMode::Visible) releaseManagedSelection(id);
        featureVisibility_[id] = change.newMode;
        const bool displayed = bool(context_->IsDisplayed(object->second));
        if (change.newMode == VisibilityMode::Hidden) {
            if (displayed) context_->Erase(object->second, Standard_False);
        } else {
            if (!displayed) context_->Display(object->second, Standard_False);
            setFeatureTransparency(id, object->second);
            context_->Redisplay(object->second, Standard_False);
        }
        const auto construction = sketchConstructionObjects_.find(id);
        if (construction != sketchConstructionObjects_.end()) {
            if (change.newMode == VisibilityMode::Hidden) {
                context_->Erase(construction->second, Standard_False);
            } else {
                context_->Display(construction->second, Standard_False);
            }
        }
        changed = true;
        if (change.newMode != VisibilityMode::Visible
            && std::any_of(selectionState_.selected.begin(), selectionState_.selected.end(),
                [&id](const auto& item) { return item.featureId == id; })) {
            selectedFeatureUnavailable = true;
        }
    }
    if (!changed) return;

    selectionActivationDirty_ = true;
    if (selectedFeatureUnavailable) {
        if (transformDragging_) cancelTransform();
        if (pushPullActive_) cancelPushPull();
        traceSelectionLifecycle("ClearSelected BEFORE (visibility change)");
        context_->ClearSelected(Standard_False);
        traceSelectionLifecycle("ClearSelected AFTER (visibility change)");
    }
    // Snap references are still invalidated globally in Stage 7. This keeps
    // candidate caches correct while the viewer presentation update is local.
    invalidateSnapReferenceCache("MODEL_CHANGED: presentation visibility changed");
    resetDetectedCycle();
    selectionState_.hovered.reset();
    if (bulkUpdateDepth_ == 0) {
        applySelectionMode();
        syncSelectionStateFromOcct();
    }
    updateTransformGizmo();
    if (bulkUpdateDepth_ == 0) context_->UpdateCurrentViewer();
}

void CadViewer::setSpatialBox(const gp_Pnt& min, const gp_Pnt& max)
{
    if (!initialized_ || context_.IsNull()) return;
    const gp_Vec diagonal(min, max);
    if (diagonal.X() <= 0.0 || diagonal.Y() <= 0.0 || diagonal.Z() <= 0.0) return;
    spatialBoxMin_ = min;
    spatialBoxMax_ = max;
    const auto shape = BRepPrimAPI_MakeBox(min, max).Shape();
    if (spatialBoxObject_.IsNull()) {
        spatialBoxObject_ = new AIS_Shape(shape);
        spatialBoxObject_->SetDisplayMode(AIS_WireFrame);
        spatialBoxObject_->SetColor(Quantity_NOC_CYAN1);
        spatialBoxObject_->SetTransparency(0.8);
        context_->Display(spatialBoxObject_, Standard_False);
        context_->Deactivate(spatialBoxObject_);
    } else {
        spatialBoxObject_->SetShape(shape);
        context_->Redisplay(spatialBoxObject_, Standard_False);
    }
    if (bulkUpdateDepth_ == 0) context_->UpdateCurrentViewer();
}

void CadViewer::setSpatialBoxChangedHandler(
    std::function<void(const gp_Pnt&, const gp_Pnt&)> handler)
{
    spatialBoxChangedHandler_ = std::move(handler);
}

void CadViewer::clearSpatialBox()
{
    if (spatialBoxObject_.IsNull() || context_.IsNull()) return;
    context_->Remove(spatialBoxObject_, Standard_False);
    spatialBoxObject_.Nullify();
    spatialBoxMin_ = gp_Pnt();
    spatialBoxMax_ = gp_Pnt();
    spatialBoxHandle_ = -1;
    if (bulkUpdateDepth_ == 0) context_->UpdateCurrentViewer();
}

void CadViewer::activateSection(const SectionAxis axis)
{
    if (!initialized_ || context_.IsNull() || view_.IsNull()) return;
    Bnd_Box bounds;
    for (const auto& [id, object] : featureObjects_) {
        Q_UNUSED(id);
        if (!object.IsNull()) BRepBndLib::Add(object->Shape(), bounds);
    }
    if (bounds.IsVoid()) return;
    sectionBounds_ = bounds;
    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const gp_Pnt center((xmin + xmax) * 0.5, (ymin + ymax) * 0.5, (zmin + zmax) * 0.5);
    sectionState_.active = true;
    sectionState_.axis = axis;
    sectionState_.flipped = false;
    sectionState_.origin = center;
    sectionDragPosition_ = axis == SectionAxis::X ? center.X()
        : axis == SectionAxis::Y ? center.Y() : center.Z();
    if (sectionClipPlane_.IsNull()) sectionClipPlane_ = new Graphic3d_ClipPlane();
    view_->AddClipPlane(sectionClipPlane_);
    updateSectionPresentation();
}

void CadViewer::flipSection()
{
    if (!sectionState_.active) return;
    sectionState_.flipped = !sectionState_.flipped;
    updateSectionPresentation();
}

void CadViewer::clearSection()
{
    if (!sectionClipPlane_.IsNull() && !view_.IsNull())
        view_->RemoveClipPlane(sectionClipPlane_);
    if (!sectionPlaneObject_.IsNull() && !context_.IsNull())
        context_->Remove(sectionPlaneObject_, Standard_False);
    if (!sectionHandleObject_.IsNull() && !context_.IsNull())
        context_->Remove(sectionHandleObject_, Standard_False);
    if (!sectionNormalObject_.IsNull() && !context_.IsNull())
        context_->Remove(sectionNormalObject_, Standard_False);
    sectionClipPlane_.Nullify();
    sectionPlaneObject_.Nullify();
    sectionHandleObject_.Nullify();
    sectionNormalObject_.Nullify();
    sectionState_ = {};
    sectionBounds_.SetVoid();
    sectionDragging_ = false;
    sectionHandleHovered_ = false;
    unsetCursor();
    setToolTip({});
    if (!view_.IsNull() && bulkUpdateDepth_ == 0) view_->Redraw();
}

void CadViewer::setSectionInteractionStatusHandler(
    std::function<void(const QString&)> handler)
{
    sectionInteractionStatusHandler_ = std::move(handler);
}

const CadViewer::SectionState& CadViewer::sectionState() const noexcept
{
    return sectionState_;
}

bool CadViewer::restoreSection(
    const SectionAxis axis, const gp_Pnt& origin, const bool flipped)
{
    activateSection(axis);
    if (!sectionState_.active) return false;
    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    sectionBounds_.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double lower = axis == SectionAxis::X ? xmin : axis == SectionAxis::Y ? ymin : zmin;
    const double upper = axis == SectionAxis::X ? xmax : axis == SectionAxis::Y ? ymax : zmax;
    const double requested = axis == SectionAxis::X ? origin.X()
        : axis == SectionAxis::Y ? origin.Y() : origin.Z();
    sectionDragPosition_ = std::clamp(requested, lower, upper);
    if (axis == SectionAxis::X) sectionState_.origin.SetX(sectionDragPosition_);
    else if (axis == SectionAxis::Y) sectionState_.origin.SetY(sectionDragPosition_);
    else sectionState_.origin.SetZ(sectionDragPosition_);
    sectionState_.flipped = flipped;
    updateSectionPresentation();
    return true;
}

CadViewer::CameraState CadViewer::cameraState() const
{
    CameraState state;
    if (!view_.IsNull() && !view_->Camera().IsNull()) {
        const auto camera = view_->Camera();
        state.valid = true;
        state.eye = camera->Eye();
        state.center = camera->Center();
        state.up = camera->Up();
        state.scale = camera->Scale();
    }
    return state;
}

void CadViewer::restoreCamera(const CameraState& state)
{
    if (!state.valid || view_.IsNull() || view_->Camera().IsNull()) return;
    const auto camera = view_->Camera();
    camera->SetEyeAndCenter(state.eye, state.center);
    camera->SetUp(state.up);
    camera->SetScale(std::max(state.scale, 1.0e-9));
    updateOrbitStateFromCamera();
    invalidateSnapProjectionCache("CAMERA_CHANGED: saved view restored");
    view_->Redraw();
}

void CadViewer::updateSectionPresentation()
{
    if (!sectionState_.active || sectionClipPlane_.IsNull() || view_.IsNull()) return;
    gp_Dir normal;
    if (sectionState_.axis == SectionAxis::X) normal = gp_Dir(1, 0, 0);
    else if (sectionState_.axis == SectionAxis::Y) normal = gp_Dir(0, 1, 0);
    else normal = gp_Dir(0, 0, 1);
    if (sectionState_.flipped) normal.Reverse();
    sectionClipPlane_->SetEquation(gp_Pln(sectionState_.origin, normal));
    sectionClipPlane_->SetOn(true);
    const auto shape = makeSectionGrid(
        sectionState_.axis, sectionDragPosition_, sectionBounds_);
    if (sectionPlaneObject_.IsNull()) {
        sectionPlaneObject_ = new AIS_Shape(shape);
        sectionPlaneObject_->SetDisplayMode(AIS_WireFrame);
        sectionPlaneObject_->SetColor(Quantity_NOC_ORANGE);
        sectionPlaneObject_->SetWidth(2.0);
        context_->Display(sectionPlaneObject_, Standard_False);
        context_->Deactivate(sectionPlaneObject_);
    } else {
        sectionPlaneObject_->SetShape(shape);
        context_->Redisplay(sectionPlaneObject_, Standard_False);
    }
    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    sectionBounds_.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double handleRadius = std::max({xmax - xmin, ymax - ymin, zmax - zmin}) * 0.045;
    const auto handleShape = BRepPrimAPI_MakeSphere(
        sectionState_.origin, std::max(handleRadius, 0.05)).Shape();
    if (sectionHandleObject_.IsNull()) {
        sectionHandleObject_ = new AIS_Shape(handleShape);
        context_->Display(sectionHandleObject_, Standard_False);
        context_->Deactivate(sectionHandleObject_);
    } else {
        sectionHandleObject_->SetShape(handleShape);
        context_->Redisplay(sectionHandleObject_, Standard_False);
    }
    sectionHandleObject_->SetColor(sectionHandleHovered_
        ? Quantity_NOC_YELLOW : Quantity_NOC_ORANGE);
    sectionHandleObject_->SetTransparency(0.1);
    const auto normalShape = makeSectionNormalIndicator(
        sectionState_.axis, sectionState_.origin, sectionBounds_, sectionState_.flipped);
    if (sectionNormalObject_.IsNull()) {
        sectionNormalObject_ = new AIS_Shape(normalShape);
        sectionNormalObject_->SetDisplayMode(AIS_WireFrame);
        sectionNormalObject_->SetColor(Quantity_NOC_YELLOW);
        sectionNormalObject_->SetWidth(3.0);
        context_->Display(sectionNormalObject_, Standard_False);
        context_->Deactivate(sectionNormalObject_);
    } else {
        sectionNormalObject_->SetShape(normalShape);
        context_->Redisplay(sectionNormalObject_, Standard_False);
    }
    if (bulkUpdateDepth_ == 0) view_->Redraw();
}

int CadViewer::sectionHandleAt(const QPoint& position) const
{
    if (!sectionState_.active || view_.IsNull()) return -1;
    const auto isNear = [&position](const QPointF& screen, const double radius) {
        const auto dx = screen.x() - position.x();
        const auto dy = screen.y() - position.y();
        return dx * dx + dy * dy <= radius * radius;
    };
    if (isNear(projectWorldPoint(view_, sectionState_.origin, width(), height()), 32.0)) return 0;
    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    sectionBounds_.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double length = std::max({xmax - xmin, ymax - ymin, zmax - zmin}) * 0.35;
    gp_Vec direction(sectionState_.axis == SectionAxis::X ? gp_Vec(1, 0, 0)
        : sectionState_.axis == SectionAxis::Y ? gp_Vec(0, 1, 0) : gp_Vec(0, 0, 1));
    if (sectionState_.flipped) direction.Reverse();
    direction *= std::max(length, 1.0e-3);
    return isNear(projectWorldPoint(view_, sectionState_.origin.Translated(direction), width(), height()), 28.0)
        ? 0 : -1;
}

void CadViewer::updateSectionDrag(const QPoint& position)
{
    if (!sectionDragging_ || !sectionState_.active || view_.IsNull()) return;
    const auto originScreen = projectWorldPoint(view_, sectionState_.origin, width(), height());
    gp_Pnt axisEnd = sectionState_.origin;
    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    sectionBounds_.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double lower = sectionState_.axis == SectionAxis::X ? xmin
        : sectionState_.axis == SectionAxis::Y ? ymin : zmin;
    const double upper = sectionState_.axis == SectionAxis::X ? xmax
        : sectionState_.axis == SectionAxis::Y ? ymax : zmax;
    const double length = upper - lower;
    if (sectionState_.axis == SectionAxis::X) axisEnd.SetX(sectionState_.origin.X() + length * 0.5);
    else if (sectionState_.axis == SectionAxis::Y) axisEnd.SetY(sectionState_.origin.Y() + length * 0.5);
    else axisEnd.SetZ(sectionState_.origin.Z() + length * 0.5);
    const auto axisScreen = projectWorldPoint(view_, axisEnd, width(), height()) - originScreen;
    const double axisPixels = QPointF::dotProduct(axisScreen, axisScreen);
    if (axisPixels <= 1.0e-9) return;
    const QPointF mouseDelta = QPointF(position - sectionDragStart_);
    const double delta = QPointF::dotProduct(mouseDelta, axisScreen) / axisPixels * length;
    const double value = std::clamp(sectionDragPosition_ + delta, lower, upper);
    sectionDragPosition_ = value;
    if (sectionState_.axis == SectionAxis::X) sectionState_.origin.SetX(value);
    else if (sectionState_.axis == SectionAxis::Y) sectionState_.origin.SetY(value);
    else sectionState_.origin.SetZ(value);
    updateSectionPresentation();
}

int CadViewer::spatialBoxHandleAt(const QPoint& position) const
{
    if (spatialBoxObject_.IsNull() || view_.IsNull()) return -1;
    const gp_Pnt center(
        (spatialBoxMin_.X() + spatialBoxMax_.X()) * 0.5,
        (spatialBoxMin_.Y() + spatialBoxMax_.Y()) * 0.5,
        (spatialBoxMin_.Z() + spatialBoxMax_.Z()) * 0.5);
    const std::array<gp_Pnt, 6> faces{
        gp_Pnt(spatialBoxMin_.X(), center.Y(), center.Z()),
        gp_Pnt(spatialBoxMax_.X(), center.Y(), center.Z()),
        gp_Pnt(center.X(), spatialBoxMin_.Y(), center.Z()),
        gp_Pnt(center.X(), spatialBoxMax_.Y(), center.Z()),
        gp_Pnt(center.X(), center.Y(), spatialBoxMin_.Z()),
        gp_Pnt(center.X(), center.Y(), spatialBoxMax_.Z())};
    int result = -1;
    double best = 18.0 * 18.0;
    for (int index = 0; index < static_cast<int>(faces.size()); ++index) {
        const auto screen = projectWorldPoint(view_, faces[index], width(), height());
        const auto dx = screen.x() - position.x();
        const auto dy = screen.y() - position.y();
        const auto distance = dx * dx + dy * dy;
        if (distance < best) {
            best = distance;
            result = index;
        }
    }
    return result;
}

void CadViewer::updateSpatialBoxDrag(const QPoint& position)
{
    if (spatialBoxHandle_ < 0 || view_.IsNull()) return;
    const gp_Pnt center(
        (spatialBoxDragMin_.X() + spatialBoxDragMax_.X()) * 0.5,
        (spatialBoxDragMin_.Y() + spatialBoxDragMax_.Y()) * 0.5,
        (spatialBoxDragMin_.Z() + spatialBoxDragMax_.Z()) * 0.5);
    const int axis = spatialBoxHandle_ / 2;
    const bool minimum = (spatialBoxHandle_ % 2) == 0;
    const double lower = axis == 0 ? spatialBoxDragMin_.X()
        : axis == 1 ? spatialBoxDragMin_.Y() : spatialBoxDragMin_.Z();
    const double upper = axis == 0 ? spatialBoxDragMax_.X()
        : axis == 1 ? spatialBoxDragMax_.Y() : spatialBoxDragMax_.Z();
    const double length = upper - lower;
    if (length <= 1.0e-9) return;
    gp_Pnt axisEnd = center;
    if (axis == 0) axisEnd.SetX(center.X() + length * 0.5);
    if (axis == 1) axisEnd.SetY(center.Y() + length * 0.5);
    if (axis == 2) axisEnd.SetZ(center.Z() + length * 0.5);
    const auto startScreen = projectWorldPoint(
        view_, spatialBoxHandle_ % 2 == 0
            ? (axis == 0 ? gp_Pnt(lower, center.Y(), center.Z())
                : axis == 1 ? gp_Pnt(center.X(), lower, center.Z())
                            : gp_Pnt(center.X(), center.Y(), lower))
            : (axis == 0 ? gp_Pnt(upper, center.Y(), center.Z())
                : axis == 1 ? gp_Pnt(center.X(), upper, center.Z())
                            : gp_Pnt(center.X(), center.Y(), upper)), width(), height());
    const auto endScreen = projectWorldPoint(view_, axisEnd, width(), height());
    const QPointF screenAxis = endScreen - startScreen;
    const double screenLengthSquared = QPointF::dotProduct(screenAxis, screenAxis);
    if (screenLengthSquared <= 1.0e-9) return;
    const QPointF mouseDelta = QPointF(position - spatialBoxDragStart_);
    const double worldDelta = QPointF::dotProduct(mouseDelta, screenAxis)
        / screenLengthSquared * length;
    const double value = minimum ? std::min(lower + worldDelta, upper - 1.0e-6)
                                 : std::max(upper + worldDelta, lower + 1.0e-6);
    gp_Pnt min = spatialBoxDragMin_;
    gp_Pnt max = spatialBoxDragMax_;
    if (axis == 0) minimum ? min.SetX(value) : max.SetX(value);
    if (axis == 1) minimum ? min.SetY(value) : max.SetY(value);
    if (axis == 2) minimum ? min.SetZ(value) : max.SetZ(value);
    setSpatialBox(min, max);
    if (spatialBoxChangedHandler_) spatialBoxChangedHandler_(min, max);
}

cad::application::VisibilityMode CadViewer::featureVisibilityMode(
    const QString& featureId) const noexcept
{
    const auto found = featureVisibility_.find(featureId);
    return found == featureVisibility_.end()
        ? VisibilityMode::Visible : found->second;
}

void CadViewer::recordPerformanceSample(
    const char* operation, const std::int64_t milliseconds)
{
    if (!performanceDiagnostics_) return;
    auto& metric = performanceMetrics_[QString::fromLatin1(operation)];
    ++metric.count;
    metric.totalMilliseconds += milliseconds;
    metric.maximumMilliseconds = std::max(metric.maximumMilliseconds, milliseconds);
    if (metric.count == 1 || metric.count % 120 == 0) {
        qInfo().noquote() << "CadViewer performance" << operation
                          << "samples=" << metric.count
                          << "avgMs=" << (static_cast<double>(metric.totalMilliseconds)
                              / static_cast<double>(metric.count))
                          << "maxMs=" << metric.maximumMilliseconds;
    }
}

bool CadViewer::featureIsSelectable(const QString& featureId) const noexcept
{
    return !selectionDisabled_
        && featureId != editingSketchFeatureId_
        && featureVisibilityMode(featureId) == VisibilityMode::Visible;
}

void CadViewer::releaseManagedSelection(const QString& featureId)
{
    const auto active = managedSelectionModes_.find(featureId);
    if (active == managedSelectionModes_.end()) return;
    const auto found = featureObjects_.find(featureId);
    if (found != featureObjects_.end() && !found->second.IsNull()) {
        traceSelectionLifecycle(QString("release managed BEFORE feature=%1 obj=%2 mode=%3")
            .arg(featureId).arg(selectionObjectPointer(found->second)).arg(active->second));
        context_->Deactivate(found->second, active->second);
        traceSelectionLifecycle(QString("release managed AFTER feature=%1").arg(featureId));
    }
    managedSelectionModes_.erase(active);
    selectionActivationDirty_ = true;
}

void CadViewer::updateManagedShape(const QString& featureId,
    const Handle(AIS_Shape)& object, const TopoDS_Shape& shape)
{
    // Release selection while the old shape is still intact. The cache must
    // never describe selection structures belonging to a previous shape.
    releaseManagedSelection(featureId);
    object->SetShape(shape);
    context_->Redisplay(object, Standard_False);
    selectionActivationDirty_ = true;
    if (bulkUpdateDepth_ == 0) applySelectionMode();
}

void CadViewer::applyFeaturePresentation(
    const QString& featureId, const Handle(AIS_Shape)& object)
{
    if (object.IsNull()) return;
    if (featureVisibilityMode(featureId) == VisibilityMode::Hidden) {
        context_->Erase(object, Standard_False);
        return;
    }
    context_->Display(object, Standard_False);
    setFeatureTransparency(featureId, object);
    context_->Redisplay(object, Standard_False);
}

void CadViewer::setFeatureTransparency(
    const QString& featureId, const Handle(AIS_Shape)& object)
{
    if (object.IsNull()) return;
    if (featureVisibilityMode(featureId) == VisibilityMode::Ghosted) {
        object->SetTransparency(GhostTransparency);
    } else if (xRayEnabled_) {
        object->SetTransparency(XRayTransparency);
    } else {
        object->UnsetTransparency();
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
        managedSelectionModes_.erase(it->first);
        traceSelectionLifecycle(QString("AIS unregister feature=%1 obj=%2 before Remove")
            .arg(it->first).arg(selectionObjectPointer(it->second)));
        context_->Remove(it->second, Standard_False);
        traceSelectionLifecycle(QString("AIS unregister feature=%1 after Remove").arg(it->first));
        std::erase(displayedShapes_, it->second);
        const auto construction = sketchConstructionObjects_.find(it->first);
        if (construction != sketchConstructionObjects_.end()) {
            context_->Remove(construction->second, Standard_False);
            sketchConstructionObjects_.erase(construction);
        }
        featureVisibility_.erase(it->first);
        it = featureObjects_.erase(it);
        changed = true;
    }
    if (changed) {
        selectionActivationDirty_ = true;
        invalidateSnapReferenceCache("MODEL_CHANGED: features retained");
        resetDetectedCycle();
        selectionState_.hovered.reset();
        if (bulkUpdateDepth_ == 0) syncSelectionStateFromOcct();
        if (bulkUpdateDepth_ == 0) context_->UpdateCurrentViewer();
    }
}

void CadViewer::clear()
{
    if (!initialized_) {
        return;
    }

    cancelPushPull();
    clearSection();
    clearSketchLinePickTarget();
    if (!sketchPreviewObject_.IsNull()) {
        context_->Remove(sketchPreviewObject_, Standard_False);
        sketchPreviewObject_.Nullify();
    }
    sketchPreviewFirstPoint_.reset();
    context_->RemoveAll(Standard_True);
    spatialBoxObject_.Nullify();
    displayedShapes_.clear();
    featureObjects_.clear();
    sketchConstructionObjects_.clear();
    managedSelectionModes_.clear();
    editingSketchFeatureId_.clear();
    featureVisibility_.clear();
    selectionActivationDirty_ = true;
    invalidateSnapReferenceCache("MODEL_CHANGED: viewer cleared");
    resetDetectedCycle();
    selectionState_ = {};
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

    QElapsedTimer timer;
    timer.start();
    view_->FitAll();
    view_->ZFitAll();
    view_->Redraw();
    recordPerformanceSample("FitAll", timer.elapsed());
}

void CadViewer::setSelectionMode(SelectionMode mode)
{
    const auto oldMode = selectionMode_;
    traceSelectionLifecycle(QString("setSelectionMode begin old=%1 new=%2 dirty=%3 managed=%4")
        .arg(selectionModeName(oldMode))
        .arg(selectionModeName(mode))
        .arg(selectionActivationDirty_)
        .arg(managedSelectionModes_.size()));
    if (pushPullActive_) {
        cancelPushPull();
    }

    pushPullArmed_ = false;
    unsetCursor();
    selectionMode_ = mode;
    selectionActivationDirty_ = true;

    if (initialized_) {
        clearSelection();
        applySelectionMode();
    }

    syncToolBarState();
    traceSelectionLifecycle(QString("setSelectionMode end old=%1 new=%2 dirty=%3 managed=%4")
        .arg(selectionModeName(oldMode))
        .arg(selectionModeName(selectionMode_))
        .arg(selectionActivationDirty_)
        .arg(managedSelectionModes_.size()));
}

void CadViewer::setAxisPickCancelHandler(std::function<void()> handler)
{
    axisPickCancelHandler_ = std::move(handler);
}

void CadViewer::setSketchLinePickTarget(
    std::shared_ptr<const cad::parametric::SketchFeature> sketch,
    std::function<void(const cad::parametric::SketchEntityId&)> pickedHandler,
    std::function<void(const std::optional<cad::parametric::SketchEntityId>&)> hoveredHandler)
{
    if (qEnvironmentVariableIsSet("PARAMETRICCAD_TRACE_ACTIONS")) {
        std::fprintf(stderr, "[REV] beginSketchLinePick source=%s entities=%zu\n",
            sketch ? sketch->id().c_str() : "<null>",
            sketch ? sketch->entities().size() : 0U);
        std::fflush(stderr);
    }
    sketchLinePickSketch_ = std::move(sketch);
    sketchLinePickedHandler_ = std::move(pickedHandler);
    sketchLineHoveredHandler_ = std::move(hoveredHandler);
    sketchLineHoveredId_.reset();
    sketchLinePickMousePress_ = false;
    updateSketchLinePickHover(lastMousePosition_);
}

void CadViewer::clearSketchLinePickTarget()
{
    if (qEnvironmentVariableIsSet("PARAMETRICCAD_TRACE_ACTIONS")) {
        std::fprintf(stderr, "[REV] endSketchLinePick\n");
        std::fflush(stderr);
    }
    sketchLinePickSketch_.reset();
    sketchLinePickedHandler_ = {};
    sketchLineHoveredHandler_ = {};
    sketchLineHoveredId_.reset();
    sketchLinePickMousePress_ = false;
    if (!sketchLinePickHighlightObject_.IsNull() && !context_.IsNull()) {
        context_->Remove(sketchLinePickHighlightObject_, Standard_True);
    }
    sketchLinePickHighlightObject_.Nullify();
}

std::optional<cad::parametric::SketchEntityId> CadViewer::sketchLineAtScreen(
    const QPoint& position) const
{
    if (!sketchLinePickSketch_ || !view_ || width() <= 0 || height() <= 0) {
        return std::nullopt;
    }
    const auto frame = sketchLinePickSketch_->currentFrame();
    std::vector<cad::viewer::SketchLineScreenCandidate> candidates;
    for (const auto& entity : sketchLinePickSketch_->entities()) {
        const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
        if (!line) continue;
        const gp_Pnt start = sketchWorldPoint(frame, line->start);
        const gp_Pnt end = sketchWorldPoint(frame, line->end);
        candidates.push_back({line->id,
            projectWorldPoint(view_, start, width(), height()),
            projectWorldPoint(view_, end, width(), height()), line->construction});
    }
    return cad::viewer::pickSketchLine(candidates, QPointF(position), SketchTrimHitPixels);
}

void CadViewer::updateSketchLinePickHover(const QPoint& position)
{
    if (!sketchLinePickSketch_) return;
    std::optional<cad::parametric::SketchEntityId> id;
    try {
        id = sketchLineAtScreen(position);
    } catch (const std::exception&) {
        id.reset();
    }
    if (id == sketchLineHoveredId_) return;
    sketchLineHoveredId_ = id;
    if (qEnvironmentVariableIsSet("PARAMETRICCAD_TRACE_ACTIONS")) {
        const auto text = id ? QString::fromStdString(*id) : QStringLiteral("<none>");
        std::fprintf(stderr, "[REV] hover position=%d,%d entity=%s\n",
            position.x(), position.y(), text.toUtf8().constData());
        std::fflush(stderr);
    }
    if (sketchLineHoveredHandler_) sketchLineHoveredHandler_(id);

    if (!context_ || !view_) return;
    if (!sketchLinePickHighlightObject_.IsNull()) {
        context_->Remove(sketchLinePickHighlightObject_, Standard_True);
        sketchLinePickHighlightObject_.Nullify();
    }
    if (!id) return;
    const auto frame = sketchLinePickSketch_->currentFrame();
    for (const auto& entity : sketchLinePickSketch_->entities()) {
        const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
        if (!line || line->id != *id) continue;
        const auto shape = BRepBuilderAPI_MakeEdge(
            sketchWorldPoint(frame, line->start), sketchWorldPoint(frame, line->end)).Edge();
        sketchLinePickHighlightObject_ = new AIS_Shape(shape);
        sketchLinePickHighlightObject_->SetDisplayMode(AIS_WireFrame);
        sketchLinePickHighlightObject_->SetColor(Quantity_NOC_YELLOW);
        sketchLinePickHighlightObject_->SetWidth(4.0);
        context_->Display(sketchLinePickHighlightObject_, Standard_True);
        break;
    }
}

CadViewer::SelectionMode CadViewer::selectionMode() const
{
    return selectionMode_;
}

void CadViewer::setOrbitMode(const OrbitMode mode)
{
    if (orbitMode_ == mode) return;

    if (mode == OrbitMode::Turntable) {
        updateOrbitStateFromCamera();
        applyTurntableCamera();
        invalidateSnapProjectionCache("CAMERA_CHANGED: locked-Z orbit mode");
        if (view_) view_->Redraw();
        if (transformMode_) updateTransformGizmo();
    }

    orbitMode_ = mode;
    syncToolBarState();
}

CadViewer::OrbitMode CadViewer::orbitMode() const noexcept
{
    return orbitMode_;
}

void CadViewer::updateOrbitStateFromCamera()
{
    if (!view_ || view_->Camera().IsNull()) return;

    const gp_Dir direction = view_->Camera()->Direction();
    const double horizontal = std::hypot(direction.X(), direction.Y());
    if (horizontal > 1.0e-9) {
        orbitAzimuth_ = std::atan2(direction.Y(), direction.X());
    }
    orbitElevation_ = std::asin(std::clamp(direction.Z(), -1.0, 1.0));
}

void CadViewer::applyTurntableCamera()
{
    if (!view_ || view_->Camera().IsNull()) return;

    const auto camera = view_->Camera();
    const gp_Pnt center = camera->Center();
    const double distance = std::max(camera->Distance(), 1.0e-6);
    const double elevation = std::clamp(
        orbitElevation_, -OrbitElevationLimit, OrbitElevationLimit);
    const double horizontal = std::cos(elevation);
    const gp_Dir direction(
        horizontal * std::cos(orbitAzimuth_),
        horizontal * std::sin(orbitAzimuth_),
        std::sin(elevation));

    gp_Pnt eye = center;
    eye.Translate(-gp_Vec(direction) * distance);
    camera->SetEyeAndCenter(eye, center);

    gp_Vec up(0.0, 0.0, 1.0);
    const gp_Vec directionVector(direction);
    up.SetCoord(
        up.X() - directionVector.X() * direction.Z(),
        up.Y() - directionVector.Y() * direction.Z(),
        up.Z() - directionVector.Z() * direction.Z());
    if (up.Magnitude() <= 1.0e-9) {
        up = gp_Vec(0.0, 1.0, 0.0);
    }
    camera->SetUp(gp_Dir(up));
}

void CadViewer::orbitTurntable(const QPoint& currentPosition)
{
    QElapsedTimer timer;
    timer.start();
    const int deltaX = currentPosition.x() - lastMousePosition_.x();
    const int deltaY = lastMousePosition_.y() - currentPosition.y();
    orbitAzimuth_ += static_cast<double>(deltaX) * OrbitRadiansPerPixel;
    orbitElevation_ = std::clamp(
        orbitElevation_ + static_cast<double>(deltaY) * OrbitRadiansPerPixel,
        -OrbitElevationLimit, OrbitElevationLimit);
    applyTurntableCamera();
    invalidateSnapProjectionCache("CAMERA_CHANGED: locked-Z orbit");
    view_->Redraw();
    recordPerformanceSample("orbit redraw", timer.elapsed());
    if (transformMode_) updateTransformGizmo();
}

cad::application::SelectionSnapshot CadViewer::selectionSnapshot() const
{
    return selectionState_.snapshot();
}

std::vector<cad::topology::TopologicalReference> CadViewer::captureTopologySelection(
    const cad::parametric::Body& body) const
{
    std::vector<cad::topology::TopologicalReference> result;
    if (!selectionAdapter_) return result;

    for (const auto& hit : selectionAdapter_->selectedHits()) {
        if (!hit.hasSubshape() || hit.shape.IsNull()) continue;
        const auto feature = body.findFeature(hit.item.featureId.toStdString());
        if (!feature || feature->shape().IsNull()) continue;
        try {
            result.push_back(cad::topology::TopologicalSignatureBuilder::createReference(
                feature->id(), feature->shape(), hit.shape));
        } catch (const Standard_Failure& failure) {
            qCWarning(pcadViewerLog) << "failed to capture topology"
                                     << hit.item.featureId
                                     << failure.GetMessageString();
        } catch (const std::exception& failure) {
            qCWarning(pcadViewerLog) << "failed to capture topology"
                                     << hit.item.featureId
                                     << failure.what();
        }
    }
    return result;
}

void CadViewer::restoreSelection(
    const cad::parametric::Body& body,
    const std::vector<cad::topology::TopologicalReference>& topology,
    const std::vector<std::string>& objectFeatureIds)
{
    if (!initialized_ || !context_) return;

    traceSelectionLifecycle("ClearSelected BEFORE (restoreSelection)");
    context_->ClearSelected(Standard_False);
    traceSelectionLifecycle("ClearSelected AFTER (restoreSelection)");
    const cad::topology::TopologicalReferenceResolver resolver(body);
    for (const auto& reference : topology) {
        const auto resolved = resolver.resolve(reference);
        if (resolved.status != cad::topology::ResolveStatus::Resolved || !resolved.shape) continue;
        const auto object = featureObjects_.find(QString::fromStdString(reference.featureId));
        if (object == featureObjects_.end() || object->second.IsNull()
            || !context_->IsDisplayed(object->second)
            || !featureIsSelectable(object->first)) continue;
        const Handle(StdSelect_BRepOwner) owner = new StdSelect_BRepOwner(
            *resolved.shape, object->second, 0, Standard_True);
        context_->AddOrRemoveSelected(owner, Standard_False);
    }
    for (const auto& featureId : objectFeatureIds) {
        const auto object = featureObjects_.find(QString::fromStdString(featureId));
        if (object != featureObjects_.end() && !object->second.IsNull()
            && context_->IsDisplayed(object->second)
            && featureIsSelectable(object->first)) {
            context_->AddOrRemoveSelected(object->second, Standard_False);
        }
    }
    if (bulkUpdateDepth_ == 0) context_->UpdateCurrentViewer();
    syncSelectionStateFromOcct();
    updateTransformGizmo();
}

void CadViewer::setPushPullCommittedHandler(
    std::function<void(const QString&, const cad::topology::TopologicalReference&,
                       const gp_Vec&, double)> handler
)
{
    pushPullCommittedHandler_ = std::move(handler);
}

void CadViewer::cancelActiveOperation()
{
    if (pushPullActive_) cancelPushPull();
    if (transformDragging_) cancelTransform();
}

void CadViewer::clearSelection()
{
    if (!initialized_) {
        return;
    }

    traceSelectionLifecycle("ClearSelected BEFORE");
    context_->ClearSelected(Standard_True);
    traceSelectionLifecycle("ClearSelected AFTER");
    notifyFeatureSelection();
}

void CadViewer::applySelectionMode()
{
    traceSelectionLifecycle(QString("applySelectionMode begin mode=%1 dirty=%2 managed=%3 features=%4")
        .arg(selectionModeName(selectionMode_))
        .arg(selectionActivationDirty_)
        .arg(managedSelectionModes_.size())
        .arg(featureObjects_.size()));
    if (!initialized_) {
        traceSelectionLifecycle("applySelectionMode skipped: not initialized");
        return;
    }

    // Redisplaying a shape does not change the active AIS selection mode.
    // Keep activation scoped to persistent feature presentations; the OCCT
    // context also contains transient previews and technical overlays with
    // independent lifetimes.
    if (!selectionActivationDirty_) {
        traceSelectionLifecycle("applySelectionMode skipped: not dirty");
        return;
    }

    QElapsedTimer timer;
    timer.start();
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
    case SelectionMode::Vertex:
        mode = AIS_Shape::SelectionMode(TopAbs_VERTEX);
        break;
    }

    for (const auto& [id, object] : featureObjects_) {
        if (id == editingSketchFeatureId_) {
            traceSelectionLifecycle(QString("apply skips editing Sketch feature=%1 obj=%2")
                .arg(id).arg(selectionObjectPointer(object)));
            continue;
        }
        if (object.IsNull()) {
            managedSelectionModes_.erase(id);
            continue;
        }

        const bool selectable = !selectionDisabled_ && featureIsSelectable(id);
        const auto active = managedSelectionModes_.find(id);
        traceSelectionLifecycle(QString("apply obj=%1 feature=%2 displayed=%3 old=%4 requested=%5 selectable=%6")
            .arg(selectionObjectPointer(object))
            .arg(id)
            .arg(context_->IsDisplayed(object) ? 1 : 0)
            .arg(active == managedSelectionModes_.end()
                ? QStringLiteral("none") : QString::number(active->second))
            .arg(mode)
            .arg(selectable ? 1 : 0));
        if (!selectable) {
            if (active != managedSelectionModes_.end()) {
                traceSelectionLifecycle(QString("Deactivate object BEFORE obj=%1 feature=%2 mode=%3")
                    .arg(selectionObjectPointer(object)).arg(id).arg(active->second));
                context_->Deactivate(object, active->second);
                traceSelectionLifecycle(QString("Deactivate object AFTER obj=%1 feature=%2")
                    .arg(selectionObjectPointer(object)).arg(id));
                managedSelectionModes_.erase(active);
            } else {
                traceSelectionLifecycle(QString("Deactivate object BEFORE obj=%1 feature=%2 mode=all")
                    .arg(selectionObjectPointer(object)).arg(id));
                context_->Deactivate(object);
                traceSelectionLifecycle(QString("Deactivate object AFTER obj=%1 feature=%2")
                    .arg(selectionObjectPointer(object)).arg(id));
            }
            continue;
        }

        if (active != managedSelectionModes_.end() && active->second == mode) {
            continue;
        }
        if (active != managedSelectionModes_.end()) {
            traceSelectionLifecycle(QString("Deactivate object BEFORE obj=%1 feature=%2 mode=%3")
                .arg(selectionObjectPointer(object)).arg(id).arg(active->second));
            context_->Deactivate(object, active->second);
            traceSelectionLifecycle(QString("Deactivate object AFTER obj=%1 feature=%2")
                .arg(selectionObjectPointer(object)).arg(id));
        }
        // Use Multiple concurrency explicitly. The convenience Activate()
        // overload requests GlobalOrLocal concurrency and may internally
        // deactivate selectors on unrelated AIS objects.
        traceSelectionLifecycle(QString("SetSelectionModeActive BEFORE obj=%1 feature=%2 mode=%3 displayed=%4")
            .arg(selectionObjectPointer(object)).arg(id).arg(mode)
            .arg(context_->IsDisplayed(object) ? 1 : 0));
        context_->SetSelectionModeActive(
            object, mode, Standard_True,
            AIS_SelectionModesConcurrency_Multiple,
            Standard_True);
        traceSelectionLifecycle(QString("SetSelectionModeActive AFTER obj=%1 feature=%2 mode=%3")
            .arg(selectionObjectPointer(object)).arg(id).arg(mode));
        traceSelectionLifecycle(QString("AIS state after activation obj=%1 feature=%2 cached=%3 activeModes=%4")
            .arg(selectionObjectPointer(object)).arg(id).arg(mode)
            .arg(activeSelectionModes(context_, object)));
        managedSelectionModes_[id] = mode;
    }
    if (transformGizmo_) {
        transformGizmo_->deactivateSelection();
    }
    QElapsedTimer updateTimer;
    updateTimer.start();
    context_->UpdateCurrentViewer();
    lastViewerUpdateMilliseconds_ = updateTimer.elapsed();
    lastSelectionActivationMilliseconds_ = timer.elapsed();
    selectionActivationDirty_ = false;
    if (performanceDiagnostics_) {
        qInfo().noquote() << "CadViewer selection activation: mode="
                          << static_cast<int>(selectionMode_)
                          << "AIS=" << featureObjects_.size()
                          << "ms=" << lastSelectionActivationMilliseconds_
                          << "viewerUpdateMs=" << lastViewerUpdateMilliseconds_;
    }
    traceSelectionLifecycle(QString("applySelectionMode end mode=%1 dirty=%2 managed=%3")
        .arg(selectionModeName(selectionMode_))
        .arg(selectionActivationDirty_)
        .arg(managedSelectionModes_.size()));
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
        const auto found = std::find_if(
            featureObjects_.begin(), featureObjects_.end(),
            [&shape](const auto& entry) { return entry.second == shape; });
        if (found != featureObjects_.end()) setFeatureTransparency(found->first, shape);
        else if (xRayEnabled_) shape->SetTransparency(XRayTransparency);
        else shape->UnsetTransparency();
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
    if (!initialized_ || pushPullActive_ || selectionDisabled_) {
        return;
    }

    resetDetectedCycle();

    if (selectionAdapter_) {
        QElapsedTimer timer;
        timer.start();
        selectionAdapter_->moveTo(position, view_, true);
        recordPerformanceSample("hover MoveTo", timer.elapsed());
        const auto detected = selectionAdapter_->detectedHit();
        if (!detected) {
            selectionState_.hovered.reset();
        } else if (!selectionState_.hovered
                   || !selectionState_.hovered->hasSameTransientIdentity(*detected)) {
            selectionState_.hovered = detected;
        }
    }
}

void CadViewer::syncSelectionStateFromOcct()
{
    if (!selectionAdapter_) {
        selectionState_.rebuildSelected({});
        return;
    }
    selectionState_.rebuildSelected(selectionAdapter_->selectedHits());
}

void CadViewer::selectAt(
    const QPoint& position,
    bool toggleSelection,
    bool cycleDetected)
{
    if (!initialized_ || pushPullActive_ || selectionDisabled_) {
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

    if (!selectionAdapter_) {
        return false;
    }

    const auto faceHit = selectionAdapter_->validatedSelectedFaceHit();
    if (!faceHit) {
        return false;
    }

    const auto selectedObject = Handle(AIS_Shape)::DownCast(
        faceHit->presentation);
    if (!featureIsSelectable(faceHit->item.featureId)) return false;
    if (selectedObject.IsNull() || faceHit->shape.IsNull()
        || faceHit->shape.ShapeType() != TopAbs_FACE
        || !faceHit->item.currentSubshapeIndex
        || *faceHit->item.currentSubshapeIndex <= 0
        || selectedObject->Shape().IsNull()) {
        return false;
    }

    const TopoDS_Face selectedFace = TopoDS::Face(faceHit->shape);
    const QString featureId = faceHit->item.featureId;
    const int faceIndex = *faceHit->item.currentSubshapeIndex;
    const auto faceReference = cad::topology::TopologicalSignatureBuilder::createReference(
        featureId.toStdString(), selectedObject->Shape(), selectedFace);
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
    pushPullFaceReference_ = faceReference;
    pushPullObject_ = selectedObject;
    pushPullDistance_ = 0.0;
    if (!operationSession_->beginCreate(
            cad::application::InteractiveOperationKind::PushPull,
            {featureId.toStdString()})) {
        return false;
    }
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

    if (pushPullFace_.IsNull()
        || pushPullFace_.ShapeType() != TopAbs_FACE
        || pushPullBaseShape_.IsNull()) {
        return {};
    }

    try {
        const gp_Vec extrusionVector = pushPullNormal_ * distance;
        BRepPrimAPI_MakePrism prismBuilder(pushPullFace_, extrusionVector);
        prismBuilder.Build();
        if (!prismBuilder.IsDone() || prismBuilder.Shape().IsNull()) return {};
        const TopoDS_Shape prism = prismBuilder.Shape();

        if (distance > 0.0) {
            BRepAlgoAPI_Fuse fuse(pushPullBaseShape_, prism);
            fuse.Build();
            return fuse.IsDone() && !fuse.Shape().IsNull()
                ? fuse.Shape() : TopoDS_Shape();
        }

        BRepAlgoAPI_Cut cut(pushPullBaseShape_, prism);
        cut.Build();
        return cut.IsDone() && !cut.Shape().IsNull()
            ? cut.Shape() : TopoDS_Shape();
    } catch (const Standard_Failure&) {
        return {};
    } catch (const std::exception&) {
        return {};
    }
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
    operationSession_->updatePreview("distance", pushPullDistance_);

    if (std::abs(pushPullDistance_) <= PushPullTolerance) {
        if (!pushPullPreview_.IsNull()) {
            context_->Remove(pushPullPreview_, Standard_False);
            pushPullPreview_.Nullify();
        }

        if (!pushPullObject_.IsNull()) {
            applyFeaturePresentation(pushPullFeatureId_, pushPullObject_);
        }

        context_->UpdateCurrentViewer();
        return;
    }

    const TopoDS_Shape previewShape =
        buildPushPullResult(pushPullDistance_);

    if (previewShape.IsNull()) {
        cancelPushPull();
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
    const auto faceReference = pushPullFaceReference_;
    const gp_Vec normal = pushPullNormal_;
    const double distance = pushPullDistance_;

    if (hasChange && pushPullPreview_.IsNull()) {
        cancelPushPull();
        return;
    }

    if (!pushPullPreview_.IsNull()) {
        context_->Remove(pushPullPreview_, Standard_False);
        pushPullPreview_.Nullify();
    }

    const auto operation = operationSession_->commit();
    if (!operation) {
        return;
    }

    if (!pushPullObject_.IsNull()) {
        applyFeaturePresentation(pushPullFeatureId_, pushPullObject_);
    }

    pushPullActive_ = false;
    pushPullDistance_ = 0.0;
    pushPullFace_.Nullify();
    pushPullBaseShape_.Nullify();
    pushPullFeatureId_.clear();
    pushPullFaceIndex_ = 0;
    pushPullFaceReference_.reset();
    pushPullObject_.Nullify();
    if (pushPullArmed_) {
        setCursor(Qt::CrossCursor);
    } else {
        unsetCursor();
    }

    applySelectionMode();
    context_->UpdateCurrentViewer();
    syncToolBarState();

    if (hasChange && pushPullCommittedHandler_ && faceReference) {
        pushPullCommittedHandler_(featureId, *faceReference, normal, distance);
    }
}

void CadViewer::cancelPushPull()
{
    if (!pushPullActive_) {
        return;
    }

    operationSession_->cancel();

    if (!pushPullPreview_.IsNull()) {
        context_->Remove(pushPullPreview_, Standard_False);
        pushPullPreview_.Nullify();
    }

    if (!pushPullObject_.IsNull()) {
        applyFeaturePresentation(pushPullFeatureId_, pushPullObject_);
    }

    pushPullActive_ = false;
    pushPullDistance_ = 0.0;
    pushPullFace_.Nullify();
    pushPullBaseShape_.Nullify();
    pushPullFeatureId_.clear();
    pushPullFaceIndex_ = 0;
    pushPullFaceReference_.reset();
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

void CadViewer::stopMousePan()
{
    if (interactionMode_ == InteractionMode::Pan && mouseGrabber() == this) {
        releaseMouse();
    }
    interactionMode_ = InteractionMode::None;
}

std::optional<std::array<QPointF, 3>> CadViewer::currentTrihedronAxisPositions() const
{
    if (!view_ || view_->Camera().IsNull()) return std::nullopt;

    Standard_Integer width = 0;
    Standard_Integer height = 0;
    view_->Window()->Size(width, height);
    if (width <= 0 || height <= 0) return std::nullopt;

    const double indicatorSize = std::clamp(
        std::min(this->width(), this->height()) * AxisIndicatorScale,
        48.0,
        120.0
    );
    const QPointF base(
        indicatorSize * 0.85,
        this->height() - indicatorSize * 0.85
    );
    const auto project = [this, width, height](const gp_Pnt& point) {
        return projectWorldPoint(view_, point, width, height);
    };
    const gp_Pnt origin = view_->Camera()->Center();
    const std::array<gp_Dir, 3> axes{
        gp_Dir(1.0, 0.0, 0.0),
        gp_Dir(0.0, 1.0, 0.0),
        gp_Dir(0.0, 0.0, 1.0)
    };
    const double armLength = indicatorSize * AxisIndicatorArm;
    std::array<QPointF, 3> positions{};
    const QPointF projectedOrigin = project(origin);
    for (int axis = 0; axis < static_cast<int>(axes.size()); ++axis) {
        gp_Pnt axisPoint = origin;
        axisPoint.Translate(gp_Vec(axes[axis]));
        const QPointF screenDirection = project(axisPoint) - projectedOrigin;
        const double length = std::hypot(
            screenDirection.x(),
            screenDirection.y()
        );
        if (length <= 1.0e-6) {
            positions[axis] = base;
            continue;
        }
        positions[axis] =
            base + screenDirection * (armLength / length);
    }
    return positions;
}

std::optional<int> CadViewer::axisIndicatorHitTest(const QPoint& position) const
{
    const auto positions = currentTrihedronAxisPositions();
    if (!positions) return std::nullopt;

    const double indicatorSize = std::clamp(
        std::min(this->width(), this->height()) * AxisIndicatorScale,
        48.0,
        120.0
    );
    const QPointF base(
        indicatorSize * 0.85,
        this->height() - indicatorSize * 0.85
    );
    const double hitRadius = std::clamp(
        indicatorSize * AxisIndicatorHitRadius,
        18.0,
        28.0
    );

    std::optional<int> closestAxis;
    double closestDistance = hitRadius;
    for (int index = 0; index < 3; ++index) {
        const QPointF start = base;
        const QPointF cursor(position);
        const QPointF segment = (*positions)[index] - start;
        const double segmentLengthSquared = QPointF::dotProduct(segment, segment);
        if (segmentLengthSquared <= 1.0e-6) continue;
        const double projection = std::clamp(
            QPointF::dotProduct(cursor - start, segment)
                / segmentLengthSquared,
            0.0,
            1.0
        );
        const QPointF nearest = start + segment * projection;
        const double distance = std::hypot(
            cursor.x() - nearest.x(),
            cursor.y() - nearest.y()
        );
        if (distance <= closestDistance) {
            closestDistance = distance;
            closestAxis = index;
        }
    }

    return closestAxis;
}

void CadViewer::updateAxisHover(const QPoint& position)
{
    const auto axis = axisIndicatorHitTest(position);
    if (axis) {
        if (!axisHoverActive_) {
            setCursor(Qt::PointingHandCursor);
            axisHoverActive_ = true;
        }
        return;
    }

    clearAxisHover();
}

void CadViewer::clearAxisHover()
{
    if (axisHoverActive_) {
        unsetCursor();
        axisHoverActive_ = false;
    }
}

void CadViewer::setStandardView(const StandardView view)
{
    if (!view_ || view_->Camera().IsNull()) return;

    gp_Dir direction;
    gp_Dir up;
    switch (view) {
    case StandardView::Right:
        direction = gp_Dir(-1.0, 0.0, 0.0);
        up = gp_Dir(0.0, 0.0, 1.0);
        break;
    case StandardView::Front:
        direction = gp_Dir(0.0, -1.0, 0.0);
        up = gp_Dir(0.0, 0.0, 1.0);
        break;
    case StandardView::Top:
        direction = gp_Dir(0.0, 0.0, -1.0);
        up = gp_Dir(0.0, 1.0, 0.0);
        break;
    }

    const auto camera = view_->Camera();
    const gp_Pnt center = camera->Center();
    const double distance = std::max(camera->Distance(), 1.0e-6);
    gp_Pnt eye = center;
    eye.Translate(-gp_Vec(direction) * distance);
    camera->SetEyeAndCenter(eye, center);
    camera->SetUp(up);
    switch (view) {
    case StandardView::Right:
        orbitAzimuth_ = Pi;
        orbitElevation_ = 0.0;
        break;
    case StandardView::Front:
        orbitAzimuth_ = -Pi / 2.0;
        orbitElevation_ = 0.0;
        break;
    case StandardView::Top:
        orbitAzimuth_ = 0.0;
        orbitElevation_ = -Pi / 2.0;
        break;
    }
    invalidateSnapProjectionCache("CAMERA_CHANGED: standard view");
    view_->Redraw();
    if (transformMode_) {
        updateTransformGizmo();
    }
}

void CadViewer::mousePressEvent(QMouseEvent* event)
{
    lastMousePosition_ =
        event->position().toPoint();
    mousePressPosition_ = lastMousePosition_;
    interactionMode_ = InteractionMode::None;
    clearAxisHover();

    if (sketchLinePickSketch_ && event->button() == Qt::LeftButton) {
        sketchLinePickMousePress_ = true;
        std::optional<cad::parametric::SketchEntityId> id;
        try {
            id = sketchLineAtScreen(lastMousePosition_);
        } catch (const std::exception&) {
            id.reset();
        }
        if (qEnvironmentVariableIsSet("PARAMETRICCAD_TRACE_ACTIONS")) {
            const auto text = id ? QString::fromStdString(*id) : QStringLiteral("<none>");
            std::fprintf(stderr, "[REV] click position=%d,%d entity=%s\n",
                lastMousePosition_.x(), lastMousePosition_.y(), text.toUtf8().constData());
            std::fflush(stderr);
        }
        if (id && sketchLinePickedHandler_) {
            if (qEnvironmentVariableIsSet("PARAMETRICCAD_TRACE_ACTIONS")) {
                std::fprintf(stderr, "[REV] entity accepted=%s\n", id->c_str());
                std::fflush(stderr);
            }
            sketchLinePickedHandler_(*id);
        }
        return;
    }

    if (sketchMode_ && event->button() == Qt::LeftButton) {
        if (sketchPreviewTool_ != SketchPreviewTool::Trim
            && sketchPreviewTool_ != SketchPreviewTool::Extend
            && !context_.IsNull()) {
            context_->MoveTo(lastMousePosition_.x(), lastMousePosition_.y(), view_, Standard_False);
            const auto detected = context_->DetectedInteractive();
            for (const auto& marker : sketchConstraintMarkers_) {
                if (!marker.presentation.IsNull() && detected == marker.presentation) {
                    if (sketchConstraintMarkerClickedHandler_)
                        sketchConstraintMarkerClickedHandler_(marker.constraintId);
                    return;
                }
            }
        }
        const auto point = sketchPointAtScreen(lastMousePosition_);
        if (point) {
            gp_Pnt world = sketchOrigin_;
            world.Translate(gp_Vec(sketchXDirection_) * point->X()
                + gp_Vec(sketchYDirection_) * point->Y());
            if (!sketchPreviewFirstPoint_) {
                sketchPreviewFirstPoint_ = world;
            } else {
                sketchPreviewFirstPoint_.reset();
                if (!sketchPreviewObject_.IsNull() && !context_.IsNull()) {
                    context_->Remove(sketchPreviewObject_, Standard_True);
                }
                sketchPreviewObject_.Nullify();
            }
        }
        if (sketchPointClickedHandler_) {
            if (point) sketchPointClickedHandler_(*point,
                sketchLocalToleranceFromPixels(lastMousePosition_, SketchTrimHitPixels));
        }
        return;
    }

    if (transformDragging_) {
        if (event->button() == Qt::RightButton) {
            cancelTransform();
            return;
        }
        if (event->button() == Qt::LeftButton) return;
    }

    if (initialized_ && event->button() == Qt::LeftButton && !pushPullActive_) {
        if (sectionHandleAt(lastMousePosition_) >= 0) {
            sectionDragging_ = true;
            sectionHandleHovered_ = true;
            sectionDragStart_ = lastMousePosition_;
            setCursor(Qt::ClosedHandCursor);
            setToolTip("Drag to move section plane");
            if (sectionInteractionStatusHandler_)
                sectionInteractionStatusHandler_("Drag to move section plane");
            return;
        }
        const auto spatialHandle = spatialBoxHandleAt(lastMousePosition_);
        if (spatialHandle >= 0) {
            spatialBoxHandle_ = spatialHandle;
            spatialBoxDragStart_ = lastMousePosition_;
            spatialBoxDragMin_ = spatialBoxMin_;
            spatialBoxDragMax_ = spatialBoxMax_;
            return;
        }
        const auto axis = axisIndicatorHitTest(lastMousePosition_);
        if (axis) {
            switch (*axis) {
            case 0:
                setStandardView(StandardView::Right);
                break;
            case 1:
                setStandardView(StandardView::Front);
                break;
            case 2:
                setStandardView(StandardView::Top);
                break;
            }
            clearAxisHover();
            interactionMode_ = InteractionMode::None;
            return;
        }
    }

    if (transformMode_ && event->button() == Qt::LeftButton
        && transformGizmo_) {
        const auto handle = transformGizmo_->hitTest(lastMousePosition_, view_);
        if (handle != TransformHandle::None) {
            beginTransform(
                handle,
                lastMousePosition_,
                event->modifiers().testFlag(Qt::ControlModifier)
            );
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

        if (orbitMode_ == OrbitMode::Free) {
            view_->StartRotation(
                lastMousePosition_.x(),
                lastMousePosition_.y()
            );
        } else {
            updateOrbitStateFromCamera();
        }
        invalidateSnapProjectionCache("CAMERA_CHANGED: rotation started");
    }

    if (initialized_ && !selectionDisabled_
        && event->button() == Qt::LeftButton
        && !pushPullArmed_) {
        selectionAdapter_->moveTo(lastMousePosition_, view_, true);
        if (!selectionAdapter_->detectedHit()) {
            interactionMode_ = InteractionMode::PendingEmptyPan;
        }
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

    if (sketchLinePickSketch_ && event->buttons() == Qt::NoButton) {
        updateSketchLinePickHover(currentPosition);
    }

    if (sketchMode_ && sketchPreviewTool_ == SketchPreviewTool::None && !context_.IsNull()) {
        context_->MoveTo(currentPosition.x(), currentPosition.y(), view_, Standard_False);
        std::string hovered;
        const auto detected = context_->DetectedInteractive();
        for (const auto& marker : sketchConstraintMarkers_) {
            if (!marker.presentation.IsNull() && detected == marker.presentation) { hovered = marker.constraintId; break; }
        }
        if (sketchConstraintMarkerHoveredHandler_) sketchConstraintMarkerHoveredHandler_(hovered);
    }

    if (sketchMode_ && (sketchPreviewTool_ == SketchPreviewTool::Trim
        || sketchPreviewTool_ == SketchPreviewTool::Extend)) {
        const auto point = sketchPointAtScreen(currentPosition);
        if (point && sketchMouseMovedHandler_) {
            sketchMouseMovedHandler_(*point, sketchLocalToleranceFromPixels(currentPosition,
                SketchTrimHitPixels));
        } else if (!point) {
            clearSketchTrimPreview();
        }
    }

    if (sketchMode_ && sketchPreviewFirstPoint_
        && sketchPreviewTool_ != SketchPreviewTool::Trim
        && sketchPreviewTool_ != SketchPreviewTool::Extend) {
        const auto point = sketchPointAtScreen(currentPosition);
        if (point && sketchPreviewTool_ != SketchPreviewTool::None) {
            gp_Pnt current = sketchOrigin_;
            current.Translate(gp_Vec(sketchXDirection_) * point->X()
                + gp_Vec(sketchYDirection_) * point->Y());
            const auto shape = makeSketchPreviewShape(
                *sketchPreviewFirstPoint_, current, sketchNormal_, sketchPreviewTool_);
            if (sketchPreviewObject_.IsNull()) {
                sketchPreviewObject_ = new AIS_Shape(shape);
                sketchPreviewObject_->SetDisplayMode(AIS_WireFrame);
                sketchPreviewObject_->SetColor(Quantity_NOC_YELLOW);
                sketchPreviewObject_->SetWidth(2.0);
                context_->Display(sketchPreviewObject_, Standard_True);
            } else {
                sketchPreviewObject_->SetShape(shape);
                context_->Redisplay(sketchPreviewObject_, Standard_True);
            }
        }
    }

    if (event->buttons() != Qt::NoButton) {
        clearAxisHover();
    }

    if (transformDragging_) {
        updateTransformPreview(currentPosition);
        lastMousePosition_ = currentPosition;
        return;
    }

    if (sectionDragging_) {
        if (!event->buttons().testFlag(Qt::LeftButton)) {
            sectionDragging_ = false;
            return;
        }
        updateSectionDrag(currentPosition);
        if (sectionInteractionStatusHandler_) {
            const double offset = sectionState_.axis == SectionAxis::X
                ? sectionState_.origin.X()
                : sectionState_.axis == SectionAxis::Y
                    ? sectionState_.origin.Y() : sectionState_.origin.Z();
            sectionInteractionStatusHandler_(QString("Section offset: %1").arg(offset, 0, 'f', 3));
        }
        lastMousePosition_ = currentPosition;
        return;
    }

    if (spatialBoxHandle_ >= 0) {
        if (!event->buttons().testFlag(Qt::LeftButton)) {
            spatialBoxHandle_ = -1;
            return;
        }
        updateSpatialBoxDrag(currentPosition);
        lastMousePosition_ = currentPosition;
        return;
    }

    if (pushPullActive_) {
        updatePushPullPreview(currentPosition);
        lastMousePosition_ = currentPosition;
        return;
    }

    if (interactionMode_ == InteractionMode::Pan) {
        if (!event->buttons().testFlag(Qt::LeftButton)) {
            stopMousePan();
            return;
        }

        const int deltaX = currentPosition.x() - lastMousePosition_.x();
        const int deltaY = lastMousePosition_.y() - currentPosition.y();
        QElapsedTimer timer;
        timer.start();
        invalidateSnapProjectionCache("CAMERA_CHANGED: pan");
        view_->Pan(deltaX, deltaY);
        recordPerformanceSample("pan redraw", timer.elapsed());
        lastMousePosition_ = currentPosition;
        return;
    }

    if (interactionMode_ == InteractionMode::PendingEmptyPan) {
        if (!event->buttons().testFlag(Qt::LeftButton)) {
            interactionMode_ = InteractionMode::None;
        } else if ((currentPosition - mousePressPosition_).manhattanLength()
                   >= QApplication::startDragDistance()) {
            interactionMode_ = InteractionMode::Pan;
            grabMouse();
            lastMousePosition_ = currentPosition;
        }
        if (interactionMode_ == InteractionMode::Pan) {
            return;
        }
    }

    if (event->buttons().testFlag(Qt::MiddleButton)) {

        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            const int deltaX =
                currentPosition.x() -
                lastMousePosition_.x();

            const int deltaY =
                lastMousePosition_.y() -
                currentPosition.y();

            QElapsedTimer timer;
            timer.start();
            view_->Pan(
                deltaX,
                deltaY
            );
            invalidateSnapProjectionCache("CAMERA_CHANGED: pan");
            recordPerformanceSample("pan redraw", timer.elapsed());
        } else {
            if (orbitMode_ == OrbitMode::Free) {
                QElapsedTimer timer;
                timer.start();
                view_->Rotation(
                    currentPosition.x(),
                    currentPosition.y()
                );
                invalidateSnapProjectionCache("CAMERA_CHANGED: orbit");
                recordPerformanceSample("orbit redraw", timer.elapsed());
            } else {
                orbitTurntable(currentPosition);
            }
        }

    } else if (event->buttons() == Qt::NoButton) {
        const bool sectionHovered = sectionHandleAt(currentPosition) >= 0;
        if (sectionHovered != sectionHandleHovered_) {
            sectionHandleHovered_ = sectionHovered;
            updateSectionPresentation();
        }
        if (sectionHovered) {
            setCursor(Qt::OpenHandCursor);
            setToolTip("Drag to move section plane");
            if (sectionInteractionStatusHandler_)
                sectionInteractionStatusHandler_("Drag to move section plane");
            lastMousePosition_ = currentPosition;
            return;
        }
        if (sectionHandleHovered_) {
            sectionHandleHovered_ = false;
            updateSectionPresentation();
        }
        unsetCursor();
        setToolTip({});
        const auto axis = axisIndicatorHitTest(currentPosition);
        updateAxisHover(currentPosition);
        if (axis) {
            lastMousePosition_ = currentPosition;
            return;
        }

        if (transformMode_ && transformGizmo_) {
            transformGizmo_->setHovered(
                transformGizmo_->hitTest(currentPosition, view_));
        } else {
            updateHover(currentPosition);
        }
    }

    lastMousePosition_ = currentPosition;
}

void CadViewer::mouseReleaseEvent(QMouseEvent* event)
{
    if ((sketchLinePickSketch_ || sketchLinePickMousePress_)
        && event->button() == Qt::LeftButton) {
        sketchLinePickMousePress_ = false;
        return;
    }
    if (sectionDragging_ && event->button() == Qt::LeftButton) {
        sectionDragging_ = false;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (spatialBoxHandle_ >= 0 && event->button() == Qt::LeftButton) {
        spatialBoxHandle_ = -1;
        return;
    }
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

    if (event->button() == Qt::LeftButton) {
        if (interactionMode_ == InteractionMode::Pan) {
            stopMousePan();
            return;
        }
        if (interactionMode_ == InteractionMode::PendingEmptyPan) {
            interactionMode_ = InteractionMode::None;
        }
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

void CadViewer::focusOutEvent(QFocusEvent* event)
{
    clearAxisHover();
    if (interactionMode_ != InteractionMode::None) {
        stopMousePan();
    }
    QWidget::focusOutEvent(event);
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

    zoomAtCursor(event->position().toPoint(), factor);
    invalidateSnapProjectionCache("CAMERA_CHANGED: zoom");
}

void CadViewer::keyPressEvent(QKeyEvent* event)
{
    if (sketchMode_ && event->key() == Qt::Key_Escape) {
        sketchPreviewFirstPoint_.reset();
        if (!sketchPreviewObject_.IsNull() && !context_.IsNull()) {
            context_->Remove(sketchPreviewObject_, Standard_True);
        }
        sketchPreviewObject_.Nullify();
        if (sketchCancelHandler_) sketchCancelHandler_();
        event->accept();
        return;
    }
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

    if (interactionMode_ != InteractionMode::None &&
        event->key() == Qt::Key_Escape) {
        stopMousePan();
        return;
    }

    if (axisPickCancelHandler_ && event->key() == Qt::Key_Escape) {
        axisPickCancelHandler_();
        event->accept();
        return;
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
    case Qt::Key_4:
        setSelectionMode(SelectionMode::Vertex);
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

    // A tree selection change must not leave an in-progress transform attached
    // to the previous feature. The next selection is then the sole source for
    // gizmo attachment and snap source references.
    if (transformDragging_) cancelTransform();

    // Only change AIS selection. Keep presentations, camera and selection mode.
    // This is the tree-to-viewer path; do not echo a selection notification.
    traceSelectionLifecycle("ClearSelected BEFORE (applySelectionSnapshot)");
    context_->ClearSelected(Standard_False);
    traceSelectionLifecycle("ClearSelected AFTER (applySelectionSnapshot)");
    for (const auto& [id, object] : featureObjects_) {
        if (featureIds.contains(id) && context_->IsDisplayed(object)
            && featureIsSelectable(id)) {
            context_->AddOrRemoveSelected(object, Standard_False);
        }
    }
    context_->UpdateCurrentViewer();
    syncSelectionStateFromOcct();
    updateTransformGizmo();
}

void CadViewer::notifyFeatureSelection()
{
    syncSelectionStateFromOcct();
    const auto snapshot = selectionState_.snapshot();
    emit selectionChanged(snapshot);
    QStringList ids;
    for (const auto& item : selectionState_.selected) {
        if (!ids.contains(item.featureId)) {
            ids.append(item.featureId);
        }
    }
    emit featureSelectionChanged(ids);
    updateTransformGizmo();
}
