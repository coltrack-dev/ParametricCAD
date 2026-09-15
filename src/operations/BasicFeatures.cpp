#include "operations/BasicFeatures.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>

#include <BRepOffset_Mode.hxx>
#include <GeomAbs_JoinType.hxx>
#include <TopTools_ListOfShape.hxx>

#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

template <typename Builder>
TopoDS_Shape checkedShape(
    Builder& builder,
    const char* operationName
)
{
    builder.Build();

    if (!builder.IsDone()) {
        throw std::runtime_error(
            std::string(operationName) + " failed"
        );
    }

    TopoDS_Shape result = builder.Shape();
    if (result.IsNull()) {
        throw std::runtime_error(
            std::string(operationName) + " returned a null shape"
        );
    }

    return result;
}

} // namespace

namespace cad::modeling {

TopoDS_Shape BasicFeatures::box(
    const double width,
    const double depth,
    const double height
)
{
    requirePositive(width, "width");
    requirePositive(depth, "depth");
    requirePositive(height, "height");

    BRepPrimAPI_MakeBox builder(width, depth, height);
    return checkedShape(builder, "Box");
}

TopoDS_Shape BasicFeatures::cylinder(
    const double radius,
    const double height
)
{
    requirePositive(radius, "radius");
    requirePositive(height, "height");

    BRepPrimAPI_MakeCylinder builder(radius, height);
    return checkedShape(builder, "Cylinder");
}

TopoDS_Shape BasicFeatures::cone(
    const double bottomRadius,
    const double topRadius,
    const double height
)
{
    if (bottomRadius < 0.0 || topRadius < 0.0) {
        throw std::invalid_argument(
            "Cone radii must not be negative"
        );
    }
    if (bottomRadius == 0.0 && topRadius == 0.0) {
        throw std::invalid_argument(
            "At least one cone radius must be positive"
        );
    }
    requirePositive(height, "height");

    BRepPrimAPI_MakeCone builder(
        bottomRadius,
        topRadius,
        height
    );
    return checkedShape(builder, "Cone");
}

TopoDS_Shape BasicFeatures::sphere(const double radius)
{
    requirePositive(radius, "radius");

    BRepPrimAPI_MakeSphere builder(radius);
    return checkedShape(builder, "Sphere");
}

TopoDS_Shape BasicFeatures::torus(
    const double majorRadius,
    const double minorRadius
)
{
    requirePositive(majorRadius, "majorRadius");
    requirePositive(minorRadius, "minorRadius");

    if (minorRadius >= majorRadius) {
        throw std::invalid_argument(
            "minorRadius must be smaller than majorRadius"
        );
    }

    BRepPrimAPI_MakeTorus builder(
        majorRadius,
        minorRadius
    );
    return checkedShape(builder, "Torus");
}

TopoDS_Wire BasicFeatures::rectangleWire(
    const double width,
    const double height,
    const gp_Pnt& origin
)
{
    requirePositive(width, "width");
    requirePositive(height, "height");

    const gp_Pnt p1 = origin;
    const gp_Pnt p2(
        origin.X() + width,
        origin.Y(),
        origin.Z()
    );
    const gp_Pnt p3(
        origin.X() + width,
        origin.Y() + height,
        origin.Z()
    );
    const gp_Pnt p4(
        origin.X(),
        origin.Y() + height,
        origin.Z()
    );

    BRepBuilderAPI_MakeWire wireBuilder;
    wireBuilder.Add(BRepBuilderAPI_MakeEdge(p1, p2).Edge());
    wireBuilder.Add(BRepBuilderAPI_MakeEdge(p2, p3).Edge());
    wireBuilder.Add(BRepBuilderAPI_MakeEdge(p3, p4).Edge());
    wireBuilder.Add(BRepBuilderAPI_MakeEdge(p4, p1).Edge());

    if (!wireBuilder.IsDone()) {
        throw std::runtime_error(
            "Rectangle wire construction failed"
        );
    }

    return wireBuilder.Wire();
}

TopoDS_Wire BasicFeatures::circleWire(
    const double radius,
    const gp_Ax2& plane
)
{
    requirePositive(radius, "radius");

    const gp_Circ circle(plane, radius);
    BRepBuilderAPI_MakeEdge edgeBuilder(circle);

    if (!edgeBuilder.IsDone()) {
        throw std::runtime_error(
            "Circle edge construction failed"
        );
    }

    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
    if (!wireBuilder.IsDone()) {
        throw std::runtime_error(
            "Circle wire construction failed"
        );
    }

    return wireBuilder.Wire();
}

TopoDS_Face BasicFeatures::face(const TopoDS_Wire& wire)
{
    requireNonNull(wire, "wire");

    BRepBuilderAPI_MakeFace builder(wire);
    if (!builder.IsDone()) {
        throw std::runtime_error(
            "Face construction failed"
        );
    }

    return builder.Face();
}

TopoDS_Shape BasicFeatures::extrude(
    const TopoDS_Shape& profile,
    const gp_Vec& vector
)
{
    requireNonNull(profile, "profile");

    if (vector.SquareMagnitude() <= 1.0e-18) {
        throw std::invalid_argument(
            "Extrusion vector must not be zero"
        );
    }

    BRepPrimAPI_MakePrism builder(
        profile,
        vector,
        Standard_False,
        Standard_True
    );
    return checkedShape(builder, "Extrude");
}

TopoDS_Shape BasicFeatures::revolve(
    const TopoDS_Shape& profile,
    const gp_Ax1& axis,
    const double angleRadians
)
{
    requireNonNull(profile, "profile");

    if (!std::isfinite(angleRadians)
        || std::abs(angleRadians) <= 1.0e-12) {
        throw std::invalid_argument(
            "Revolution angle must be non-zero"
        );
    }

    BRepPrimAPI_MakeRevol builder(
        profile,
        axis,
        angleRadians,
        Standard_True
    );
    return checkedShape(builder, "Revolve");
}

TopoDS_Shape BasicFeatures::fuse(
    const TopoDS_Shape& left,
    const TopoDS_Shape& right
)
{
    requireNonNull(left, "left");
    requireNonNull(right, "right");

    BRepAlgoAPI_Fuse builder(left, right);
    return checkedShape(builder, "Fuse");
}

TopoDS_Shape BasicFeatures::cut(
    const TopoDS_Shape& base,
    const TopoDS_Shape& tool
)
{
    requireNonNull(base, "base");
    requireNonNull(tool, "tool");

    BRepAlgoAPI_Cut builder(base, tool);
    return checkedShape(builder, "Cut");
}

TopoDS_Shape BasicFeatures::common(
    const TopoDS_Shape& left,
    const TopoDS_Shape& right
)
{
    requireNonNull(left, "left");
    requireNonNull(right, "right");

    BRepAlgoAPI_Common builder(left, right);
    return checkedShape(builder, "Common");
}

TopoDS_Shape BasicFeatures::fillet(
    const TopoDS_Shape& shape,
    const std::vector<TopoDS_Edge>& edges,
    const double radius
)
{
    requireNonNull(shape, "shape");
    requirePositive(radius, "radius");

    if (edges.empty()) {
        throw std::invalid_argument(
            "Fillet requires at least one edge"
        );
    }

    BRepFilletAPI_MakeFillet builder(shape);
    for (const TopoDS_Edge& edge : edges) {
        if (edge.IsNull()) {
            throw std::invalid_argument(
                "Fillet edge must not be null"
            );
        }
        builder.Add(radius, edge);
    }

    return checkedShape(builder, "Fillet");
}

    TopoDS_Shape BasicFeatures::chamfer(
        const TopoDS_Shape& shape,
        const std::vector<TopoDS_Edge>& edges,
        const double distance
    )
{
    requireNonNull(shape, "shape");
    requirePositive(distance, "distance");

    if (edges.empty()) {
        throw std::invalid_argument(
            "Chamfer requires at least one edge"
        );
    }

    BRepFilletAPI_MakeChamfer builder(shape);

    for (const TopoDS_Edge& edge : edges) {
        if (edge.IsNull()) {
            throw std::invalid_argument(
                "Chamfer edge must not be null"
            );
        }

        builder.Add(distance, edge);
    }

    return checkedShape(builder, "Chamfer");
}

TopoDS_Shape BasicFeatures::shell(
    const TopoDS_Shape& shape,
    const std::vector<TopoDS_Face>& facesToRemove,
    const double thickness,
    const double tolerance
)
{
    requireNonNull(shape, "shape");

    if (!std::isfinite(thickness)
        || std::abs(thickness) <= 1.0e-12) {
        throw std::invalid_argument(
            "Shell thickness must be non-zero"
        );
    }
    requirePositive(tolerance, "tolerance");

    TopTools_ListOfShape removedFaces;
    for (const TopoDS_Face& face : facesToRemove) {
        if (face.IsNull()) {
            throw std::invalid_argument(
                "Shell face must not be null"
            );
        }
        removedFaces.Append(face);
    }

    BRepOffsetAPI_MakeThickSolid builder;
    builder.MakeThickSolidByJoin(
        shape,
        removedFaces,
        thickness,
        tolerance,
        BRepOffset_Skin,
        Standard_False,
        Standard_False,
        GeomAbs_Arc,
        Standard_False
    );

    if (!builder.IsDone()) {
        throw std::runtime_error(
            "Shell failed"
        );
    }

    TopoDS_Shape result = builder.Shape();
    if (result.IsNull()) {
        throw std::runtime_error(
            "Shell returned a null shape"
        );
    }

    return result;
}

TopoDS_Shape BasicFeatures::offset(
    const TopoDS_Shape& shape,
    const double distance,
    const double tolerance
)
{
    requireNonNull(shape, "shape");

    if (!std::isfinite(distance)
        || std::abs(distance) <= 1.0e-12) {
        throw std::invalid_argument(
            "Offset distance must be non-zero"
        );
    }
    requirePositive(tolerance, "tolerance");

    BRepOffsetAPI_MakeOffsetShape builder;
    builder.PerformByJoin(
        shape,
        distance,
        tolerance,
        BRepOffset_Skin,
        Standard_False,
        Standard_False,
        GeomAbs_Arc,
        Standard_False
    );

    if (!builder.IsDone()) {
        throw std::runtime_error(
            "Offset failed"
        );
    }

    TopoDS_Shape result = builder.Shape();
    if (result.IsNull()) {
        throw std::runtime_error(
            "Offset returned a null shape"
        );
    }

    return result;
}

TopoDS_Shape BasicFeatures::loft(
    const std::vector<TopoDS_Wire>& sections,
    const bool makeSolid,
    const bool ruled
)
{
    if (sections.size() < 2) {
        throw std::invalid_argument(
            "Loft requires at least two section wires"
        );
    }

    BRepOffsetAPI_ThruSections builder(
        makeSolid ? Standard_True : Standard_False,
        ruled ? Standard_True : Standard_False
    );

    for (const TopoDS_Wire& section : sections) {
        if (section.IsNull()) {
            throw std::invalid_argument(
                "Loft section must not be null"
            );
        }
        builder.AddWire(section);
    }

    return checkedShape(builder, "Loft");
}

TopoDS_Shape BasicFeatures::sweep(
    const TopoDS_Wire& path,
    const TopoDS_Shape& profile
)
{
    requireNonNull(path, "path");
    requireNonNull(profile, "profile");

    BRepOffsetAPI_MakePipe builder(path, profile);
    return checkedShape(builder, "Sweep");
}

void BasicFeatures::requirePositive(
    const double value,
    const char* parameterName
)
{
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(
            std::string(parameterName)
            + " must be a finite positive number"
        );
    }
}

void BasicFeatures::requireNonNull(
    const TopoDS_Shape& shape,
    const char* parameterName
)
{
    if (shape.IsNull()) {
        throw std::invalid_argument(
            std::string(parameterName)
            + " must not be null"
        );
    }
}

} // namespace cad::modeling
