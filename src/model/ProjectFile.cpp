#include "model/ProjectFile.h"
#include "operations/SketchConstraintSolver.h"
#include "model/Feature.h"
#include "model/Document.h"
#include "model/Body.h"
#include "operations/BoxFeature.h"
#include "operations/CylinderFeature.h"
#include "operations/ParametricFeatures.h"
#include "operations/PatternFeatures.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <Standard_Failure.hxx>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace {
using namespace cad::parametric;

std::string legacyFeatureId(const Feature& feature, const int index)
{
    return std::string(feature.legacyIdPrefix()) + std::to_string(index);
}

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
double number(const QJsonObject& o, const char* key)
{
    const auto v = o.value(QLatin1String(key));
    require(v.isDouble() && std::isfinite(v.toDouble()), "Invalid numeric parameter");
    return v.toDouble();
}
std::string string(const QJsonObject& o, const char* key)
{
    const auto v = o.value(QLatin1String(key));
    require(v.isString(), "Invalid string field");
    return v.toString().toStdString();
}
bool boolean(const QJsonObject& o, const char* key)
{
    const auto v = o.value(QLatin1String(key));
    require(v.isBool(), "Invalid boolean field");
    return v.toBool();
}
std::vector<std::string> strings(const QJsonObject& o, const char* key)
{
    const auto array = o.value(QLatin1String(key)).toArray();
    require(!array.isEmpty(), "Invalid string array");
    std::vector<std::string> result;
    for (const auto& value : array) {
        require(value.isString(), "Invalid string array item");
        result.push_back(value.toString().toStdString());
    }
    return result;
}
std::vector<int> positiveIntegers(const QJsonObject& o, const char* key)
{
    const auto array = o.value(QLatin1String(key)).toArray();
    require(!array.isEmpty(), "Invalid integer array");
    std::vector<int> result;
    for (const auto& value : array) {
        require(value.isDouble() && std::isfinite(value.toDouble())
                    && value.toDouble() >= 1.0
                    && std::floor(value.toDouble()) == value.toDouble(),
                "Invalid positive integer array item");
        result.push_back(value.toInt());
    }
    return result;
}

std::vector<cad::topology::TopologicalReference> topologicalReferences(
    const QJsonObject& o, const char* key, const std::string& sourceId)
{
    const auto array = o.value(QLatin1String(key)).toArray();
    require(!array.isEmpty(), "Invalid topological reference array");
    std::vector<cad::topology::TopologicalReference> result;
    for (const auto& value : array) {
        require(value.isObject(), "Invalid topological reference item");
        auto reference = cad::topology::topologicalReferenceFromJson(value.toObject());
        require(reference.featureId == sourceId
                    && reference.kind == cad::topology::TopologicalKind::Edge,
                "Invalid Fillet/Chamfer edge reference");
        result.push_back(std::move(reference));
    }
    return result;
}

std::vector<SketchEntity> sketchEntities(const QJsonObject& object)
{
    std::vector<SketchEntity> result;
    for (const auto& value : object.value("entities").toArray()) {
        require(value.isObject(), "Invalid sketch entity");
        const auto entity = value.toObject();
        const auto type = string(entity, "type");
        if (type == "Line") {
            result.push_back(SketchLine{
                {entity.value("start").toArray().at(0).toDouble(),
                 entity.value("start").toArray().at(1).toDouble()},
                {entity.value("end").toArray().at(0).toDouble(),
                 entity.value("end").toArray().at(1).toDouble()},
                entity.value("id").toString().toStdString()});
        } else if (type == "Circle") {
            result.push_back(SketchCircle{
                {entity.value("center").toArray().at(0).toDouble(),
                 entity.value("center").toArray().at(1).toDouble()},
                number(entity, "radius"), entity.value("id").toString().toStdString()});
        } else if (type == "Arc") {
            result.push_back(SketchArc{
                {entity.value("center").toArray().at(0).toDouble(),
                 entity.value("center").toArray().at(1).toDouble()},
                number(entity, "radius"),
                number(entity, "startAngle"),
                number(entity, "endAngle"),
                boolean(entity, "clockwise"), entity.value("id").toString().toStdString()});
        } else {
            throw std::invalid_argument("Unsupported sketch entity type");
        }
    }
    return result;
}

SketchPointRole pointRole(const std::string& value)
{
    if (value == "LineStart") return SketchPointRole::LineStart;
    if (value == "LineEnd") return SketchPointRole::LineEnd;
    if (value == "ArcStart") return SketchPointRole::ArcStart;
    if (value == "ArcEnd") return SketchPointRole::ArcEnd;
    if (value == "CircleCenter") return SketchPointRole::CircleCenter;
    if (value == "ArcCenter") return SketchPointRole::ArcCenter;
    throw std::invalid_argument("Invalid Sketch point role");
}

std::vector<SketchConstraint> sketchConstraints(const QJsonObject& object)
{
    std::vector<SketchConstraint> result;
    for (const auto& value : object.value("constraints").toArray()) {
        require(value.isObject(), "Invalid Sketch constraint");
        const auto constraint = value.toObject();
        const auto type = string(constraint, "type");
        if (type == "Coincident") {
            result.push_back(CoincidentConstraint{
                {string(constraint, "aEntityId"), pointRole(string(constraint, "aRole"))},
                {string(constraint, "bEntityId"), pointRole(string(constraint, "bRole"))},
                constraint.value("id").toString().toStdString()});
        } else if (type == "Horizontal") {
            result.push_back(HorizontalConstraint{string(constraint, "entityId"),
                boolean(constraint, "anchorStart"), constraint.value("id").toString().toStdString()});
        } else if (type == "Vertical") {
            result.push_back(VerticalConstraint{string(constraint, "entityId"),
                boolean(constraint, "anchorStart"), constraint.value("id").toString().toStdString()});
        } else if (type == "Distance") {
            result.push_back(DistanceConstraint{string(constraint, "entityId"),
                number(constraint, "value"), boolean(constraint, "anchorStart"),
                constraint.value("id").toString().toStdString()});
        } else if (type == "Radius") {
            result.push_back(RadiusConstraint{string(constraint, "entityId"),
                number(constraint, "value"), constraint.value("id").toString().toStdString()});
        } else if (type == "HorizontalDistance" || type == "VerticalDistance") {
            const SketchPointRef first{string(constraint, "firstEntityId"),
                pointRole(string(constraint, "firstRole"))};
            const SketchPointRef second{string(constraint, "secondEntityId"),
                pointRole(string(constraint, "secondRole"))};
            if (type == "HorizontalDistance")
                result.push_back(HorizontalDistanceConstraint{first, second, number(constraint, "value"),
                    constraint.value("id").toString().toStdString()});
            else
                result.push_back(VerticalDistanceConstraint{first, second, number(constraint, "value"),
                    constraint.value("id").toString().toStdString()});
        } else if (type == "Angle") {
            result.push_back(AngleConstraint{string(constraint, "entityId"),
                number(constraint, "valueRadians"), boolean(constraint, "anchorStart"),
                constraint.value("id").toString().toStdString()});
        } else if (type == "Parallel") {
            result.push_back(ParallelConstraint{string(constraint, "firstLineId"),
                string(constraint, "secondLineId"), boolean(constraint, "anchorStart"),
                constraint.value("id").toString().toStdString()});
        } else if (type == "Perpendicular") {
            result.push_back(PerpendicularConstraint{string(constraint, "firstLineId"),
                string(constraint, "secondLineId"), boolean(constraint, "anchorStart"),
                constraint.value("id").toString().toStdString()});
        } else {
            throw std::invalid_argument("Unsupported Sketch constraint type");
        }
    }
    return result;
}
QJsonObject encode(const ParametricFeature::Ptr& feature, const Body& preceding)
{
    for (const auto& weak : feature->dependencies()) {
        const auto dependency = weak.lock();
        require(dependency && preceding.findFeature(dependency->id()) == dependency,
                "Feature references a missing or forward dependency");
    }
    return feature->serialize();
}

using FeatureFactory = std::function<ParametricFeature::Ptr(const QJsonObject&, const Body&)>;

const std::unordered_map<std::string, FeatureFactory>& factories()
{
    static const std::unordered_map<std::string, FeatureFactory> registry{
        {"Sketch", [](const QJsonObject& o, const Body& body) {
            const auto support = o.value("supportType").toString(
                o.value("plane").toString("XY"));
            if (support == "Face") {
                const auto reference = cad::topology::topologicalReferenceFromJson(
                    o.value("supportReference").toObject());
                const auto source = body.findFeature(reference.featureId);
                require(source && reference.kind == cad::topology::TopologicalKind::Face,
                        "Sketch references missing or invalid support face");
                auto sketch = std::make_shared<SketchFeature>(string(o, "id"), source,
                    reference, sketchEntities(o));
                sketch->setConstraints(sketchConstraints(o));
                const auto solved = cad::operations::SketchConstraintSolver::solve(
                    sketch->entities(), sketch->constraints());
                if (solved.status != cad::operations::SolveStatus::Solved)
                    throw std::runtime_error(solved.error);
                sketch->replaceEntities(0, sketch->entityCount(), solved.entities);
                return sketch;
            }
            const auto supportType = support == "XZ" ? SketchSupportType::XZ
                : support == "YZ" ? SketchSupportType::YZ : SketchSupportType::XY;
            auto sketch = std::make_shared<SketchFeature>(string(o, "id"), supportType,
                number(o, "width"), number(o, "height"), sketchEntities(o));
            sketch->setConstraints(sketchConstraints(o));
            const auto solved = cad::operations::SketchConstraintSolver::solve(
                sketch->entities(), sketch->constraints());
            if (solved.status != cad::operations::SolveStatus::Solved)
                throw std::runtime_error(solved.error);
            sketch->replaceEntities(0, sketch->entityCount(), solved.entities);
            return sketch;
        }},
        {"Face", [](const QJsonObject& o, const Body& body) {
            const auto sourceId = string(o, "sourceFeatureId");
            const auto source = body.findFeature(sourceId);
            if (!source || source->role() != FeatureRole::Sketch) {
                throw std::runtime_error("Face '" + string(o, "id")
                    + "' references missing, forward or non-Sketch source '"
                    + sourceId + "'");
            }
            return std::make_shared<FaceFeature>(string(o, "id"), source);
        }},
        {"Extrude", [](const QJsonObject& o, const Body& body) {
            if (o.contains("sourceSketchId")) {
                const auto source = body.findFeature(string(o, "sourceSketchId"));
                const auto sketch = std::dynamic_pointer_cast<SketchFeature>(source);
                require(static_cast<bool>(sketch), "Extrude references missing Sketch source");
                return std::make_shared<ExtrudeFeature>(string(o, "id"), sketch,
                    number(o, "distance"), boolean(o, "reversed"));
            }
            const auto source = body.findFeature(string(o, "sourceFeatureId"));
            require(static_cast<bool>(source), "Extrude references missing source");
            return std::make_shared<ExtrudeFeature>(string(o, "id"), source,
                gp_Vec(number(o, "vectorX"), number(o, "vectorY"), number(o, "vectorZ")));
        }},
        {"Pocket", [](const QJsonObject& o, const Body& body) {
            const auto target = body.findFeature(string(o, "targetFeatureId"));
            const auto sketch = std::dynamic_pointer_cast<SketchFeature>(
                body.findFeature(string(o, "sourceSketchId")));
            require(target && sketch, "Pocket references missing target or Sketch");
            return std::make_shared<PocketFeature>(string(o, "id"), target, sketch,
                number(o, "depth"));
        }},
        {"PushPull", [](const QJsonObject& o, const Body& body) {
            const auto source = body.findFeature(string(o, "sourceFeatureId"));
            require(static_cast<bool>(source), "PushPull references missing source");
            const double faceIndexValue = number(o, "faceIndex");
            require(faceIndexValue >= 1.0 && std::floor(faceIndexValue) == faceIndexValue,
                    "Invalid PushPull face index");
            return std::make_shared<PushPullFeature>(string(o, "id"), source,
                static_cast<int>(faceIndexValue),
                gp_Vec(number(o, "normalX"), number(o, "normalY"), number(o, "normalZ")),
                number(o, "distance"));
        }},
        {"Box", [](const QJsonObject& o, const Body&) {
            return std::make_shared<BoxParametricFeature>(string(o,"id"), number(o,"width"), number(o,"depth"), number(o,"height"));
        }},
        {"Cylinder", [](const QJsonObject& o, const Body&) {
            return std::make_shared<CylinderParametricFeature>(string(o,"id"), number(o,"radius"), number(o,"height"));
        }},
        {"Cone", [](const QJsonObject& o, const Body&) {
            return std::make_shared<ConeFeature>(string(o,"id"), number(o,"bottomRadius"), number(o,"topRadius"), number(o,"height"));
        }},
        {"Sphere", [](const QJsonObject& o, const Body&) {
            return std::make_shared<SphereFeature>(string(o,"id"), number(o,"radius"));
        }},
        {"Torus", [](const QJsonObject& o, const Body&) {
            return std::make_shared<TorusFeature>(string(o,"id"), number(o,"majorRadius"), number(o,"minorRadius"));
        }},
        {"Hexagon", [](const QJsonObject& o, const Body&) {
            return std::make_shared<HexagonFeature>(string(o,"id"), number(o,"acrossFlats"), number(o,"height"));
        }},
        {"Boolean", [](const QJsonObject& o, const Body& body) {
            const auto left = body.findFeature(string(o,"left"));
            const auto right = body.findFeature(string(o,"right"));
            require(left && right, "Missing or forward boolean reference");
            const auto operation = string(o,"operation");
            require(operation == "Fuse" || operation == "Cut" || operation == "Common", "Unknown Boolean operation");
            const auto kind = operation == "Fuse" ? BooleanOperation::Fuse
                : operation == "Cut" ? BooleanOperation::Cut : BooleanOperation::Common;
            return std::make_shared<BooleanFeature>(string(o,"id"), left, right, kind);
        }},
        {"Fillet", [](const QJsonObject& o, const Body& body) {
            const auto source = body.findFeature(string(o, "sourceFeatureId"));
            require(static_cast<bool>(source), "Fillet references missing source");
            const auto sourceId = string(o, "sourceFeatureId");
            if (o.contains("topologicalReferences")) {
                return std::make_shared<FilletFeature>(string(o, "id"), source,
                    topologicalReferences(o, "topologicalReferences", sourceId), number(o, "radius"));
            }
            return std::make_shared<FilletFeature>(string(o, "id"), source,
                positiveIntegers(o, "edgeIndices"), number(o, "radius"));
        }},
        {"Chamfer", [](const QJsonObject& o, const Body& body) {
            const auto source = body.findFeature(string(o, "sourceFeatureId"));
            require(static_cast<bool>(source), "Chamfer references missing source");
            const auto sourceId = string(o, "sourceFeatureId");
            if (o.contains("topologicalReferences")) {
                return std::make_shared<ChamferFeature>(string(o, "id"), source,
                    topologicalReferences(o, "topologicalReferences", sourceId), number(o, "distance"));
            }
            return std::make_shared<ChamferFeature>(string(o, "id"), source,
                positiveIntegers(o, "edgeIndices"), number(o, "distance"));
        }},
        {"LinearPattern", [](const QJsonObject& o, const Body& body) {
            std::vector<ParametricFeature::Ptr> sources;
            for (const auto& sourceId : strings(o, "sourceIds")) {
                const auto source = body.findFeature(sourceId);
                require(static_cast<bool>(source), "LinearPattern references missing source");
                sources.push_back(source);
            }
            return std::make_shared<LinearPatternFeature>(
                string(o, "id"), sources,
                gp_Vec(number(o, "directionX"), number(o, "directionY"), number(o, "directionZ")),
                number(o, "spacing"), static_cast<int>(number(o, "count")),
                boolean(o, "includeSource"));
        }},
        {"PathPattern", [](const QJsonObject& o, const Body& body) {
            std::vector<ParametricFeature::Ptr> sources;
            for (const auto& sourceId : strings(o, "sourceIds")) {
                const auto source = body.findFeature(sourceId);
                require(static_cast<bool>(source), "PathPattern references missing source");
                sources.push_back(source);
            }
            const auto path = body.findFeature(string(o, "pathId"));
            require(static_cast<bool>(path), "PathPattern references missing path");
            const auto distribution = string(o, "distribution") == "FixedSpacing"
                ? PathPatternDistribution::FixedSpacing : PathPatternDistribution::FitCount;
            const auto orientation = string(o, "orientation") == "Fixed"
                ? PathPatternOrientation::Fixed : PathPatternOrientation::Tangent;
            return std::make_shared<PathPatternFeature>(
                string(o, "id"), sources, path, number(o, "spacing"),
                static_cast<int>(number(o, "count")), distribution, orientation,
                number(o, "startOffset"), number(o, "endOffset"), boolean(o, "includeSource"));
        }}
    };
    return registry;
}

ParametricFeature::Ptr decode(const QJsonObject& o, const Body& body)
{
    const auto id = string(o, "id");
    require(!id.empty(), "Empty feature id");
    const auto type = string(o, "type");
    const auto factory = factories().find(type);
    require(factory != factories().end(), "Unsupported parametric feature type");
    auto feature = factory->second(o, body);
    if (o.contains("placement")) {
        const auto values = o.value("placement").toArray();
        require(values.size() == 12, "Invalid feature placement");
        double matrix[12]{};
        for (int index = 0; index < 12; ++index) {
            require(values.at(index).isDouble()
                        && std::isfinite(values.at(index).toDouble()),
                    "Invalid feature placement value");
            matrix[index] = values.at(index).toDouble();
        }
        gp_Trsf placement;
        placement.SetValues(
            matrix[0], matrix[1], matrix[2], matrix[3],
            matrix[4], matrix[5], matrix[6], matrix[7],
            matrix[8], matrix[9], matrix[10], matrix[11]
        );
        feature->setPlacement(placement);
    }
    feature->setName(string(o, "name"));
    return feature;
}
}

bool ProjectFile::save(const QString& path, const Document& document,
                       const cad::parametric::Body& body, QString& error)
{
    error.clear();
    try {
        QJsonArray features;
        QJsonArray history;

        Body canonical;
        for (std::size_t index = 0; index < document.features().size(); ++index) {
            const auto& feature = document.features()[index];
            canonical.addFeature(feature->toParametricFeature(
                legacyFeatureId(*feature, static_cast<int>(index))));
        }
        for (const auto& feature : body.features()) canonical.addFeature(feature);

        Body preceding;
        for (const auto& feature : canonical.features()) {
            history.append(encode(feature, preceding));
            preceding.addFeature(feature);
        }
        const QByteArray data = QJsonDocument(QJsonObject{{"format", "ParametricCAD"}, {"version", 1},
            {"features", features}, {"body", history}}).toJson();
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            error = file.errorString(); return false;
        }
        return true;
    } catch (const Standard_Failure& e) { error = QString::fromUtf8(e.GetMessageString()); }
      catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
    return false;
}

bool ProjectFile::load(const QString& path, Document& document,
                       cad::parametric::Body& body, QString& error)
{
    error.clear();
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
        const auto data = file.readAll();
        if (file.error() != QFileDevice::NoError) { error = file.errorString(); return false; }
        QJsonParseError parseError;
        const auto json = QJsonDocument::fromJson(data, &parseError);
        require(parseError.error == QJsonParseError::NoError && json.isObject(), "Invalid project JSON");
        const auto root = json.object();
        require(string(root,"format") == "ParametricCAD" && number(root,"version") == 1, "Unsupported project format or version");
        require(root.value("features").isArray() && root.value("body").isArray(), "Missing feature arrays");
        Body loadedBody;
        const auto legacyFeatures = root.value("features").toArray();
        const std::unordered_map<std::string, std::function<ParametricFeature::Ptr(const QJsonObject&, const std::string&)>> legacyFactories{
            {"Box", [](const QJsonObject& o, const std::string& id) {
                return std::make_shared<BoxParametricFeature>(id, number(o,"width"), number(o,"depth"), number(o,"height"));
            }},
            {"Cylinder", [](const QJsonObject& o, const std::string& id) {
                return std::make_shared<CylinderParametricFeature>(id, number(o,"radius"), number(o,"height"));
            }}
        };
        for (int index = 0; index < legacyFeatures.size(); ++index) {
            const auto& value = legacyFeatures.at(index);
            require(value.isObject(), "Invalid feature entry");
            const auto o = value.toObject();
            const auto type = string(o,"type");
            const auto factory = legacyFactories.find(type);
            require(factory != legacyFactories.end(), "Unsupported document feature type");
            loadedBody.addFeature(factory->second(o, "legacy-" + QString::fromStdString(type).toLower().toStdString()
                + "-" + std::to_string(index)));
        }
        for (const auto& value : root.value("body").toArray()) {
            require(value.isObject(), "Invalid body feature entry");
            loadedBody.addFeature(decode(value.toObject(), loadedBody));
        }
        if (!loadedBody.recompute()) throw std::runtime_error(loadedBody.lastError());
        Document loaded;
        document = std::move(loaded);
        body = std::move(loadedBody);
        return true;
    } catch (const Standard_Failure& e) { error = QString::fromUtf8(e.GetMessageString()); }
      catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
    return false;
}
