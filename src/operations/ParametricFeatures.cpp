#include "operations/ParametricFeatures.h"

#include "operations/BasicFeatures.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopAbs_ShapeEnum.hxx>

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

FeatureProperty numericProperty(
    const char* key,
    const char* label,
    const double value
)
{
    return {key, label, value, 0.001, 1'000'000.0, true};
}

FeatureProperty textProperty(
    const char* key,
    const char* label,
    std::string value
)
{
    return {key, label, std::move(value), std::nullopt, std::nullopt, false};
}

} // namespace

SketchFeature::SketchFeature(std::string id, double width, double height)
    : ParametricFeature(std::move(id), "Rectangle Sketch"),
      width_(width), height_(height)
{
}

std::vector<FeatureProperty> SketchFeature::properties() const
{
    return {numericProperty("width", "Width", width_),
            numericProperty("height", "Height", height_),
            textProperty("plane", "Plane", "XY")};
}

bool SketchFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "width") setSize(value, height_);
    else if (key == "height") setSize(width_, value);
    else return false;
    return true;
}

void SketchFeature::writeParameters(QJsonObject& object) const
{
    object.insert("plane", "XY");
    object.insert("width", width_);
    object.insert("height", height_);
}

void SketchFeature::setSize(double width, double height)
{
    width_ = width;
    height_ = height;
    markDirty();
}

double SketchFeature::width() const noexcept { return width_; }
double SketchFeature::height() const noexcept { return height_; }

TopoDS_Shape SketchFeature::build() const
{
    return cad::modeling::BasicFeatures::rectangleWire(width_, height_);
}

FaceFeature::FaceFeature(std::string id, const Ptr& source)
    : ParametricFeature(std::move(id), "Face"), source_(source)
{
    requireFeature(source, "Sketch source");
    if (source->role() != FeatureRole::Sketch) {
        throw std::invalid_argument("Face source must be a Rectangle Sketch");
    }
    sourceFeatureId_ = source->id();
    addDependency(source);
}

std::vector<FeatureProperty> FaceFeature::properties() const
{
    return {textProperty("sourceFeatureId", "Source Sketch", sourceFeatureId_)};
}

void FaceFeature::writeParameters(QJsonObject& object) const
{
    object.insert("sourceFeatureId", QString::fromStdString(sourceFeatureId_));
}

const std::string& FaceFeature::sourceFeatureId() const noexcept
{
    return sourceFeatureId_;
}

ParametricFeature::Ptr FaceFeature::source() const
{
    return source_.lock();
}

TopoDS_Shape FaceFeature::build() const
{
    const auto sketch = source();
    if (!sketch) {
        throw std::runtime_error("Missing Sketch source '" + sourceFeatureId_ + "'");
    }
    if (sketch->state() != FeatureState::UpToDate || sketch->shape().IsNull()
        || sketch->shape().ShapeType() != TopAbs_WIRE) {
        throw std::runtime_error("Sketch '" + sourceFeatureId_ + "' must be rebuilt into a valid closed wire");
    }
    // BasicFeatures validates the wire and constructs the face with BRepBuilderAPI_MakeFace.
    const auto face = cad::modeling::BasicFeatures::face(TopoDS::Wire(sketch->shape()));
    if (!BRepCheck_Analyzer(face).IsValid()) {
        throw std::runtime_error("Sketch '" + sourceFeatureId_ + "' produced an invalid face");
    }
    return face;
}

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

std::vector<FeatureProperty> BoxParametricFeature::properties() const
{
    return {numericProperty("width", "Width", width_),
            numericProperty("depth", "Depth", depth_),
            numericProperty("height", "Height", height_)};
}

bool BoxParametricFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "width") setSize(value, depth_, height_);
    else if (key == "depth") setSize(width_, value, height_);
    else if (key == "height") setSize(width_, depth_, value);
    else return false;
    return true;
}

void BoxParametricFeature::writeParameters(QJsonObject& object) const
{
    object.insert("width", width_);
    object.insert("depth", depth_);
    object.insert("height", height_);
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

std::vector<FeatureProperty> CylinderParametricFeature::properties() const
{
    return {numericProperty("radius", "Radius", radius_),
            numericProperty("height", "Height", height_)};
}

bool CylinderParametricFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "radius") setRadius(value);
    else if (key == "height") setHeight(value);
    else return false;
    return true;
}

void CylinderParametricFeature::writeParameters(QJsonObject& object) const
{
    object.insert("radius", radius_);
    object.insert("height", height_);
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

std::vector<FeatureProperty> ConeFeature::properties() const
{
    return {numericProperty("bottomRadius", "Bottom Radius", bottomRadius_),
            numericProperty("topRadius", "Top Radius", topRadius_),
            numericProperty("height", "Height", height_)};
}

bool ConeFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "bottomRadius") setBottomRadius(value);
    else if (key == "topRadius") setTopRadius(value);
    else if (key == "height") setHeight(value);
    else return false;
    return true;
}

void ConeFeature::writeParameters(QJsonObject& object) const
{
    object.insert("bottomRadius", bottomRadius_);
    object.insert("topRadius", topRadius_);
    object.insert("height", height_);
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

std::vector<FeatureProperty> SphereFeature::properties() const
{
    return {numericProperty("radius", "Radius", radius_)};
}

bool SphereFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key != "radius") return false;
    setRadius(value);
    return true;
}

void SphereFeature::writeParameters(QJsonObject& object) const
{
    object.insert("radius", radius_);
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

std::vector<FeatureProperty> TorusFeature::properties() const
{
    return {numericProperty("majorRadius", "Major Radius", majorRadius_),
            numericProperty("minorRadius", "Minor Radius", minorRadius_)};
}

bool TorusFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "majorRadius") setMajorRadius(value);
    else if (key == "minorRadius") setMinorRadius(value);
    else return false;
    return true;
}

void TorusFeature::writeParameters(QJsonObject& object) const
{
    object.insert("majorRadius", majorRadius_);
    object.insert("minorRadius", minorRadius_);
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

std::vector<FeatureProperty> HexagonFeature::properties() const
{
    return {numericProperty("acrossFlats", "Across Flats", acrossFlats_),
            numericProperty("height", "Height", height_)};
}

bool HexagonFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "acrossFlats") setAcrossFlats(value);
    else if (key == "height") setHeight(value);
    else return false;
    return true;
}

void HexagonFeature::writeParameters(QJsonObject& object) const
{
    object.insert("acrossFlats", acrossFlats_);
    object.insert("height", height_);
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

std::vector<FeatureProperty> ExtrudeFeature::properties() const
{
    return {numericProperty("length", "Length", vector_.Magnitude())};
}

bool ExtrudeFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key != "length" || vector_.Magnitude() == 0.0) return false;
    setVector(vector_.Normalized() * value);
    return true;
}

void ExtrudeFeature::writeParameters(QJsonObject& object) const
{
    object.insert("sourceFeatureId", QString::fromStdString(profile_->id()));
    object.insert("vectorX", vector_.X());
    object.insert("vectorY", vector_.Y());
    object.insert("vectorZ", vector_.Z());
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

PushPullFeature::PushPullFeature(
    std::string id,
    const Ptr& source,
    const int faceIndex,
    gp_Vec normal,
    const double distance
)
    : ParametricFeature(std::move(id), "Push/Pull"),
      sourceFeatureId_(source ? source->id() : std::string{}),
      source_(source),
      faceIndex_(faceIndex),
      normal_(std::move(normal)),
      distance_(distance)
{
    requireFeature(source, "Push/Pull source");
    if (faceIndex_ <= 0) {
        throw std::invalid_argument("Push/Pull face index must be positive");
    }
    if (normal_.Magnitude() <= 1.0e-9) {
        throw std::invalid_argument("Push/Pull normal must not be zero");
    }
    if (!std::isfinite(distance_) || std::abs(distance_) <= 1.0e-9) {
        throw std::invalid_argument("Push/Pull distance must not be zero");
    }
    normal_.Normalize();
    addDependency(source);
}

std::vector<FeatureProperty> PushPullFeature::properties() const
{
    return {textProperty("sourceFeatureId", "Source", sourceFeatureId_),
            {"faceIndex", "Face", faceIndex_, std::nullopt, std::nullopt, false},
            numericProperty("distance", "Distance", std::abs(distance_))};
}

bool PushPullFeature::setNumericProperty(
    const std::string& key,
    const double value
)
{
    if (key != "distance" || !std::isfinite(value) || value <= 0.001) {
        return false;
    }
    distance_ = distance_ < 0.0 ? -value : value;
    markDirty();
    return true;
}

std::vector<std::string> PushPullFeature::hiddenDependencyIds() const
{
    return {sourceFeatureId_};
}

ParametricFeature::Ptr PushPullFeature::source() const
{
    return source_.lock();
}

const std::string& PushPullFeature::sourceFeatureId() const noexcept
{
    return sourceFeatureId_;
}

int PushPullFeature::faceIndex() const noexcept
{
    return faceIndex_;
}

const gp_Vec& PushPullFeature::normal() const noexcept
{
    return normal_;
}

double PushPullFeature::distance() const noexcept
{
    return distance_;
}

TopoDS_Shape PushPullFeature::build() const
{
    const auto source = source_.lock();
    requireFeature(source, "Push/Pull source");

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(source->shape(), TopAbs_FACE, faces);
    if (faceIndex_ > faces.Extent()) {
        throw std::runtime_error("Push/Pull face reference is no longer valid");
    }

    const TopoDS_Face face = TopoDS::Face(faces.FindKey(faceIndex_));
    const TopoDS_Shape prism =
        BRepPrimAPI_MakePrism(face, normal_ * distance_).Shape();

    if (distance_ > 0.0) {
        BRepAlgoAPI_Fuse fuse(source->shape(), prism);
        fuse.Build();
        if (!fuse.IsDone()) throw std::runtime_error("Push/Pull fuse failed");
        return fuse.Shape();
    }

    BRepAlgoAPI_Cut cut(source->shape(), prism);
    cut.Build();
    if (!cut.IsDone()) throw std::runtime_error("Push/Pull cut failed");
    return cut.Shape();
}

void PushPullFeature::writeParameters(QJsonObject& object) const
{
    object.insert("sourceFeatureId", QString::fromStdString(sourceFeatureId_));
    object.insert("faceIndex", faceIndex_);
    object.insert("normalX", normal_.X());
    object.insert("normalY", normal_.Y());
    object.insert("normalZ", normal_.Z());
    object.insert("distance", distance_);
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

std::vector<FeatureProperty> BooleanFeature::properties() const
{
    const char* operationName = operation_ == BooleanOperation::Fuse ? "Fuse"
        : operation_ == BooleanOperation::Cut ? "Cut" : "Common";
    return {textProperty("operation", "Operation", operationName),
            textProperty("left", "Left", left_->name()),
            textProperty("right", "Right", right_->name())};
}

std::string BooleanFeature::creationLabel() const
{
    return operation_ == BooleanOperation::Fuse ? "Boolean Fuse"
        : operation_ == BooleanOperation::Cut ? "Boolean Cut" : "Boolean Common";
}

std::vector<std::string> BooleanFeature::hiddenDependencyIds() const
{
    if (operation_ != BooleanOperation::Cut) return {};
    return {left_->id(), right_->id()};
}

void BooleanFeature::writeParameters(QJsonObject& object) const
{
    object.insert("left", QString::fromStdString(left_->id()));
    object.insert("right", QString::fromStdString(right_->id()));
    object.insert("operation", operation_ == BooleanOperation::Fuse ? "Fuse"
        : operation_ == BooleanOperation::Cut ? "Cut" : "Common");
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
