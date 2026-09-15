#include "operations/ParametricFeatures.h"

#include "operations/BasicFeatures.h"

#include <stdexcept>
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
            std::string(parameterName) + " feature must not be null"
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

TopoDS_Shape BoxParametricFeature::build() const
{
    return cad::modeling::BasicFeatures::box(width_, depth_, height_);
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

TopoDS_Shape CylinderParametricFeature::build() const
{
    return cad::modeling::BasicFeatures::cylinder(radius_, height_);
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

TopoDS_Shape TorusFeature::build() const
{
    return cad::modeling::BasicFeatures::torus(
        majorRadius_,
        minorRadius_
    );
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
    std::vector<std::pair<TopoDS_Edge, TopoDS_Face>> edges,
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

TopoDS_Shape SweepFeature::build() const
{
    return cad::modeling::BasicFeatures::sweep(
        path_,
        profile_->shape()
    );
}

} // namespace cad::parametric
