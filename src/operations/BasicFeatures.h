#pragma once

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>

#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <utility>
#include <vector>

namespace cad::modeling {

/**
 * Geometry-only modeling operations used by higher-level parametric Features.
 *
 * These functions intentionally do not know about Document, UI, selection,
 * undo/redo, or feature history.  They are the OpenCASCADE modeling kernel
 * adapter for ParametricCAD.
 */
class BasicFeatures final
{
public:
    BasicFeatures() = delete;

    // Primitive solids.
    static TopoDS_Shape box(double width, double depth, double height);
    static TopoDS_Shape cylinder(double radius, double height);
    static TopoDS_Shape cone(
        double bottomRadius,
        double topRadius,
        double height
    );
    static TopoDS_Shape sphere(double radius);
    static TopoDS_Shape torus(double majorRadius, double minorRadius);

    // Simple 2D profiles.
    static TopoDS_Wire rectangleWire(
        double width,
        double height,
        const gp_Pnt& origin = gp_Pnt(0.0, 0.0, 0.0)
    );
    static TopoDS_Wire circleWire(
        double radius,
        const gp_Ax2& plane = gp_Ax2()
    );
    static TopoDS_Face face(const TopoDS_Wire& wire);

    // Additive/subtractive construction.
    static TopoDS_Shape extrude(
        const TopoDS_Shape& profile,
        const gp_Vec& vector
    );
    static TopoDS_Shape revolve(
        const TopoDS_Shape& profile,
        const gp_Ax1& axis,
        double angleRadians
    );

    // Boolean composition.
    static TopoDS_Shape fuse(
        const TopoDS_Shape& left,
        const TopoDS_Shape& right
    );
    static TopoDS_Shape cut(
        const TopoDS_Shape& base,
        const TopoDS_Shape& tool
    );
    static TopoDS_Shape common(
        const TopoDS_Shape& left,
        const TopoDS_Shape& right
    );

    // Local detail operations.
    static TopoDS_Shape fillet(
        const TopoDS_Shape& shape,
        const std::vector<TopoDS_Edge>& edges,
        double radius
    );
    static TopoDS_Shape chamfer(
        const TopoDS_Shape& shape,
        const std::vector<TopoDS_Edge>& edges,
        double distance
    );
    // Thin-wall / offset operations.
    static TopoDS_Shape shell(
        const TopoDS_Shape& shape,
        const std::vector<TopoDS_Face>& facesToRemove,
        double thickness,
        double tolerance = 1.0e-3
    );
    static TopoDS_Shape offset(
        const TopoDS_Shape& shape,
        double distance,
        double tolerance = 1.0e-3
    );

    // Multi-profile / path-based construction.
    static TopoDS_Shape loft(
        const std::vector<TopoDS_Wire>& sections,
        bool makeSolid = true,
        bool ruled = false
    );
    static TopoDS_Shape sweep(
        const TopoDS_Wire& path,
        const TopoDS_Shape& profile
    );

private:
    static void requirePositive(double value, const char* parameterName);
    static void requireNonNull(const TopoDS_Shape& shape, const char* parameterName);
};

} // namespace cad::modeling
