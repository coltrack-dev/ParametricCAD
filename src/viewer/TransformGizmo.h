#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <QPoint>
#include <V3d_View.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <array>
#include <optional>
#include <vector>

namespace cad::viewer {

enum class TransformHandle
{
    None,
    TranslateX,
    TranslateY,
    TranslateZ,
    RotateX,
    RotateY,
    RotateZ,
    Center
};

class TransformGizmo final
{
public:
    explicit TransformGizmo(const Handle(AIS_InteractiveContext)& context);

    void show(const gp_Pnt& pivot, const Handle(V3d_View)& view);
    void hide();
    bool visible() const noexcept;
    TransformHandle hitTest(const QPoint& position, const Handle(V3d_View)& view) const;
    void setHovered(TransformHandle handle);
    void setSnapActive(bool active);
    void setSnapTarget(const std::optional<gp_Pnt>& point, const Handle(V3d_View)& view);
    void deactivateSelection();

    const gp_Pnt& pivot() const noexcept;
    const gp_Dir& axis(TransformHandle handle) const;

private:
    struct Part
    {
        TransformHandle handle{TransformHandle::None};
        Handle(AIS_Shape) presentation;
        std::vector<gp_Pnt> points;
    };

    void clearPresentations();
    void recolor();
    gp_Pnt project(const gp_Pnt& point, const Handle(V3d_View)& view) const;

    Handle(AIS_InteractiveContext) context_;
    std::array<gp_Dir, 3> axes_;
    std::vector<Part> parts_;
    gp_Pnt pivot_;
    double size_{1.0};
    TransformHandle hovered_{TransformHandle::None};
    bool snapActive_{false};
    Handle(AIS_Shape) snapTargetPresentation_;
};

} // namespace cad::viewer
