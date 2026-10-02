#include "viewer/TransformGizmo.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <Graphic3d_Camera.hxx>
#include <Quantity_Color.hxx>

#include <algorithm>
#include <cmath>

namespace cad::viewer {

namespace
{
constexpr double HitTolerancePixels = 12.0;
constexpr int RingSegments = 48;
constexpr double Pi = 3.14159265358979323846;

double distanceToSegment(const QPointF& point, const QPointF& first, const QPointF& second)
{
    const QPointF direction = second - first;
    const double lengthSquared = QPointF::dotProduct(direction, direction);
    if (lengthSquared <= 1.0e-9) return std::hypot(point.x() - first.x(), point.y() - first.y());
    const double t = std::clamp(
        QPointF::dotProduct(point - first, direction) / lengthSquared,
        0.0,
        1.0
    );
    const QPointF nearest = first + direction * t;
    return std::hypot(point.x() - nearest.x(), point.y() - nearest.y());
}

Quantity_Color colorFor(
    const TransformHandle handle,
    const TransformHandle hovered,
    const bool snapActive
)
{
    if (snapActive) return Quantity_Color(Quantity_NOC_YELLOW);
    if (handle == hovered) return Quantity_Color(Quantity_NOC_WHITE);
    switch (handle) {
    case TransformHandle::TranslateX:
    case TransformHandle::RotateX: return Quantity_Color(Quantity_NOC_RED);
    case TransformHandle::TranslateY:
    case TransformHandle::RotateY: return Quantity_Color(Quantity_NOC_GREEN);
    case TransformHandle::TranslateZ:
    case TransformHandle::RotateZ: return Quantity_Color(Quantity_NOC_BLUE1);
    case TransformHandle::Center: return Quantity_Color(Quantity_NOC_YELLOW);
    default: return Quantity_Color(Quantity_NOC_WHITE);
    }
}
}

TransformGizmo::TransformGizmo(const Handle(AIS_InteractiveContext)& context)
    : context_(context),
      axes_{gp_Dir(1.0, 0.0, 0.0), gp_Dir(0.0, 1.0, 0.0), gp_Dir(0.0, 0.0, 1.0)}
{
}

void TransformGizmo::clearPresentations()
{
    for (const auto& part : parts_) {
        if (!part.presentation.IsNull()) {
            context_->Remove(part.presentation, Standard_False);
        }
    }
    parts_.clear();
}

void TransformGizmo::show(const gp_Pnt& pivot, const Handle(V3d_View)& view)
{
    clearPresentations();
    pivot_ = pivot;

    const auto camera = view->Camera();
    const double depth = std::max(
        1.0,
        gp_Vec(camera->Eye(), pivot).Dot(gp_Vec(camera->Direction()))
    );
    size_ = std::max(1.0, camera->ViewDimensions(depth).Y() * 0.16);

    const std::array<Quantity_Color, 3> colors{
        Quantity_Color(Quantity_NOC_RED),
        Quantity_Color(Quantity_NOC_GREEN),
        Quantity_Color(Quantity_NOC_BLUE1)
    };
    for (int index = 0; index < 3; ++index) {
        const gp_Pnt end = pivot_.Translated(gp_Vec(axes_[index]) * size_);
        Part axisPart;
        axisPart.handle = static_cast<TransformHandle>(
            static_cast<int>(TransformHandle::TranslateX) + index);
        axisPart.points = {pivot_, end};
        axisPart.presentation = new AIS_Shape(BRepBuilderAPI_MakeEdge(pivot_, end));
        axisPart.presentation->SetColor(colors[index]);
        axisPart.presentation->SetWidth(3.0);
        context_->Display(axisPart.presentation, Standard_False);
        context_->Deactivate(axisPart.presentation);
        parts_.push_back(std::move(axisPart));

        Part ringPart;
        ringPart.handle = static_cast<TransformHandle>(
            static_cast<int>(TransformHandle::RotateX) + index);
        gp_Vec first = index == 0 ? gp_Vec(0.0, 1.0, 0.0) : gp_Vec(1.0, 0.0, 0.0);
        if (index == 1) first = gp_Vec(0.0, 0.0, 1.0);
        const gp_Vec second = gp_Vec(axes_[index]) ^ first;
        BRepBuilderAPI_MakePolygon polygon;
        for (int segment = 0; segment < RingSegments; ++segment) {
            const double angle = 2.0 * Pi * segment / RingSegments;
            const gp_Vec offset = (first * std::cos(angle) + second * std::sin(angle)) * size_ * 0.78;
            const gp_Pnt point = pivot_.Translated(offset);
            polygon.Add(point);
            ringPart.points.push_back(point);
        }
        polygon.Close();
        ringPart.presentation = new AIS_Shape(polygon.Wire());
        ringPart.presentation->SetColor(colors[index]);
        ringPart.presentation->SetWidth(2.0);
        context_->Display(ringPart.presentation, Standard_False);
        context_->Deactivate(ringPart.presentation);
        parts_.push_back(std::move(ringPart));
    }

    Part center;
    center.handle = TransformHandle::Center;
    center.points = {pivot_};
    center.presentation = new AIS_Shape(BRepPrimAPI_MakeSphere(pivot_, size_ * 0.08).Shape());
    center.presentation->SetColor(Quantity_Color(Quantity_NOC_YELLOW));
    context_->Display(center.presentation, Standard_False);
    context_->Deactivate(center.presentation);
    parts_.push_back(std::move(center));
    recolor();
    context_->UpdateCurrentViewer();
}

void TransformGizmo::hide()
{
    clearPresentations();
    context_->UpdateCurrentViewer();
}

bool TransformGizmo::visible() const noexcept
{
    return !parts_.empty();
}

gp_Pnt TransformGizmo::project(const gp_Pnt& point, const Handle(V3d_View)& view) const
{
    Standard_Integer x = 0;
    Standard_Integer y = 0;
    Standard_Real z = 0.0;
    view->Convert(point.X(), point.Y(), point.Z(), x, y);
    return gp_Pnt(x, y, z);
}

TransformHandle TransformGizmo::hitTest(
    const QPoint& position,
    const Handle(V3d_View)& view
) const
{
    if (!visible()) return TransformHandle::None;
    Standard_Integer width = 0;
    Standard_Integer height = 0;
    view->Window()->Size(width, height);
    const QPointF point(position.x(), height - position.y());
    TransformHandle result = TransformHandle::None;
    double best = HitTolerancePixels;
    for (const auto& part : parts_) {
        if (part.points.size() == 1) {
            const gp_Pnt screen = project(part.points.front(), view);
            const double distance = std::hypot(
                point.x() - screen.X(), point.y() - screen.Y());
            if (distance < best) { best = distance; result = part.handle; }
            continue;
        }
        for (std::size_t index = 1; index < part.points.size(); ++index) {
            const gp_Pnt first = project(part.points[index - 1], view);
            const gp_Pnt second = project(part.points[index], view);
            const double distance = distanceToSegment(
                point,
                QPointF(first.X(), first.Y()),
                QPointF(second.X(), second.Y())
            );
            if (distance < best) { best = distance; result = part.handle; }
        }
        if (part.points.size() > 2) {
            const gp_Pnt first = project(part.points.back(), view);
            const gp_Pnt second = project(part.points.front(), view);
            const double distance = distanceToSegment(
                point,
                QPointF(first.X(), first.Y()),
                QPointF(second.X(), second.Y())
            );
            if (distance < best) { best = distance; result = part.handle; }
        }
    }
    return result;
}

void TransformGizmo::setHovered(const TransformHandle handle)
{
    hovered_ = handle;
    recolor();
}

void TransformGizmo::setSnapActive(const bool active)
{
    snapActive_ = active;
    recolor();
}

void TransformGizmo::deactivateSelection()
{
    for (const auto& part : parts_) {
        if (!part.presentation.IsNull()) {
            context_->Deactivate(part.presentation);
        }
    }
}

void TransformGizmo::recolor()
{
    for (const auto& part : parts_) {
        if (!part.presentation.IsNull()) {
            part.presentation->SetColor(colorFor(part.handle, hovered_, snapActive_));
            context_->Redisplay(part.presentation, Standard_False);
        }
    }
}

const gp_Pnt& TransformGizmo::pivot() const noexcept
{
    return pivot_;
}

const gp_Dir& TransformGizmo::axis(const TransformHandle handle) const
{
    switch (handle) {
    case TransformHandle::TranslateX:
    case TransformHandle::RotateX: return axes_[0];
    case TransformHandle::TranslateY:
    case TransformHandle::RotateY: return axes_[1];
    case TransformHandle::TranslateZ:
    case TransformHandle::RotateZ: return axes_[2];
    default: return axes_[0];
    }
}

} // namespace cad::viewer
