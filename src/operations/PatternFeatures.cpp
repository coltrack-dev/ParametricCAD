#include "operations/PatternFeatures.h"

#include <BRepAdaptor_CompCurve.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Builder.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>

#include <QJsonArray>

#include <cmath>
#include <stdexcept>

namespace cad::parametric {

namespace {

FeatureProperty numberProperty(const char* key, const char* label, double value)
{
    return {key, label, value, std::nullopt, std::nullopt, true};
}

FeatureProperty integerProperty(const char* key, const char* label, int value)
{
    return {key, label, value, 1.0, 1'000'000.0, true};
}

FeatureProperty booleanProperty(const char* key, const char* label, bool value)
{
    return {key, label, value, std::nullopt, std::nullopt, true};
}

FeatureProperty textProperty(const char* key, const char* label, std::string value)
{
    return {key, label, std::move(value), std::nullopt, std::nullopt, false};
}

FeatureProperty enumProperty(const char* key, const char* label, std::string value)
{
    return {key, label, std::move(value), std::nullopt, std::nullopt, true};
}

void addShape(BRep_Builder& builder, TopoDS_Compound& compound, const TopoDS_Shape& shape)
{
    if (!shape.IsNull()) builder.Add(compound, shape);
}

TopoDS_Shape transformed(const TopoDS_Shape& shape, const gp_Trsf& transform)
{
    return BRepBuilderAPI_Transform(shape, transform, true).Shape();
}

std::string distributionName(const PathPatternDistribution distribution)
{
    return distribution == PathPatternDistribution::FixedSpacing
        ? "FixedSpacing" : "FitCount";
}

std::string orientationName(const PathPatternOrientation orientation)
{
    return orientation == PathPatternOrientation::Fixed ? "Fixed" : "Tangent";
}

PathPatternDistribution distributionFrom(const std::string& value)
{
    if (value == "FixedSpacing") return PathPatternDistribution::FixedSpacing;
    if (value == "FitCount") return PathPatternDistribution::FitCount;
    throw std::invalid_argument("Unknown path pattern distribution");
}

PathPatternOrientation orientationFrom(const std::string& value)
{
    if (value == "Fixed") return PathPatternOrientation::Fixed;
    if (value == "Tangent") return PathPatternOrientation::Tangent;
    throw std::invalid_argument("Unknown path pattern orientation");
}

TopoDS_Wire pathWire(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return {};
    if (shape.ShapeType() == TopAbs_WIRE) return TopoDS::Wire(shape);
    if (shape.ShapeType() == TopAbs_EDGE) {
        BRep_Builder builder;
        TopoDS_Wire wire;
        builder.MakeWire(wire);
        builder.Add(wire, TopoDS::Edge(shape));
        return wire;
    }
    return {};
}

} // namespace

LinearPatternFeature::LinearPatternFeature(
    std::string id,
    std::vector<Ptr> sources,
    gp_Vec direction,
    const double spacing,
    const int count,
    const bool includeSource
)
    : ParametricFeature(std::move(id), "Linear Pattern"),
      direction_(direction), spacing_(spacing), count_(count), includeSource_(includeSource)
{
    for (const auto& source : sources) {
        if (!source) throw std::invalid_argument("Linear pattern source is missing");
        sourceIds_.push_back(source->id());
        sources_.push_back(source);
        addDependency(source);
    }
    if (sourceIds_.empty()) throw std::invalid_argument("Linear pattern needs a source");
}

std::vector<FeatureProperty> LinearPatternFeature::properties() const
{
    return {
        textProperty("sourceIds", "Sources", [&]() {
            std::string result;
            for (std::size_t i = 0; i < sourceIds_.size(); ++i) {
                if (i) result += ",";
                result += sourceIds_[i];
            }
            return result;
        }()),
        numberProperty("directionX", "Direction X", direction_.X()),
        numberProperty("directionY", "Direction Y", direction_.Y()),
        numberProperty("directionZ", "Direction Z", direction_.Z()),
        numberProperty("spacing", "Spacing", spacing_),
        integerProperty("count", "Count", count_),
        booleanProperty("includeSource", "Include Source", includeSource_)
    };
}

std::vector<std::string> LinearPatternFeature::hiddenDependencyIds() const
{
    return sourceIds_;
}

bool LinearPatternFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "directionX") direction_.SetX(value);
    else if (key == "directionY") direction_.SetY(value);
    else if (key == "directionZ") direction_.SetZ(value);
    else if (key == "spacing") spacing_ = value;
    else if (key == "count") count_ = static_cast<int>(std::llround(value));
    else return false;
    markDirty();
    return true;
}

bool LinearPatternFeature::setProperty(const std::string& key, const PropertyValue& value)
{
    if (key == "includeSource" && std::holds_alternative<bool>(value)) {
        includeSource_ = std::get<bool>(value);
        markDirty();
        return true;
    }
    if (std::holds_alternative<double>(value)) return setNumericProperty(key, std::get<double>(value));
    if (std::holds_alternative<int>(value)) return setNumericProperty(key, std::get<int>(value));
    return false;
}

TopoDS_Shape LinearPatternFeature::build() const
{
    if (count_ < 1) throw std::invalid_argument("Linear pattern count must be at least one");
    if (spacing_ <= 0.0) throw std::invalid_argument("Linear pattern spacing must be positive");
    if (direction_.Magnitude() <= 1.0e-9) throw std::invalid_argument("Linear pattern direction is invalid");

    BRep_Builder builder;
    TopoDS_Compound result;
    builder.MakeCompound(result);
    const gp_Vec step = direction_.Normalized() * spacing_;
    const int first = includeSource_ ? 0 : 1;
    for (const auto& weak : sources_) {
        const auto source = weak.lock();
        if (!source || source->shape().IsNull()) throw std::runtime_error("Linear pattern source is unavailable");
        for (int index = first; index < first + count_; ++index) {
            gp_Trsf transform;
            transform.SetTranslation(step * index);
            addShape(builder, result, transformed(source->shape(), transform));
        }
    }
    return result;
}

void LinearPatternFeature::writeParameters(QJsonObject& object) const
{
    QJsonArray sources;
    for (const auto& sourceId : sourceIds_) sources.append(QString::fromStdString(sourceId));
    object.insert("sourceIds", sources);
    object.insert("directionX", direction_.X());
    object.insert("directionY", direction_.Y());
    object.insert("directionZ", direction_.Z());
    object.insert("spacing", spacing_);
    object.insert("count", count_);
    object.insert("includeSource", includeSource_);
}

ParametricFeature::Ptr LinearPatternFeature::clone(std::string newId) const
{
    std::vector<Ptr> sources;
    for (const auto& weak : sources_) sources.push_back(weak.lock());
    auto copy = std::make_shared<LinearPatternFeature>(
        std::move(newId), sources, direction_, spacing_, count_, includeSource_);
    copyPlacementTo(copy);
    return copy;
}

PathPatternFeature::PathPatternFeature(
    std::string id,
    std::vector<Ptr> sources,
    const Ptr& path,
    const double spacing,
    const int count,
    const PathPatternDistribution distribution,
    const PathPatternOrientation orientation,
    const double startOffset,
    const double endOffset,
    const bool includeSource
)
    : ParametricFeature(std::move(id), "Path Pattern"),
      pathId_(path ? path->id() : std::string{}), path_(path), spacing_(spacing), count_(count),
      distribution_(distribution), orientation_(orientation), startOffset_(startOffset),
      endOffset_(endOffset), includeSource_(includeSource)
{
    if (!path) throw std::invalid_argument("Path pattern path is missing");
    addDependency(path);
    for (const auto& source : sources) {
        if (!source) throw std::invalid_argument("Path pattern source is missing");
        sourceIds_.push_back(source->id());
        sources_.push_back(source);
        addDependency(source);
    }
    if (sourceIds_.empty()) throw std::invalid_argument("Path pattern needs a source");
}

std::vector<FeatureProperty> PathPatternFeature::properties() const
{
    std::string sources;
    for (std::size_t i = 0; i < sourceIds_.size(); ++i) {
        if (i) sources += ",";
        sources += sourceIds_[i];
    }
    return {
        textProperty("sourceIds", "Sources", sources),
        textProperty("pathId", "Path", pathId_),
        numberProperty("spacing", "Spacing", spacing_),
        integerProperty("count", "Count", count_),
        enumProperty("distribution", "Distribution", distributionName(distribution_)),
        enumProperty("orientation", "Orientation", orientationName(orientation_)),
        numberProperty("startOffset", "Start Offset", startOffset_),
        numberProperty("endOffset", "End Offset", endOffset_),
        booleanProperty("includeSource", "Include Source", includeSource_)
    };
}

std::vector<std::string> PathPatternFeature::hiddenDependencyIds() const
{
    auto result = sourceIds_;
    result.push_back(pathId_);
    return result;
}

bool PathPatternFeature::setNumericProperty(const std::string& key, const double value)
{
    if (key == "spacing") spacing_ = value;
    else if (key == "count") count_ = static_cast<int>(std::llround(value));
    else if (key == "startOffset") startOffset_ = value;
    else if (key == "endOffset") endOffset_ = value;
    else return false;
    markDirty();
    return true;
}

bool PathPatternFeature::setProperty(const std::string& key, const PropertyValue& value)
{
    if (key == "includeSource" && std::holds_alternative<bool>(value)) {
        includeSource_ = std::get<bool>(value);
        markDirty();
        return true;
    }
    if (key == "distribution" && std::holds_alternative<std::string>(value)) {
        distribution_ = distributionFrom(std::get<std::string>(value));
        markDirty();
        return true;
    }
    if (key == "orientation" && std::holds_alternative<std::string>(value)) {
        orientation_ = orientationFrom(std::get<std::string>(value));
        markDirty();
        return true;
    }
    if (std::holds_alternative<double>(value)) return setNumericProperty(key, std::get<double>(value));
    if (std::holds_alternative<int>(value)) return setNumericProperty(key, std::get<int>(value));
    return false;
}

TopoDS_Shape PathPatternFeature::build() const
{
    if (spacing_ <= 0.0) throw std::invalid_argument("Path pattern spacing must be positive");
    if (count_ < 1) throw std::invalid_argument("Path pattern count must be at least one");
    const auto path = path_.lock();
    if (!path || path->shape().IsNull()) throw std::runtime_error("Path pattern path is unavailable");
    const TopoDS_Wire pathShape = pathWire(path->shape());
    if (pathShape.IsNull()) throw std::invalid_argument("Path pattern requires an Edge or Wire path feature");

    BRepAdaptor_CompCurve curve(pathShape, Standard_True);
    const double totalLength = GCPnts_AbscissaPoint::Length(curve);
    if (totalLength <= 1.0e-9 || startOffset_ < 0.0 || endOffset_ < 0.0
        || startOffset_ + endOffset_ >= totalLength) {
        throw std::invalid_argument("Path pattern offsets are outside the path");
    }

    const double usableLength = totalLength - startOffset_ - endOffset_;
    std::vector<double> distances;
    if (distribution_ == PathPatternDistribution::FitCount) {
        if (count_ == 1) distances.push_back(startOffset_);
        else {
            for (int index = 0; index < count_; ++index) {
                distances.push_back(startOffset_ + usableLength * index / (count_ - 1));
            }
        }
    } else {
        for (int index = includeSource_ ? 0 : 1; index < count_ + (includeSource_ ? 0 : 1); ++index) {
            const double distance = startOffset_ + spacing_ * index;
            if (distance > totalLength - endOffset_ + 1.0e-9) break;
            distances.push_back(distance);
        }
        if (distances.empty()) throw std::invalid_argument("Path pattern has no instances in the path range");
    }

    const Standard_Real firstParameter = curve.FirstParameter();
    const gp_Pnt startPoint = curve.Value(firstParameter);
    BRep_Builder builder;
    TopoDS_Compound result;
    builder.MakeCompound(result);
    for (const auto& weak : sources_) {
        const auto source = weak.lock();
        if (!source || source->shape().IsNull()) throw std::runtime_error("Path pattern source is unavailable");
        for (const double distance : distances) {
            const GCPnts_AbscissaPoint abscissa(curve, distance, firstParameter);
            if (!abscissa.IsDone()) throw std::runtime_error("Could not evaluate path arc length");
            const Standard_Real parameter = abscissa.Parameter();
            const gp_Pnt point = curve.Value(parameter);
            gp_Trsf transform;
            transform.SetTranslation(gp_Vec(startPoint, point));
            if (orientation_ == PathPatternOrientation::Tangent) {
                gp_Pnt tangentPoint;
                gp_Vec tangent;
                curve.D1(parameter, tangentPoint, tangent);
                if (tangent.Magnitude() <= 1.0e-9) throw std::runtime_error("Invalid path tangent");
                tangent.Normalize();
                const gp_Vec localX(1.0, 0.0, 0.0);
                const gp_Vec rotationAxis = localX ^ tangent;
                gp_Vec stableAxis = rotationAxis;
                double angle = localX.Angle(tangent);
                if (stableAxis.Magnitude() <= 1.0e-9 && localX.Dot(tangent) < 0.0) {
                    stableAxis = gp_Vec(0.0, 0.0, 1.0);
                    angle = std::acos(-1.0);
                }
                if (stableAxis.Magnitude() > 1.0e-9) {
                    gp_Trsf rotation;
                    rotation.SetRotation(gp_Ax1(startPoint, gp_Dir(stableAxis)), angle);
                    transform.Multiply(rotation);
                }
            }
            addShape(builder, result, transformed(source->shape(), transform));
        }
    }
    return result;
}

void PathPatternFeature::writeParameters(QJsonObject& object) const
{
    QJsonArray sources;
    for (const auto& sourceId : sourceIds_) sources.append(QString::fromStdString(sourceId));
    object.insert("sourceIds", sources);
    object.insert("pathId", QString::fromStdString(pathId_));
    object.insert("spacing", spacing_);
    object.insert("count", count_);
    object.insert("distribution", QString::fromStdString(distributionName(distribution_)));
    object.insert("orientation", QString::fromStdString(orientationName(orientation_)));
    object.insert("startOffset", startOffset_);
    object.insert("endOffset", endOffset_);
    object.insert("includeSource", includeSource_);
}

ParametricFeature::Ptr PathPatternFeature::clone(std::string newId) const
{
    std::vector<Ptr> sources;
    for (const auto& weak : sources_) sources.push_back(weak.lock());
    auto copy = std::make_shared<PathPatternFeature>(
        std::move(newId), sources, path_.lock(), spacing_, count_, distribution_,
        orientation_, startOffset_, endOffset_, includeSource_);
    copyPlacementTo(copy);
    return copy;
}

} // namespace cad::parametric
