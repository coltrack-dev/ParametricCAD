#include "operations/ParametricFeatures.h"

#include "operations/BasicFeatures.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>

#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <array>
#include <cmath>

#include <stdexcept>
#include <string>
#include <utility>

namespace cad::parametric {

namespace {

void requireFeature(
    const ParametricFeature::Ptr& feature,
    const char* parameterName
)
{
    if (!feature) {
        throw std::invalid_argument(
            std::string(parameterName)
            + " feature must not be null"
        );
    }
}

} // namespace

BoxParametricFeature::BoxParametricFeature(
    std::string id,
    const double width,
    const double depth,
    const double height
)
    : ParametricFeature(std::move(id), "Box"),
      width_(width),
      depth_(depth),
      height_(height)
{
}

void BoxParametricFeature::setSize(
    const double width,
    const double depth,
    const double height
)
{
    width_ = width;
    depth_ = depth;
    height_ = height;
    markDirty();
}

double BoxParametricFeature::width() const noexcept
{
    return width_;
}

double BoxParametricFeature::depth() const noexcept
{
    return depth_;
}

double BoxParametricFeature::height() const noexcept
{
    return height_;
}

TopoDS_Shape BoxParametricFeature::build() const
{
    return cad::modeling::BasicFeatures::box(
        width_,
        depth_,
        height_
    );
}

CylinderParametricFeature::CylinderParametricFeature(
    std::string id,
    const double radius,
    const double height
)
    : ParametricFeature(std::move(id), "Cylinder"),
      radius_(radius),
      height_(height)
{
}

void CylinderParametricFeature::setRadius(const double radius)
{
    radius_ = radius;
    markDirty();
}

void CylinderParametricFeature::setHeight(const double height)
{
    height_ = height;
    markDirty();
}

double CylinderParametricFeature::radius() const noexcept
{
    return radius_;
}

double CylinderParametricFeature::height() const noexcept
{
    return height_;
}

TopoDS_Shape CylinderParametricFeature::build() const
{
    return cad::modeling::BasicFeatures::cylinder(
        radius_,
        height_
    );
}

ConeFeature::ConeFeature(
    std::string id,
    const double bottomRadius,
    const double topRadius,
    const double height
)
    : ParametricFeature(std::move(id), "Cone"),
      bottomRadius_(bottomRadius),
      topRadius_(topRadius),
      height_(height)
{
}

void ConeFeature::setBottomRadius(const double radius)
{
    bottomRadius_ = radius;
    markDirty();
}

void ConeFeature::setTopRadius(const double radius)
{
    topRadius_ = radius;
    markDirty();
}

void ConeFeature::setHeight(const double height)
{
    height_ = height;
    markDirty();
}

double ConeFeature::bottomRadius() const noexcept
{
    return bottomRadius_;
}

double ConeFeature::topRadius() const noexcept
{
    return topRadius_;
}

double ConeFeature::height() const noexcept
{
    return height_;
}

TopoDS_Shape ConeFeature::build() const
{
    return cad::modeling::BasicFeatures::cone(
        bottomRadius_,
        topRadius_,
        height_
    );
}

SphereFeature::SphereFeature(
    std::string id,
    const double radius
)
    : ParametricFeature(std::move(id), "Sphere"),
      radius_(radius)
{
}

void SphereFeature::setRadius(const double radius)
{
    radius_ = radius;
    markDirty();
}

double SphereFeature::radius() const noexcept
{
    return radius_;
}

TopoDS_Shape SphereFeature::build() const
{
    return cad::modeling::BasicFeatures::sphere(radius_);
}

TorusFeature::TorusFeature(
    std::string id,
    const double majorRadius,
    const double minorRadius
)
    : ParametricFeature(std::move(id), "Torus"),
      majorRadius_(majorRadius),
      minorRadius_(minorRadius)
{
}

void TorusFeature::setMajorRadius(const double radius)
{
    majorRadius_ = radius;
    markDirty();
}

void TorusFeature::setMinorRadius(const double radius)
{
    minorRadius_ = radius;
    markDirty();
}

double TorusFeature::majorRadius() const noexcept
{
    return majorRadius_;
}

double TorusFeature::minorRadius() const noexcept
{
    return minorRadius_;
}

TopoDS_Shape TorusFeature::build() const
{
    return cad::modeling::BasicFeatures::torus(
        majorRadius_,
        minorRadius_
    );
}


HexagonFeature::HexagonFeature(
    std::string id,
    const double acrossFlats,
    const double height
)
    : ParametricFeature(std::move(id), "Hexagon"),
      acrossFlats_(acrossFlats),
      height_(height)
{
}

void HexagonFeature::setAcrossFlats(
    const double acrossFlats
)
{
    acrossFlats_ = acrossFlats;
    markDirty();
}

void HexagonFeature::setHeight(
    const double height
)
{
    height_ = height;
    markDirty();
}

double HexagonFeature::acrossFlats() const noexcept
{
    return acrossFlats_;
}

double HexagonFeature::height() const noexcept
{
    return height_;
}

TopoDS_Shape HexagonFeature::build() const
{
    if (!std::isfinite(acrossFlats_) || acrossFlats_ <= 0.0) {
        throw std::invalid_argument(
            "acrossFlats must be a finite positive number"
        );
    }

    if (!std::isfinite(height_) || height_ <= 0.0) {
        throw std::invalid_argument(
            "height must be a finite positive number"
        );
    }

    // For a regular hexagon:
    // acrossFlats = sqrt(3) * circumradius.
    const double circumradius =
        acrossFlats_ / std::sqrt(3.0);

    std::array<gp_Pnt, 6> points{};

    constexpr double Pi =
        3.1415926535897932384626433832795;

    for (int index = 0; index < 6; ++index) {
        // 30-degree offset gives horizontal top/bottom flats.
        const double angle =
            Pi / 6.0
            + static_cast<double>(index) * Pi / 3.0;

        points[static_cast<std::size_t>(index)] =
            gp_Pnt(
                circumradius * std::cos(angle),
                circumradius * std::sin(angle),
                0.0
            );
    }

    BRepBuilderAPI_MakeWire wireBuilder;

    for (int index = 0; index < 6; ++index) {
        const int nextIndex =
            (index + 1) % 6;

        wireBuilder.Add(
            BRepBuilderAPI_MakeEdge(
                points[static_cast<std::size_t>(index)],
                points[static_cast<std::size_t>(nextIndex)]
            ).Edge()
        );
    }

    if (!wireBuilder.IsDone()) {
        throw std::runtime_error(
            "Hexagon wire construction failed"
        );
    }

    BRepBuilderAPI_MakeFace faceBuilder(
        wireBuilder.Wire()
    );

    if (!faceBuilder.IsDone()) {
        throw std::runtime_error(
            "Hexagon face construction failed"
        );
    }

    BRepPrimAPI_MakePrism prismBuilder(
        faceBuilder.Face(),
        gp_Vec(0.0, 0.0, height_)
    );

    prismBuilder.Build();

    if (!prismBuilder.IsDone()) {
        throw std::runtime_error(
            "Hexagon extrusion failed"
        );
    }

    TopoDS_Shape result =
        prismBuilder.Shape();

    if (result.IsNull()) {
        throw std::runtime_error(
            "Hexagon extrusion returned a null shape"
        );
    }

    return result;
}

ExtrudeFeature::ExtrudeFeature(
    std::string id,
    const Ptr& profile,
    gp_Vec vector
)
    : ParametricFeature(std::move(id), "Extrude"),
      profile_(profile),
      vector_(std::move(vector))
{
    requireFeature(profile_, "profile");
    addDependency(profile_);
}

void ExtrudeFeature::setVector(gp_Vec vector)
{
    vector_ = std::move(vector);
    markDirty();
}

const ParametricFeature::Ptr&
ExtrudeFeature::profile() const noexcept
{
    return profile_;
}

const gp_Vec&
ExtrudeFeature::vector() const noexcept
{
    return vector_;
}

TopoDS_Shape ExtrudeFeature::build() const
{
    return cad::modeling::BasicFeatures::extrude(
        profile_->shape(),
        vector_
    );
}

RevolveFeature::RevolveFeature(
    std::string id,
    const Ptr& profile,
    gp_Ax1 axis,
    const double angleRadians
)
    : ParametricFeature(std::move(id), "Revolve"),
      profile_(profile),
      axis_(std::move(axis)),
      angleRadians_(angleRadians)
{
    requireFeature(profile_, "profile");
    addDependency(profile_);
}

void RevolveFeature::setAxis(gp_Ax1 axis)
{
    axis_ = std::move(axis);
    markDirty();
}

void RevolveFeature::setAngleRadians(
    const double angleRadians
)
{
    angleRadians_ = angleRadians;
    markDirty();
}

const ParametricFeature::Ptr&
RevolveFeature::profile() const noexcept
{
    return profile_;
}

const gp_Ax1&
RevolveFeature::axis() const noexcept
{
    return axis_;
}

double RevolveFeature::angleRadians() const noexcept
{
    return angleRadians_;
}

TopoDS_Shape RevolveFeature::build() const
{
    return cad::modeling::BasicFeatures::revolve(
        profile_->shape(),
        axis_,
        angleRadians_
    );
}

BooleanFeature::BooleanFeature(
    std::string id,
    const Ptr& left,
    const Ptr& right,
    const BooleanOperation operation
)
    : ParametricFeature(std::move(id), "Boolean"),
      left_(left),
      right_(right),
      operation_(operation)
{
    requireFeature(left_, "left");
    requireFeature(right_, "right");

    addDependency(left_);
    addDependency(right_);
}

void BooleanFeature::setOperation(
    const BooleanOperation operation
)
{
    operation_ = operation;
    markDirty();
}

const ParametricFeature::Ptr&
BooleanFeature::left() const noexcept
{
    return left_;
}

const ParametricFeature::Ptr&
BooleanFeature::right() const noexcept
{
    return right_;
}

BooleanOperation
BooleanFeature::operation() const noexcept
{
    return operation_;
}

TopoDS_Shape BooleanFeature::build() const
{
    switch (operation_) {
        case BooleanOperation::Fuse:
            return cad::modeling::BasicFeatures::fuse(
                left_->shape(),
                right_->shape()
            );

        case BooleanOperation::Cut:
            return cad::modeling::BasicFeatures::cut(
                left_->shape(),
                right_->shape()
            );

        case BooleanOperation::Common:
            return cad::modeling::BasicFeatures::common(
                left_->shape(),
                right_->shape()
            );
    }

    throw std::logic_error("Unknown Boolean operation");
}

FilletFeature::FilletFeature(
    std::string id,
    const Ptr& base,
    std::vector<TopoDS_Edge> edges,
    const double radius
)
    : ParametricFeature(std::move(id), "Fillet"),
      base_(base),
      edges_(std::move(edges)),
      radius_(radius)
{
    requireFeature(base_, "base");
    addDependency(base_);
}

void FilletFeature::setEdges(
    std::vector<TopoDS_Edge> edges
)
{
    edges_ = std::move(edges);
    markDirty();
}

void FilletFeature::setRadius(const double radius)
{
    radius_ = radius;
    markDirty();
}

const ParametricFeature::Ptr&
FilletFeature::base() const noexcept
{
    return base_;
}

const std::vector<TopoDS_Edge>&
FilletFeature::edges() const noexcept
{
    return edges_;
}

double FilletFeature::radius() const noexcept
{
    return radius_;
}

TopoDS_Shape FilletFeature::build() const
{
    return cad::modeling::BasicFeatures::fillet(
        base_->shape(),
        edges_,
        radius_
    );
}

ChamferFeature::ChamferFeature(
    std::string id,
    const Ptr& base,
    std::vector<TopoDS_Edge> edges,
    const double distance
)
    : ParametricFeature(std::move(id), "Chamfer"),
      base_(base),
      edges_(std::move(edges)),
      distance_(distance)
{
    requireFeature(base_, "base");
    addDependency(base_);
}

void ChamferFeature::setEdges(
    std::vector<TopoDS_Edge> edges
)
{
    edges_ = std::move(edges);
    markDirty();
}

void ChamferFeature::setDistance(
    const double distance
)
{
    distance_ = distance;
    markDirty();
}

const ParametricFeature::Ptr&
ChamferFeature::base() const noexcept
{
    return base_;
}

const std::vector<TopoDS_Edge>&
ChamferFeature::edges() const noexcept
{
    return edges_;
}

double ChamferFeature::distance() const noexcept
{
    return distance_;
}

TopoDS_Shape ChamferFeature::build() const
{
    return cad::modeling::BasicFeatures::chamfer(
        base_->shape(),
        edges_,
        distance_
    );
}

ShellFeature::ShellFeature(
    std::string id,
    const Ptr& base,
    std::vector<TopoDS_Face> facesToRemove,
    const double thickness
)
    : ParametricFeature(std::move(id), "Shell"),
      base_(base),
      facesToRemove_(std::move(facesToRemove)),
      thickness_(thickness)
{
    requireFeature(base_, "base");
    addDependency(base_);
}

void ShellFeature::setFacesToRemove(
    std::vector<TopoDS_Face> faces
)
{
    facesToRemove_ = std::move(faces);
    markDirty();
}

void ShellFeature::setThickness(
    const double thickness
)
{
    thickness_ = thickness;
    markDirty();
}

const ParametricFeature::Ptr&
ShellFeature::base() const noexcept
{
    return base_;
}

const std::vector<TopoDS_Face>&
ShellFeature::facesToRemove() const noexcept
{
    return facesToRemove_;
}

double ShellFeature::thickness() const noexcept
{
    return thickness_;
}

TopoDS_Shape ShellFeature::build() const
{
    return cad::modeling::BasicFeatures::shell(
        base_->shape(),
        facesToRemove_,
        thickness_
    );
}

OffsetFeature::OffsetFeature(
    std::string id,
    const Ptr& base,
    const double distance
)
    : ParametricFeature(std::move(id), "Offset"),
      base_(base),
      distance_(distance)
{
    requireFeature(base_, "base");
    addDependency(base_);
}

void OffsetFeature::setDistance(
    const double distance
)
{
    distance_ = distance;
    markDirty();
}

const ParametricFeature::Ptr&
OffsetFeature::base() const noexcept
{
    return base_;
}

double OffsetFeature::distance() const noexcept
{
    return distance_;
}

TopoDS_Shape OffsetFeature::build() const
{
    return cad::modeling::BasicFeatures::offset(
        base_->shape(),
        distance_
    );
}

LoftFeature::LoftFeature(
    std::string id,
    std::vector<TopoDS_Wire> sections,
    const bool makeSolid,
    const bool ruled
)
    : ParametricFeature(std::move(id), "Loft"),
      sections_(std::move(sections)),
      makeSolid_(makeSolid),
      ruled_(ruled)
{
}

void LoftFeature::setSections(
    std::vector<TopoDS_Wire> sections
)
{
    sections_ = std::move(sections);
    markDirty();
}

void LoftFeature::setMakeSolid(
    const bool makeSolid
)
{
    makeSolid_ = makeSolid;
    markDirty();
}

void LoftFeature::setRuled(
    const bool ruled
)
{
    ruled_ = ruled;
    markDirty();
}

const std::vector<TopoDS_Wire>&
LoftFeature::sections() const noexcept
{
    return sections_;
}

bool LoftFeature::makeSolid() const noexcept
{
    return makeSolid_;
}

bool LoftFeature::ruled() const noexcept
{
    return ruled_;
}

TopoDS_Shape LoftFeature::build() const
{
    return cad::modeling::BasicFeatures::loft(
        sections_,
        makeSolid_,
        ruled_
    );
}

SweepFeature::SweepFeature(
    std::string id,
    TopoDS_Wire path,
    const Ptr& profile
)
    : ParametricFeature(std::move(id), "Sweep"),
      path_(std::move(path)),
      profile_(profile)
{
    requireFeature(profile_, "profile");
    addDependency(profile_);
}

void SweepFeature::setPath(
    TopoDS_Wire path
)
{
    path_ = std::move(path);
    markDirty();
}

const TopoDS_Wire&
SweepFeature::path() const noexcept
{
    return path_;
}

const ParametricFeature::Ptr&
SweepFeature::profile() const noexcept
{
    return profile_;
}

TopoDS_Shape SweepFeature::build() const
{
    return cad::modeling::BasicFeatures::sweep(
        path_,
        profile_->shape()
    );
}

} // namespace cad::parametric
