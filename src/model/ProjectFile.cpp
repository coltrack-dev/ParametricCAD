#include "model/ProjectFile.h"
#include "operations/SketchConstraintSolver.h"
#include "model/Feature.h"
#include "model/Document.h"
#include "model/Body.h"
#include "application/VisibilityManager.h"
#include "operations/BoxFeature.h"
#include "operations/CylinderFeature.h"
#include "operations/ParametricFeatures.h"
#include "operations/PatternFeatures.h"
#include "operations/ImportedFeature.h"
#include "model/ShapePayload.h"
#include "model/ProjectArchive.h"
#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QElapsedTimer>
#include <QSaveFile>
#include <Standard_Failure.hxx>
#include <gp_Mat.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
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

QString geometryEntryPath(const std::string& featureId)
{
    const auto digest = QCryptographicHash::hash(
        QByteArray::fromStdString(featureId), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("geometry/%1.brep").arg(QString::fromLatin1(digest));
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

QString visibilityModeName(const cad::application::VisibilityMode mode)
{
    switch (mode) {
    case cad::application::VisibilityMode::Visible: return "visible";
    case cad::application::VisibilityMode::Ghosted: return "ghosted";
    case cad::application::VisibilityMode::Hidden: return "hidden";
    }
    throw std::invalid_argument("Invalid visibility mode");
}

cad::application::VisibilityMode visibilityMode(const QJsonObject& object)
{
    const auto value = string(object, "mode");
    if (value == "visible") return cad::application::VisibilityMode::Visible;
    if (value == "ghosted") return cad::application::VisibilityMode::Ghosted;
    if (value == "hidden") return cad::application::VisibilityMode::Hidden;
    throw std::invalid_argument("Invalid visibility group mode");
}

QString featureRoleName(const cad::parametric::FeatureRole role)
{
    switch (role) {
    case cad::parametric::FeatureRole::Generic: return "Generic";
    case cad::parametric::FeatureRole::Sketch: return "Sketch";
    case cad::parametric::FeatureRole::Face: return "Face";
    }
    throw std::invalid_argument("Invalid visibility role");
}

std::optional<cad::parametric::FeatureRole> featureRole(const QString& value)
{
    if (value == "Generic") return cad::parametric::FeatureRole::Generic;
    if (value == "Sketch") return cad::parametric::FeatureRole::Sketch;
    if (value == "Face") return cad::parametric::FeatureRole::Face;
    return {};
}

std::optional<cad::application::VisibilityCategory> visibilityCategory(const QString& value)
{
    for (const auto category : cad::application::visibilityCategories()) {
        if (value == cad::application::visibilityCategoryId(category)) return category;
    }
    return {};
}

cad::application::VisibilityFilterState visibilityFiltersObject(const QJsonObject& object)
{
    cad::application::VisibilityFilterState result;
    const auto types = object.value("types");
    if (!types.isUndefined()) {
        require(types.isArray(), "Invalid visibility type filters");
        for (const auto& value : types.toArray()) {
            require(value.isObject(), "Invalid visibility type filter");
            const auto item = value.toObject();
            result.typeModes.emplace(string(item, "typeId"), visibilityMode(item));
        }
    }
    const auto roles = object.value("roles");
    if (!roles.isUndefined()) {
        require(roles.isArray(), "Invalid visibility role filters");
        for (const auto& value : roles.toArray()) {
            require(value.isObject(), "Invalid visibility role filter");
            const auto item = value.toObject();
            const auto role = featureRole(QString::fromStdString(string(item, "role")));
            if (role) result.roleModes[*role] = visibilityMode(item);
        }
    }
    const auto categories = object.value("categories");
    if (!categories.isUndefined()) {
        require(categories.isArray(), "Invalid visibility category filters");
        for (const auto& value : categories.toArray()) {
            require(value.isObject(), "Invalid visibility category filter");
            const auto item = value.toObject();
            const auto category = visibilityCategory(QString::fromStdString(
                string(item, "category")));
            if (category) result.categoryModes[*category] = visibilityMode(item);
        }
    }
    return result;
}

cad::application::VisibilityFilterState visibilityFilters(const QJsonObject& root)
{
    const auto visibility = root.value("visibility");
    if (!visibility.isObject()) return {};
    const auto filters = visibility.toObject().value("filters");
    if (filters.isUndefined()) return {};
    require(filters.isObject(), "Invalid visibility filters");
    return visibilityFiltersObject(filters.toObject());
}

QJsonObject encodeVisibilityFilters(const cad::application::VisibilityFilterState& filters)
{
    QJsonArray types;
    for (const auto& [typeId, mode] : filters.typeModes) {
        types.append(QJsonObject{{"typeId", QString::fromStdString(typeId)},
            {"mode", visibilityModeName(mode)}});
    }
    QJsonArray roles;
    for (const auto& [role, mode] : filters.roleModes) {
        roles.append(QJsonObject{{"role", featureRoleName(role)},
            {"mode", visibilityModeName(mode)}});
    }
    QJsonArray categories;
    for (const auto& [category, mode] : filters.categoryModes) {
        categories.append(QJsonObject{{"category", visibilityCategoryId(category)},
            {"mode", visibilityModeName(mode)}});
    }
    return QJsonObject{{"types", types}, {"roles", roles}, {"categories", categories}};
}

QJsonArray pointArray(const gp_Pnt& point)
{
    return {point.X(), point.Y(), point.Z()};
}

QJsonArray directionArray(const gp_Dir& direction)
{
    return {direction.X(), direction.Y(), direction.Z()};
}

gp_Pnt pointValue(const QJsonObject& object, const char* key)
{
    const auto values = object.value(QLatin1String(key)).toArray();
    require(values.size() == 3, "Invalid point");
    for (const auto& value : values)
        require(value.isDouble() && std::isfinite(value.toDouble()), "Invalid point value");
    return gp_Pnt(values.at(0).toDouble(), values.at(1).toDouble(), values.at(2).toDouble());
}

gp_Dir directionValue(const QJsonObject& object, const char* key)
{
    const auto values = object.value(QLatin1String(key)).toArray();
    require(values.size() == 3, "Invalid direction");
    for (const auto& value : values)
        require(value.isDouble() && std::isfinite(value.toDouble()), "Invalid direction value");
    return gp_Dir(values.at(0).toDouble(), values.at(1).toDouble(), values.at(2).toDouble());
}

QJsonObject encodeVisibilityConfiguration(
    const cad::application::VisibilityConfiguration& configuration)
{
    QJsonArray groupModes;
    for (const auto& [groupId, mode] : configuration.groupModes) {
        groupModes.append(QJsonObject{{"groupId", QString::fromStdString(groupId)},
            {"mode", visibilityModeName(mode)}});
    }
    QJsonArray overrides;
    for (const auto& [featureId, visible] : configuration.featureVisibility) {
        overrides.append(QJsonObject{{"featureId", QString::fromStdString(featureId)},
            {"visible", visible}});
    }
    return QJsonObject{{"groupModes", groupModes},
        {"filters", encodeVisibilityFilters(configuration.filters)},
        {"featureOverrides", overrides}};
}

cad::application::VisibilityConfiguration visibilityConfiguration(
    const QJsonObject& object)
{
    cad::application::VisibilityConfiguration configuration;
    const auto groupModes = object.value("groupModes");
    if (!groupModes.isUndefined()) {
        require(groupModes.isArray(), "Invalid saved view group modes");
        for (const auto& value : groupModes.toArray()) {
            require(value.isObject(), "Invalid saved view group mode");
            const auto item = value.toObject();
            configuration.groupModes.emplace(string(item, "groupId"), visibilityMode(item));
        }
    }
    const auto filters = object.value("filters");
    if (!filters.isUndefined()) {
        require(filters.isObject(), "Invalid saved view filters");
        configuration.filters = visibilityFiltersObject(filters.toObject());
    }
    const auto overrides = object.value("featureOverrides");
    if (!overrides.isUndefined()) {
        require(overrides.isArray(), "Invalid saved view feature overrides");
        for (const auto& value : overrides.toArray()) {
            require(value.isObject(), "Invalid saved view feature override");
            const auto item = value.toObject();
            configuration.featureVisibility.emplace(
                string(item, "featureId"), boolean(item, "visible"));
        }
    }
    return configuration;
}

std::vector<cad::application::SavedView> savedViews(const QJsonObject& root)
{
    std::vector<cad::application::SavedView> result;
    const auto views = root.value("views");
    if (views.isUndefined()) return result;
    require(views.isArray(), "Invalid saved views");
    for (const auto& value : views.toArray()) {
        require(value.isObject(), "Invalid saved view");
        const auto object = value.toObject();
        cad::application::SavedView view{
            string(object, "id"), string(object, "name"),
            visibilityConfiguration(object), {}, {}, {}, false, 2, false, {}, false, {}, {}, {}, 1.0};
        const auto isolated = object.value("isolatedFeatureIds");
        if (!isolated.isUndefined()) {
            require(isolated.isArray(), "Invalid saved view isolation");
            for (const auto& id : isolated.toArray()) {
                require(id.isString(), "Invalid saved view isolated feature");
                view.isolatedFeatureIds.insert(id.toString().toStdString());
            }
        }
        const auto ghosted = object.value("ghostedSelectionIds");
        if (!ghosted.isUndefined()) {
            require(ghosted.isArray(), "Invalid saved view ghosting");
            for (const auto& id : ghosted.toArray()) {
                require(id.isString(), "Invalid saved view ghosted feature");
                view.ghostedSelectionIds.insert(id.toString().toStdString());
            }
        }
        const auto spatial = object.value("spatial");
        if (!spatial.isUndefined()) {
            require(spatial.isObject(), "Invalid saved view spatial state");
            const auto item = spatial.toObject();
            view.spatialRule.enabled = boolean(item, "enabled");
            view.spatialRule.min = pointValue(item, "min");
            view.spatialRule.max = pointValue(item, "max");
            view.spatialRule.outsideMode = visibilityMode(item);
        }
        const auto section = object.value("section");
        if (!section.isUndefined()) {
            require(section.isObject(), "Invalid saved view section state");
            const auto item = section.toObject();
            view.sectionActive = boolean(item, "active");
            view.sectionAxis = item.value("axis").toInt(2);
            view.sectionFlipped = item.value("flipped").toBool(false);
            view.sectionOrigin = pointValue(item, "origin");
        }
        const auto camera = object.value("camera");
        if (!camera.isUndefined()) {
            require(camera.isObject(), "Invalid saved view camera state");
            const auto item = camera.toObject();
            view.cameraValid = boolean(item, "valid");
            view.cameraEye = pointValue(item, "eye");
            view.cameraCenter = pointValue(item, "center");
            view.cameraUp = directionValue(item, "up");
            view.cameraScale = number(item, "scale");
            require(view.cameraScale > 0.0, "Invalid saved view camera scale");
        }
        result.push_back(std::move(view));
    }
    return result;
}

std::vector<cad::application::VisibilityPreset> visibilityPresets(const QJsonObject& root)
{
    std::vector<cad::application::VisibilityPreset> result;
    const auto visibility = root.value("visibility");
    if (!visibility.isObject()) return result;
    const auto presets = visibility.toObject().value("presets");
    if (presets.isUndefined()) return result;
    require(presets.isArray(), "Invalid visibility presets");
    for (const auto& value : presets.toArray()) {
        require(value.isObject(), "Invalid visibility preset");
        const auto object = value.toObject();
        cad::application::VisibilityPreset preset{
            string(object, "id"), string(object, "name"), {}};
        const auto groupModes = object.value("groupModes");
        if (!groupModes.isUndefined()) {
            require(groupModes.isArray(), "Invalid visibility preset group modes");
            for (const auto& modeValue : groupModes.toArray()) {
                require(modeValue.isObject(), "Invalid visibility preset group mode");
                const auto modeObject = modeValue.toObject();
                preset.configuration.groupModes.emplace(
                    string(modeObject, "groupId"), visibilityMode(modeObject));
            }
        }
        const auto filters = object.value("filters");
        if (!filters.isUndefined()) {
            require(filters.isObject(), "Invalid visibility preset filters");
            preset.configuration.filters = visibilityFiltersObject(filters.toObject());
        }
        const auto overrides = object.value("featureOverrides");
        if (!overrides.isUndefined()) {
            require(overrides.isArray(), "Invalid visibility preset feature overrides");
            for (const auto& overrideValue : overrides.toArray()) {
                require(overrideValue.isObject(), "Invalid visibility preset feature override");
                const auto overrideObject = overrideValue.toObject();
                preset.configuration.featureVisibility.emplace(
                    string(overrideObject, "featureId"), boolean(overrideObject, "visible"));
            }
        }
        result.push_back(std::move(preset));
    }
    return result;
}

std::vector<cad::application::VisibilityGroup> visibilityGroups(const QJsonObject& root)
{
    std::vector<cad::application::VisibilityGroup> result;
    const auto visibility = root.value("visibility");
    if (visibility.isUndefined()) return result;
    require(visibility.isObject(), "Invalid visibility metadata");
    const auto groups = visibility.toObject().value("groups");
    require(groups.isArray(), "Invalid visibility groups");
    for (const auto& value : groups.toArray()) {
        require(value.isObject(), "Invalid visibility group");
        const auto object = value.toObject();
        cad::application::VisibilityGroup group{
            string(object, "id"), string(object, "name"), {}, {}, visibilityMode(object)};
        const auto parent = object.value("parentId");
        if (!parent.isUndefined() && !parent.isNull()) {
            require(parent.isString(), "Invalid visibility group parent");
            group.parentId = parent.toString().toStdString();
        }
        const auto members = object.value("members");
        require(members.isArray(), "Invalid visibility group members");
        for (const auto& member : members.toArray()) {
            require(member.isString(), "Invalid visibility group member");
            group.memberFeatureIds.insert(member.toString().toStdString());
        }
        result.push_back(std::move(group));
    }
    return result;
}

gp_Trsf rigidPlacement(const double* values)
{
    // Placements are JSON doubles, and hand/generated files may round matrix
    // coefficients to a fixed number of decimal places. Keep the tolerance
    // large enough for that serialization noise, while still rejecting real
    // scale or shear components.
    constexpr double tolerance = 1.0e-8;
    const gp_Mat rotation(
        values[0], values[1], values[2],
        values[4], values[5], values[6],
        values[8], values[9], values[10]);

    const auto rowDot = [&rotation](const int first, const int second) {
        return rotation.Value(first, 1) * rotation.Value(second, 1)
            + rotation.Value(first, 2) * rotation.Value(second, 2)
            + rotation.Value(first, 3) * rotation.Value(second, 3);
    };
    for (int row = 1; row <= 3; ++row) {
        if (std::abs(rowDot(row, row) - 1.0) > tolerance) {
            throw std::invalid_argument("Feature placement is not a rigid rotation");
        }
        for (int other = row + 1; other <= 3; ++other) {
            if (std::abs(rowDot(row, other)) > tolerance) {
                throw std::invalid_argument("Feature placement is not a rigid rotation");
            }
        }
    }
    if (std::abs(rotation.Determinant() - 1.0) > tolerance) {
        throw std::invalid_argument("Feature placement is not a rigid rotation");
    }

    gp_Trsf placement;
    placement.SetTransformation(
        gp_Quaternion(rotation),
        gp_Vec(values[3], values[7], values[11]));
    return placement;
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

std::vector<cad::topology::TopologicalReference> faceTopologicalReferences(
    const QJsonObject& o, const char* key, const std::string& sourceId)
{
    const auto array = o.value(QLatin1String(key)).toArray();
    require(!array.isEmpty(), "Invalid face topological reference array");
    std::vector<cad::topology::TopologicalReference> result;
    for (const auto& value : array) {
        require(value.isObject(), "Invalid face topological reference item");
        auto reference = cad::topology::topologicalReferenceFromJson(value.toObject());
        require(reference.featureId == sourceId
                    && reference.kind == cad::topology::TopologicalKind::Face,
                "Invalid face topological reference");
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
                entity.value("id").toString().toStdString(),
                entity.value("construction").toBool(false)});
        } else if (type == "Circle") {
            result.push_back(SketchCircle{
                {entity.value("center").toArray().at(0).toDouble(),
                 entity.value("center").toArray().at(1).toDouble()},
                number(entity, "radius"), entity.value("id").toString().toStdString(),
                entity.value("construction").toBool(false)});
        } else if (type == "Arc") {
            result.push_back(SketchArc{
                {entity.value("center").toArray().at(0).toDouble(),
                 entity.value("center").toArray().at(1).toDouble()},
                number(entity, "radius"),
                number(entity, "startAngle"),
                number(entity, "endAngle"),
                boolean(entity, "clockwise"), entity.value("id").toString().toStdString(),
                entity.value("construction").toBool(false)});
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
        } else if (type == "AngleBetweenLines") {
            result.push_back(AngleBetweenLinesConstraint{string(constraint, "referenceLineId"),
                string(constraint, "dependentLineId"), number(constraint, "angleRadians"),
                boolean(constraint, "anchorStart"), constraint.value("id").toString().toStdString()});
        } else if (type == "Tangent") {
            result.push_back(TangentConstraint{string(constraint, "firstEntityId"),
                string(constraint, "secondEntityId"), constraint.value("id").toString().toStdString()});
        } else if (type == "Equal") {
            result.push_back(EqualConstraint{string(constraint, "referenceEntityId"),
                string(constraint, "dependentEntityId"), constraint.value("id").toString().toStdString()});
        } else if (type == "Chamfer") {
            const auto mode = constraint.value("mode").toString();
            const auto chamferMode = mode == "TwoDistances"
                ? SketchChamferMode::TwoDistances
                : mode == "DistanceAngle" ? SketchChamferMode::DistanceAngle
                : SketchChamferMode::EqualDistance;
            result.push_back(ChamferConstraint{
                string(constraint, "firstLineId"), string(constraint, "secondLineId"),
                string(constraint, "chamferLineId"), chamferMode,
                number(constraint, "firstDistance"), number(constraint, "secondDistance"),
                number(constraint, "angleRadians"), constraint.value("id").toString().toStdString()});
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
            if (o.contains("faceReference")) {
                const auto reference = cad::topology::topologicalReferenceFromJson(
                    o.value("faceReference").toObject());
                require(reference.featureId == source->id()
                            && reference.kind == cad::topology::TopologicalKind::Face,
                        "Invalid PushPull face reference");
                return std::make_shared<PushPullFeature>(string(o, "id"), source,
                    reference,
                    gp_Vec(number(o, "normalX"), number(o, "normalY"), number(o, "normalZ")),
                    number(o, "distance"));
            }
            const double faceIndexValue = number(o, "faceIndex");
            require(faceIndexValue >= 1.0 && std::floor(faceIndexValue) == faceIndexValue,
                    "Invalid PushPull face index");
            return std::make_shared<PushPullFeature>(string(o, "id"), source,
                static_cast<int>(faceIndexValue),
                gp_Vec(number(o, "normalX"), number(o, "normalY"), number(o, "normalZ")),
                number(o, "distance"));
        }},
        {"Revolve", [](const QJsonObject& o, const Body& body) {
            const auto profile = body.findFeature(string(o, "sourceFeatureId"));
            require(static_cast<bool>(profile), "Revolve references missing profile");
            if (o.value("axis").isObject()) {
                const auto axisObject = o.value("axis").toObject();
                const auto axisName = axisObject.value("type").toString().toStdString();
                RevolveAxisDefinition axis;
                if (axisName == "GlobalX") axis.type = RevolveAxisType::GlobalX;
                else if (axisName == "GlobalY") axis.type = RevolveAxisType::GlobalY;
                else if (axisName == "GlobalZ") axis.type = RevolveAxisType::GlobalZ;
                else if (axisName == "SketchLine") axis.type = RevolveAxisType::SketchLine;
                else if (axisName == "ModelEdge") axis.type = RevolveAxisType::ModelEdge;
                else throw std::runtime_error("Invalid Revolve axis type");
                const auto sketch = std::dynamic_pointer_cast<SketchFeature>(profile);
                require(static_cast<bool>(sketch), "Revolve axis requires a Sketch profile");
                std::shared_ptr<ParametricFeature> axisSource;
                if (axis.type == RevolveAxisType::SketchLine) {
                    axis.sketchFeatureId = string(axisObject, "sketchFeatureId");
                    axis.sketchLineId = string(axisObject, "entityId");
                } else if (axis.type == RevolveAxisType::ModelEdge) {
                    axis.edgeFeatureId = string(axisObject, "featureId");
                    axis.edgeReference = cad::topology::topologicalReferenceFromJson(
                        axisObject.value("reference").toObject());
                    axisSource = body.findFeature(axis.edgeFeatureId);
                    require(static_cast<bool>(axisSource), "Revolve axis feature is missing");
                }
                return std::make_shared<RevolveFeature>(string(o, "id"), sketch, axis,
                    number(o, "angleDegrees") * std::acos(-1.0) / 180.0, axisSource);
            }
            const gp_Pnt origin(number(o, "axisOriginX"), number(o, "axisOriginY"),
                number(o, "axisOriginZ"));
            const gp_Dir direction(number(o, "axisX"), number(o, "axisY"),
                number(o, "axisZ"));
            return std::make_shared<RevolveFeature>(string(o, "id"), profile,
                gp_Ax1(origin, direction),
                number(o, "angleDegrees") * std::acos(-1.0) / 180.0);
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
        {"Shell", [](const QJsonObject& o, const Body& body) {
            const auto base = body.findFeature(string(o, "sourceFeatureId"));
            require(static_cast<bool>(base), "Shell references missing source");
            if (o.contains("topologicalReferences")) {
                return std::make_shared<ShellFeature>(string(o, "id"), base,
                    faceTopologicalReferences(o, "topologicalReferences", base->id()),
                    number(o, "thickness"));
            }
            return std::make_shared<ShellFeature>(string(o, "id"), base,
                positiveIntegers(o, "faceIndices"), number(o, "thickness"));
        }},
        {"Offset", [](const QJsonObject& o, const Body& body) {
            const auto base = body.findFeature(string(o, "sourceFeatureId"));
            require(static_cast<bool>(base), "Offset references missing source");
            return std::make_shared<OffsetFeature>(string(o, "id"), base,
                number(o, "distance"));
        }},
        {"Loft", [](const QJsonObject& o, const Body& body) {
            std::vector<ParametricFeature::Ptr> sections;
            for (const auto& id : strings(o, "sectionFeatureIds")) {
                const auto section = body.findFeature(id);
                require(static_cast<bool>(section), "Loft references missing section");
                sections.push_back(section);
            }
            return std::make_shared<LoftFeature>(string(o, "id"), sections,
                boolean(o, "solid"), o.value("ruled").toBool(false));
        }},
        {"Sweep", [](const QJsonObject& o, const Body& body) {
            const auto profile = body.findFeature(string(o, "profileFeatureId"));
            require(static_cast<bool>(profile), "Sweep references missing profile");
            if (o.value("pathType").toString() == "SketchPath") {
                const auto pathId = o.value("pathSketchFeatureId").toString(
                    o.value("pathFeatureId").toString());
                require(!pathId.isEmpty(), "Sweep Sketch path is missing its Sketch reference");
                const auto path = body.findFeature(pathId.toStdString());
                require(path && path->role() == FeatureRole::Sketch,
                    "Sweep references missing Sketch path");
                const auto sketch = std::dynamic_pointer_cast<SketchFeature>(path);
                require(static_cast<bool>(sketch), "Sweep Sketch path has invalid feature type");
                return std::make_shared<SweepFeature>(string(o, "id"), sketch,
                    strings(o, "pathEntityIds"), profile);
            }
            const auto path = body.findFeature(string(o, "pathFeatureId"));
            require(static_cast<bool>(path), "Sweep references missing path");
            if (o.contains("pathReference")) {
                return std::make_shared<SweepFeature>(string(o, "id"), path,
                    cad::topology::topologicalReferenceFromJson(
                        o.value("pathReference").toObject()), profile);
            }
            return std::make_shared<SweepFeature>(string(o, "id"), path, profile);
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
        }},
        {"IfcImported", [](const QJsonObject& o, const Body&) {
            const auto payload = o.value("geometryPayload");
            require(payload.isString() && !payload.toString().isEmpty(),
                    "Missing imported geometry payload");
            return std::make_shared<ImportedFeature>(
                string(o, "id"), string(o, "name"),
                cad::persistence::decodeBRep(payload.toString().toLatin1()),
                o.value("sourceFormat").toString("IFC"),
                o.value("sourceFile").toString(),
                o.value("ifcGlobalId").toString(),
                o.value("ifcEntityType").toString(),
                o.value("ifcDescription").toString(),
                o.value("ifcBuilding").toString(),
                o.value("ifcStorey").toString(),
                o.value("ifcBuildingGlobalId").toString(),
                o.value("ifcStoreyGlobalId").toString(),
                cad::import::deserializeIfcMetadata(o.value("ifcMetadata").toObject()));
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
        feature->setPlacement(rigidPlacement(matrix));
    }
    feature->setName(string(o, "name"));
    if (o.contains("visible")) feature->setUserVisible(boolean(o, "visible"));
    return feature;
}
}

bool ProjectFile::save(const QString& path, const Document& document,
                       const cad::parametric::Body& body, QString& error,
                       const cad::application::VisibilityManager* visibilityManager)
{
    error.clear();
    try {
        cad::persistence::ProjectArchiveWriter archive;
        if (!archive.open(path, error)) return false;
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
            auto serialized = encode(feature, preceding);
            if (std::string(feature->typeId()) == "IfcImported") {
                const auto imported = std::dynamic_pointer_cast<ImportedFeature>(feature);
                require(static_cast<bool>(imported), "Invalid imported feature instance");
                const auto entry = geometryEntryPath(feature->id());
                if (!archive.addEntry(entry,
                                      cad::persistence::encodeBRepRaw(imported->sourceShape()),
                                      error)) return false;
                serialized.insert("shapePayload", QJsonObject{
                    {"storage", "archive"}, {"path", entry}});
            }
            history.append(serialized);
            preceding.addFeature(feature);
        }
        QJsonObject root{{"format", "ParametricCAD"}, {"version", 1},
            {"storage", "archive"}, {"containerVersion", 1},
            {"features", features}, {"body", history}};
        if (visibilityManager) {
            QJsonArray groups;
            for (const auto& group : visibilityManager->groups()) {
                QJsonArray members;
                for (const auto& featureId : group.memberFeatureIds)
                    members.append(QString::fromStdString(featureId));
                groups.append(QJsonObject{
                    {"id", QString::fromStdString(group.id)},
                    {"name", QString::fromStdString(group.name)},
                    {"parentId", group.parentId
                        ? QJsonValue(QString::fromStdString(*group.parentId)) : QJsonValue()},
                    {"mode", visibilityModeName(group.mode)},
                    {"members", members}});
            }
            QJsonObject visibility{{"groups", groups}};
            const auto& filters = visibilityManager->filters();
            if (!filters.empty()) {
                visibility.insert("filters", encodeVisibilityFilters(filters));
            }
            QJsonArray presets;
            for (const auto& preset : visibilityManager->presets()) {
                QJsonArray groupModes;
                for (const auto& [groupId, mode] : preset.configuration.groupModes) {
                    groupModes.append(QJsonObject{{"groupId", QString::fromStdString(groupId)},
                        {"mode", visibilityModeName(mode)}});
                }
                QJsonArray overrides;
                for (const auto& [featureId, visible] : preset.configuration.featureVisibility) {
                    overrides.append(QJsonObject{
                        {"featureId", QString::fromStdString(featureId)},
                        {"visible", visible}});
                }
                const auto& presetFilters = preset.configuration.filters;
                QJsonObject presetObject{
                    {"id", QString::fromStdString(preset.id)},
                    {"name", QString::fromStdString(preset.name)},
                    {"groupModes", groupModes},
                    {"filters", encodeVisibilityFilters(presetFilters)},
                    {"featureOverrides", overrides}};
                presets.append(presetObject);
            }
            if (!presets.isEmpty()) visibility.insert("presets", presets);
            root.insert("visibility", visibility);
            QJsonArray views;
            for (const auto& view : visibilityManager->savedViews()) {
                QJsonArray isolated;
                for (const auto& id : view.isolatedFeatureIds)
                    isolated.append(QString::fromStdString(id));
                QJsonArray ghosted;
                for (const auto& id : view.ghostedSelectionIds)
                    ghosted.append(QString::fromStdString(id));
                QJsonObject object = encodeVisibilityConfiguration(view.visibility);
                object.insert("id", QString::fromStdString(view.id));
                object.insert("name", QString::fromStdString(view.name));
                object.insert("isolatedFeatureIds", isolated);
                object.insert("ghostedSelectionIds", ghosted);
                object.insert("spatial", QJsonObject{
                    {"enabled", view.spatialRule.enabled},
                    {"min", pointArray(view.spatialRule.min)},
                    {"max", pointArray(view.spatialRule.max)},
                    {"mode", visibilityModeName(view.spatialRule.outsideMode)}});
                object.insert("section", QJsonObject{
                    {"active", view.sectionActive},
                    {"axis", view.sectionAxis},
                    {"flipped", view.sectionFlipped},
                    {"origin", pointArray(view.sectionOrigin)}});
                object.insert("camera", QJsonObject{
                    {"valid", view.cameraValid},
                    {"eye", pointArray(view.cameraEye)},
                    {"center", pointArray(view.cameraCenter)},
                    {"up", directionArray(view.cameraUp)},
                    {"scale", view.cameraScale}});
                views.append(object);
            }
            if (!views.isEmpty()) root.insert("views", views);
        }
        if (!archive.addEntry(QStringLiteral("manifest.json"),
                              QJsonDocument(root).toJson(), error)) return false;
        return archive.close(error);
    } catch (const Standard_Failure& e) { error = QString::fromUtf8(e.GetMessageString()); }
      catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
    return false;
}

bool ProjectFile::load(const QString& path, Document& document,
                       cad::parametric::Body& body, QString& error,
                       ProjectLoadProgress progress, ProjectLoadMetrics* metrics,
                       bool recompute,
                       cad::application::VisibilityManager* visibilityManager)
{
    error.clear();
    if (metrics) *metrics = {};
    QElapsedTimer totalTimer;
    totalTimer.start();
    try {
        QElapsedTimer stageTimer;
        stageTimer.start();
        std::unique_ptr<cad::persistence::ProjectArchiveReader> archive;
        QByteArray data;
        if (cad::persistence::isProjectArchive(path)) {
            archive = std::make_unique<cad::persistence::ProjectArchiveReader>(path);
            if (!archive->open(error))
                throw std::runtime_error(error.isEmpty()
                    ? "Could not open project archive" : error.toStdString());
            if (!archive->readEntry(QStringLiteral("manifest.json"), data, error))
                throw std::runtime_error(error.isEmpty()
                    ? "Project archive has no manifest.json" : error.toStdString());
        } else {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
            data = file.readAll();
            if (file.error() != QFileDevice::NoError) { error = file.errorString(); return false; }
        }
        QJsonParseError parseError;
        const auto json = QJsonDocument::fromJson(data, &parseError);
        require(parseError.error == QJsonParseError::NoError && json.isObject(), "Invalid project JSON");
        const auto root = json.object();
        require(string(root,"format") == "ParametricCAD" && number(root,"version") == 1, "Unsupported project format or version");
        require(root.value("features").isArray() && root.value("body").isArray(), "Missing feature arrays");
        cad::application::VisibilityManager loadedVisibility;
        std::string visibilityError;
        require(loadedVisibility.replaceGroups(visibilityGroups(root), visibilityError),
                visibilityError.c_str());
        require(loadedVisibility.replaceFilters(visibilityFilters(root), visibilityError),
                visibilityError.c_str());
        require(loadedVisibility.replacePresets(visibilityPresets(root), visibilityError),
                visibilityError.c_str());
        require(loadedVisibility.replaceSavedViews(savedViews(root), visibilityError),
                visibilityError.c_str());
        if (metrics) metrics->parseMilliseconds = stageTimer.elapsed();

        Body loadedBody;
        const auto legacyFeatures = root.value("features").toArray();
        const auto bodyFeatures = root.value("body").toArray();
        const int totalFeatures = legacyFeatures.size() + bodyFeatures.size();
        if (metrics) metrics->featureCount = totalFeatures;
        if (progress) progress(0, totalFeatures);
        stageTimer.restart();
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
            if (progress) progress(index + 1, totalFeatures);
        }
        for (int index = 0; index < bodyFeatures.size(); ++index) {
            const auto& value = bodyFeatures.at(index);
            require(value.isObject(), "Invalid body feature entry");
            auto object = value.toObject();
            if (archive && object.value("type").toString() == "IfcImported"
                && !object.contains("geometryPayload")) {
                const auto payload = object.value("shapePayload");
                require(payload.isObject(), "Missing imported shape payload");
                const auto payloadObject = payload.toObject();
                require(payloadObject.value("storage").toString() == "archive"
                            && payloadObject.value("path").isString(),
                        "Invalid imported shape payload reference");
                QByteArray shapeBytes;
                if (!archive->readEntry(payloadObject.value("path").toString(),
                                        shapeBytes, error))
                    throw std::runtime_error(error.isEmpty()
                        ? "Could not read imported geometry entry" : error.toStdString());
                object.insert("geometryPayload", QString::fromLatin1(shapeBytes.toBase64()));
            }
            loadedBody.addFeature(decode(object, loadedBody));
            if (progress) progress(legacyFeatures.size() + index + 1, totalFeatures);
        }
        if (metrics) metrics->deserializeMilliseconds = stageTimer.elapsed();
        stageTimer.restart();
        if (recompute && !loadedBody.recompute()) throw std::runtime_error(loadedBody.lastError());
        if (metrics && recompute) {
            metrics->recomputeMilliseconds = stageTimer.elapsed();
            metrics->totalMilliseconds = totalTimer.elapsed();
            qInfo().noquote() << QString("Project load: parse: %1 ms, deserialize: %2 ms, "
                "recompute: %3 ms, total: %4 ms, features: %5")
                .arg(metrics->parseMilliseconds)
                .arg(metrics->deserializeMilliseconds)
                .arg(metrics->recomputeMilliseconds)
                .arg(metrics->totalMilliseconds)
                .arg(metrics->featureCount);
        }
        Document loaded;
        document = std::move(loaded);
        body = std::move(loadedBody);
        if (visibilityManager) *visibilityManager = std::move(loadedVisibility);
        return true;
    } catch (const Standard_Failure& e) { error = QString::fromUtf8(e.GetMessageString()); }
      catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
    if (metrics) metrics->totalMilliseconds = totalTimer.elapsed();
    return false;
}
