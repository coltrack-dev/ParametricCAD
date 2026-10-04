#include "viewer/CadViewer.h"

#include <cmath>
#include <algorithm>
#include <array>

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QActionGroup>
#include <QLabel>
#include <QKeyEvent>
#include <QLoggingCategory>
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
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
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

gp_Pnt sketchWorldPoint(const cad::parametric::SketchFrame& frame, const gp_Pnt2d& point)
{
    gp_Pnt result = frame.origin;
    result.Translate(gp_Vec(frame.xDirection) * point.X()
        + gp_Vec(frame.yDirection) * point.Y());
    return result;
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
    const gp_Dir& yDirection, const gp_Dir& normal)
{
    sketchPreviousSelectionMode_ = selectionMode_;
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
}

void CadViewer::exitSketchMode()
{
    sketchMode_ = false;
    setSelectionMode(sketchPreviousSelectionMode_);
    sketchPreviewFirstPoint_.reset();
    if (!sketchPreviewObject_.IsNull() && !context_.IsNull()) {
        context_->Remove(sketchPreviewObject_, Standard_True);
    }
    sketchPreviewObject_.Nullify();
    clearSketchTrimPreview();
    clearSketchConstraintMarkers();
    clearSketchConstraintHighlight();
}

bool CadViewer::sketchMode() const noexcept
{
    return sketchMode_;
}

void CadViewer::setSketchPreviewTool(const SketchPreviewTool tool)
{
    sketchPreviewTool_ = tool;
    sketchPreviewFirstPoint_.reset();
    if (!sketchPreviewObject_.IsNull() && !context_.IsNull()) {
        context_->Remove(sketchPreviewObject_, Standard_True);
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
    if (!sketchTrimPreviewObject_.IsNull() && !context_.IsNull())
        context_->Remove(sketchTrimPreviewObject_, Standard_True);
    sketchTrimPreviewObject_.Nullify();
}

void CadViewer::clearSketchConstraintMarkers()
{
    if (!context_.IsNull()) {
        for (const auto& marker : sketchConstraintMarkers_)
            if (!marker.presentation.IsNull()) context_->Remove(marker.presentation, Standard_False);
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
    if (!sketchConstraintHighlightObject_.IsNull() && !context_.IsNull())
        context_->Remove(sketchConstraintHighlightObject_, Standard_True);
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
    if (!selectionAdapter_) {
        transformGizmo_->hide();
        return;
    }
    const auto selectedObjectHit = selectionAdapter_->validatedSelectedObjectHit();
    if (!selectedObjectHit) {
        transformGizmo_->hide();
        return;
    }
    const auto object = Handle(AIS_Shape)::DownCast(
        selectedObjectHit->presentation);
    if (object.IsNull()) {
        transformGizmo_->hide();
        return;
    }
    transformGizmo_->show(shapeCenter(object->Shape()), view_);
}

void CadViewer::beginTransform(
    const TransformHandle handle,
    const QPoint& position,
    const bool copyMode
)
{
    if (!transformGizmo_ || handle == TransformHandle::None) return;
    if (!selectionAdapter_) return;
    const auto selectedObjectHit = selectionAdapter_->validatedSelectedObjectHit();
    if (!selectedObjectHit) return;

    transformObject_ = Handle(AIS_Shape)::DownCast(
        selectedObjectHit->presentation);
    if (transformObject_.IsNull()) return;
    transformFeatureId_ = selectedObjectHit->item.featureId;
    if (transformFeatureId_.isEmpty()) return;

    transformHandle_ = handle;
    transformCopyMode_ = copyMode;
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
        // Snap only against geometry that is actually visible/selectable. Pattern,
        // boolean and copy features hide their dependencies; traversing those
        // presentations here duplicates the same topology in the snap target set.
        if (object == transformObject_ || object.IsNull()
            || !context_->IsDisplayed(object)) continue;
        const auto& references = cachedReferences(id, object->Shape());
        transformTargetReferences_.insert(
            transformTargetReferences_.end(), references.begin(), references.end());
    }
    auto cachedCandidates = snapCandidateCache_.find(transformFeatureId_);
    if (cachedCandidates != snapCandidateCache_.end()) {
        transformSnapCandidates_ = cachedCandidates->second;
    } else {
        transformSnapCandidates_ = std::make_shared<std::vector<SnapCandidate>>(
            snapManager_.buildCandidates(
                transformSourceReferences_, transformTargetReferences_));
        snapCandidateCache_.emplace(transformFeatureId_, transformSnapCandidates_);
    }
    Standard_Integer windowWidth = 0;
    Standard_Integer windowHeight = 0;
    view_->Window()->Size(windowWidth, windowHeight);
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
    qDebug() << "Snap cache invalidated:" << reason;
    snapReferenceCache_.clear();
    snapCandidateCache_.clear();
    transformSnapCandidates_.reset();
    snapScreenIndex_.reset();
    invalidateSnapProjectionCache(reason);
}

void CadViewer::invalidateSnapProjectionCache(const char* reason)
{
    qDebug() << "Snap screen projection cache invalidated:" << reason;
    snapScreenProjectionCache_.clear();
}

void CadViewer::updateTransformSnap(
    gp_Trsf& delta,
    gp_Pnt& pivot,
    const gp_Trsf& rawDelta)
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
        *transformSnapCandidates_,
        delta,
        std::function<QPointF(const gp_Pnt&)>(project),
        activeSnap_,
        snapScreenIndex_.get()
    );
    if (snapTimer.elapsed() > 2) {
        qWarning() << "SnapManager::findCandidate took" << snapTimer.elapsed() << "ms";
    }
    if (activeSnap_) {
        const gp_Trsf correction = activeSnap_->correction;
        delta = correction;
        delta.Multiply(rawDelta);
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
}

void CadViewer::updateTransformPreview(const QPoint& position)
{
    if (!transformDragging_ || transformObject_.IsNull()) return;
    QElapsedTimer frameTimer;
    frameTimer.start();
    qint64 rayAndMathMs = 0;
    qint64 snapMs = 0;
    qint64 presentationMs = 0;
    qint64 viewerUpdateMs = 0;
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

    rayAndMathMs = frameTimer.elapsed();
    const gp_Trsf previousDelta = transformDelta_;
    const gp_Trsf rawDelta = delta;
    gp_Pnt movedPivot = transformPivot_;
    QElapsedTimer snapTimer;
    snapTimer.start();
    updateTransformSnap(delta, movedPivot, rawDelta);
    snapMs = snapTimer.elapsed();
    if (transformsClose(previousDelta, delta)) {
        return;
    }
    transformDelta_ = delta;
    QElapsedTimer locationTimer;
    locationTimer.start();
    const auto previewObject = transformCopyMode_
        ? transformPreviewObject_
        : transformObject_;
    if (!previewObject.IsNull()) {
        context_->SetLocation(previewObject, TopLoc_Location(delta));
    }
    if (locationTimer.elapsed() > 2) {
        qWarning() << "Transform preview SetLocation took"
                   << locationTimer.elapsed() << "ms";
    }
    presentationMs = locationTimer.elapsed();
    QElapsedTimer redrawTimer;
    redrawTimer.start();
    context_->UpdateCurrentViewer();
    viewerUpdateMs = redrawTimer.elapsed();
    if (redrawTimer.elapsed() > 2) {
        qWarning() << "Transform preview viewer update took" << redrawTimer.elapsed() << "ms";
    }
    if (frameTimer.elapsed() > 2) {
        qWarning() << "Transform mouse move breakdown: ray/math" << rayAndMathMs
                   << "ms, snapping" << snapMs
                   << "ms, presentation" << presentationMs
                   << "ms, viewer" << viewerUpdateMs
                   << "ms, total" << frameTimer.elapsed() << "ms";
    }
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
        } else if (shape.ShapeType() == TopAbs_FACE || TopExp_Explorer(shape, TopAbs_FACE).More()) {
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
    invalidateSnapReferenceCache("MODEL_CHANGED: feature added");
    selectionState_.hovered.reset();

    applySelectionMode();
    if (fitView) {
        fitAll();
    }
}

void CadViewer::updateFeature(const TopoDS_Shape& shape, const QString& featureId)
{
    qCDebug(pcadViewerLog) << "updateFeature begin" << featureId
                           << "shapeNull" << shape.IsNull();
    if (shape.IsNull()) return;
    const auto found = featureObjects_.find(featureId);
    if (found == featureObjects_.end()) {
        display(shape, featureId, false);
        return;
    }
    const auto& object = found->second;
    configureSketchPresentation(object, featureId);
    if (object->Shape().IsEqual(shape)) {
        if (featureId.startsWith("sketch-")) {
            context_->Redisplay(object, Standard_True);
        }
        qCDebug(pcadViewerLog) << "updateFeature unchanged" << featureId
                               << "ais" << static_cast<const void*>(object.get());
        return;
    }

    cancelPushPull();
    resetDetectedCycle();
    invalidateSnapReferenceCache("MODEL_CHANGED: feature shape updated");
    selectionState_.hovered.reset();
    object->SetShape(shape);
    qCDebug(pcadViewerLog) << "updateFeature SetShape" << featureId
                           << "ais" << static_cast<const void*>(object.get());
    context_->Redisplay(object, Standard_True);
    syncSelectionStateFromOcct();
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
        invalidateSnapReferenceCache("MODEL_CHANGED: visibility changed");
        resetDetectedCycle();
        selectionState_.hovered.reset();
        syncSelectionStateFromOcct();
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
        invalidateSnapReferenceCache("MODEL_CHANGED: features retained");
        resetDetectedCycle();
        selectionState_.hovered.reset();
        syncSelectionStateFromOcct();
        context_->UpdateCurrentViewer();
    }
}

void CadViewer::clear()
{
    if (!initialized_) {
        return;
    }

    cancelPushPull();
    if (!sketchPreviewObject_.IsNull()) {
        context_->Remove(sketchPreviewObject_, Standard_False);
        sketchPreviewObject_.Nullify();
    }
    sketchPreviewFirstPoint_.reset();
    context_->RemoveAll(Standard_True);
    displayedShapes_.clear();
    featureObjects_.clear();
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

cad::application::SelectionSnapshot CadViewer::selectionSnapshot() const
{
    return selectionState_.snapshot();
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
    case SelectionMode::Vertex:
        mode = AIS_Shape::SelectionMode(TopAbs_VERTEX);
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

    if (!selectionAdapter_) {
        return false;
    }

    const auto faceHit = selectionAdapter_->validatedSelectedFaceHit();
    if (!faceHit) {
        return false;
    }

    const auto selectedObject = Handle(AIS_Shape)::DownCast(
        faceHit->presentation);
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
    const int faceIndex = pushPullFaceIndex_;
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

        view_->StartRotation(
            lastMousePosition_.x(),
            lastMousePosition_.y()
        );
        invalidateSnapProjectionCache("CAMERA_CHANGED: rotation started");
    }

    if (initialized_ &&
        event->button() == Qt::LeftButton &&
        !pushPullArmed_) {
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

    if (interactionMode_ == InteractionMode::Pan) {
        if (!event->buttons().testFlag(Qt::LeftButton)) {
            stopMousePan();
            return;
        }

        const int deltaX = currentPosition.x() - lastMousePosition_.x();
        const int deltaY = lastMousePosition_.y() - currentPosition.y();
        invalidateSnapProjectionCache("CAMERA_CHANGED: pan");
        view_->Pan(deltaX, deltaY);
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

            view_->Pan(
                deltaX,
                deltaY
            );
            invalidateSnapProjectionCache("CAMERA_CHANGED: pan");
        } else {
            view_->Rotation(
                currentPosition.x(),
                currentPosition.y()
            );
            invalidateSnapProjectionCache("CAMERA_CHANGED: orbit");
        }

    } else if (event->buttons() == Qt::NoButton) {
        const auto axis = axisIndicatorHitTest(currentPosition);
        updateAxisHover(currentPosition);
        if (axis) {
            lastMousePosition_ = currentPosition;
            return;
        }

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

    // Only change AIS selection. Keep presentations, camera and selection mode.
    // This is the tree-to-viewer path; do not echo a selection notification.
    context_->ClearSelected(Standard_False);
    for (const auto& [id, object] : featureObjects_) {
        if (featureIds.contains(id) && context_->IsDisplayed(object)) {
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
