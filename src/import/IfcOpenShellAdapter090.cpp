#include "import/IfcOpenShellAdapter.h"

#include <QElapsedTimer>

#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <ifcgeom/conversion_settings.h>
#include <ifcgeom/iterator.h>
#include <ifcgeom/kernels/opencascade/opencascade_conversion_result.h>
#include <ifcgeom/kernel_registry.h>
#include <ifcparse/express.h>
#include <ifcparse/file.h>
#include <ifcparse/instance_data.h>
#include <plugin/plugin.h>
#endif

namespace cad::import {

#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
namespace {

using IfcBase = express::base;
using IfcEntity = express::entity;

void configurePluginSearchPath()
{
    static const bool configured = [] {
        ifcopenshell::plugin::set_search_paths({
            PARAMETRIC_CAD_IFCOPENSHELL_PLUGIN_DIR});
        return true;
    }();
    Q_UNUSED(configured);
}

bool excluded(const QString& type)
{
    return type == "IfcOpeningElement" || type == "IfcSpace" || type == "IfcGrid"
        || type == "IfcDistributionPort" || type == "IfcAnnotation"
        || type == "IfcProject" || type == "IfcSite" || type == "IfcBuilding"
        || type == "IfcBuildingStorey";
}

QString valueText(const ifcopenshell::attribute_value& value)
{
    if (value.isNull()) return {};
    switch (value.type()) {
    case ifcopenshell::Argument_STRING:
    case ifcopenshell::Argument_ENUMERATION:
        return QString::fromStdString(static_cast<std::string>(value));
    case ifcopenshell::Argument_INT:
        return QString::number(static_cast<qlonglong>(static_cast<int64_t>(value)));
    case ifcopenshell::Argument_DOUBLE:
        return QString::number(static_cast<double>(value), 'g', 16);
    case ifcopenshell::Argument_ENTITY_INSTANCE: {
        std::ostringstream stream;
        static_cast<IfcBase>(value).to_string(stream);
        return QString::fromStdString(stream.str());
    }
    default:
        return {};
    }
}

QString attribute(const IfcEntity& entity, const char* name)
{
    if (!entity) return {};
    try { return valueText(entity.get(name)); } catch (...) { return {}; }
}

IfcBase objectAttribute(const IfcEntity& entity, const char* name)
{
    if (!entity) return {};
    try {
        const auto value = entity.get(name);
        return value.isNull() ? IfcBase{} : static_cast<IfcBase>(value);
    } catch (...) { return {}; }
}

std::vector<IfcBase> listAttribute(const IfcEntity& entity, const char* name)
{
    if (!entity) return {};
    try {
        const auto value = entity.get(name);
        return value.isNull() ? std::vector<IfcBase>{}
                              : static_cast<std::vector<IfcBase>>(value);
    } catch (...) { return {}; }
}

IfcEntity entity(const IfcBase& value)
{
    return value ? value.as<IfcEntity>() : IfcEntity{};
}

QString propertyValue(const IfcEntity& property)
{
    const auto nominal = entity(objectAttribute(property, "NominalValue"));
    if (nominal) {
        const auto text = valueText(nominal.get_attribute_value(0));
        if (!text.isEmpty()) return text;
    }
    for (const char* name : {"EnumerationValues", "ListValues", "UpperBoundValue",
                             "LowerBoundValue", "PropertyReference"}) {
        const auto text = attribute(property, name);
        if (!text.isEmpty()) return text;
    }
    return {};
}

struct SpatialMetadata {
    QString building;
    QString buildingGlobalId;
    QString storey;
    QString storeyGlobalId;
};

struct SemanticIndex {
    std::unordered_map<uint32_t, IfcBase> typeByProduct;
    std::unordered_map<uint32_t, std::vector<IfcBase>> definitionsByProduct;
    std::unordered_map<uint32_t, std::vector<IfcBase>> materialsByProduct;
};

SemanticIndex semanticIndex(ifcopenshell::file& file)
{
    SemanticIndex result;
    for (const auto& base : file.instances_by_type("IfcRelDefinesByType")) {
        const auto relation = entity(base);
        const auto type = objectAttribute(relation, "RelatingType");
        if (!type) continue;
        for (const auto& object : listAttribute(relation, "RelatedObjects"))
            if (object) result.typeByProduct[object.identity()] = type;
    }
    for (const auto& base : file.instances_by_type("IfcRelDefinesByProperties")) {
        const auto relation = entity(base);
        const auto definition = objectAttribute(relation, "RelatingPropertyDefinition");
        if (!definition) continue;
        for (const auto& object : listAttribute(relation, "RelatedObjects"))
            if (object) result.definitionsByProduct[object.identity()].push_back(definition);
    }
    for (const auto& base : file.instances_by_type("IfcRelAssociatesMaterial")) {
        const auto relation = entity(base);
        const auto material = objectAttribute(relation, "RelatingMaterial");
        if (!material) continue;
        for (const auto& object : listAttribute(relation, "RelatedObjects"))
            if (object) result.materialsByProduct[object.identity()].push_back(material);
    }
    return result;
}

IfcMetadata metadata(const IfcEntity& product, const SemanticIndex& index)
{
    IfcMetadata result;
    result.predefinedType = attribute(product, "PredefinedType");
    if (const auto type = index.typeByProduct.find(product.identity()); type != index.typeByProduct.end()) {
        const auto typeEntity = entity(type->second);
        result.type.entityType = QString::fromStdString(typeEntity.declaration().name());
        result.type.globalId = attribute(typeEntity, "GlobalId");
        result.type.name = attribute(typeEntity, "Name");
    }
    if (const auto definitions = index.definitionsByProduct.find(product.identity());
        definitions != index.definitionsByProduct.end()) {
        for (const auto& base : definitions->second) {
            const auto definition = entity(base);
            const QString kind = QString::fromStdString(definition.declaration().name());
            if (kind == "IfcPropertySet") {
                IfcPropertySetData set;
                set.name = attribute(definition, "Name");
                for (const auto& propertyBase : listAttribute(definition, "HasProperties")) {
                    const auto property = entity(propertyBase);
                    if (!property) continue;
                    IfcPropertyValue value;
                    value.name = attribute(property, "Name");
                    value.value.kind = QString::fromStdString(property.declaration().name());
                    value.value.text = propertyValue(property);
                    set.properties.push_back(std::move(value));
                }
                result.propertySets.push_back(std::move(set));
            } else if (kind == "IfcElementQuantity") {
                IfcQuantitySetData set;
                set.name = attribute(definition, "Name");
                for (const auto& quantityBase : listAttribute(definition, "Quantities")) {
                    const auto quantity = entity(quantityBase);
                    if (!quantity) continue;
                    const QString quantityKind = QString::fromStdString(quantity.declaration().name())
                        .mid(QStringLiteral("IfcQuantity").size());
                    const char* field = "Value";
                    if (quantityKind == "Length") field = "LengthValue";
                    else if (quantityKind == "Area") field = "AreaValue";
                    else if (quantityKind == "Volume") field = "VolumeValue";
                    else if (quantityKind == "Count") field = "CountValue";
                    else if (quantityKind == "Weight") field = "WeightValue";
                    set.quantities.push_back({attribute(quantity, "Name"), quantityKind,
                                              attribute(quantity, field)});
                }
                result.quantitySets.push_back(std::move(set));
            }
        }
    }
    if (const auto materials = index.materialsByProduct.find(product.identity());
        materials != index.materialsByProduct.end()) {
        for (const auto& base : materials->second) {
            const auto material = entity(base);
            const QString kind = QString::fromStdString(material.declaration().name());
            IfcMaterialData data;
            if (kind == "IfcMaterial") {
                data.name = attribute(material, "Name");
            } else if (kind == "IfcMaterialLayerSet" || kind == "IfcMaterialLayerSetUsage") {
                const auto layerSet = kind == "IfcMaterialLayerSetUsage"
                    ? entity(objectAttribute(material, "ForLayerSet")) : material;
                data.name = attribute(layerSet, "LayerSetName");
                for (const auto& layerBase : listAttribute(layerSet, "MaterialLayers")) {
                    const auto layer = entity(layerBase);
                    const auto layerMaterial = entity(objectAttribute(layer, "Material"));
                    data.layers.push_back({attribute(layerMaterial, "Name"),
                                           attribute(layer, "LayerThickness")});
                }
            } else if (kind == "IfcMaterialList") {
                for (const auto& itemBase : listAttribute(material, "Materials")) {
                    const auto item = entity(itemBase);
                    if (item) data.layers.push_back({attribute(item, "Name"), {}});
                }
            } else if (kind == "IfcMaterialConstituentSet") {
                data.name = attribute(material, "Name");
                for (const auto& itemBase : listAttribute(material, "MaterialConstituents")) {
                    const auto item = entity(itemBase);
                    const auto itemMaterial = entity(objectAttribute(item, "Material"));
                    if (item) data.layers.push_back({attribute(itemMaterial, "Name"), {}});
                }
            }
            if (!data.name.isEmpty() || !data.layers.empty()) result.materials.push_back(std::move(data));
        }
    }
    return result;
}

QString label(const IfcEntity& object, const char* fallback)
{
    const auto name = attribute(object, "Name");
    if (!name.isEmpty()) return name;
    const auto globalId = attribute(object, "GlobalId");
    return globalId.isEmpty() ? QString::fromLatin1(fallback)
                              : QStringLiteral("%1 %2").arg(QString::fromLatin1(fallback), globalId.left(8));
}

std::unordered_map<uint32_t, SpatialMetadata> spatialIndex(ifcopenshell::file& file)
{
    std::unordered_map<uint32_t, IfcBase> containedBy;
    for (const auto& base : file.instances_by_type("IfcRelContainedInSpatialStructure")) {
        const auto relation = entity(base);
        const auto structure = objectAttribute(relation, "RelatingStructure");
        for (const auto& object : listAttribute(relation, "RelatedElements"))
            if (structure && object) containedBy[object.identity()] = structure;
    }
    std::unordered_map<uint32_t, IfcBase> parentBy;
    for (const auto& base : file.instances_by_type("IfcRelAggregates")) {
        const auto relation = entity(base);
        const auto parent = objectAttribute(relation, "RelatingObject");
        for (const auto& child : listAttribute(relation, "RelatedObjects"))
            if (parent && child) parentBy[child.identity()] = parent;
    }
    std::unordered_map<uint32_t, SpatialMetadata> result;
    for (const auto& [productId, base] : containedBy) {
        auto current = entity(base);
        IfcEntity building, storey;
        std::unordered_set<uint32_t> visited;
        while (current && visited.insert(current.identity()).second) {
            const auto type = QString::fromStdString(current.declaration().name());
            if (!building && type == "IfcBuilding") building = current;
            if (!storey && type == "IfcBuildingStorey") storey = current;
            const auto parent = parentBy.find(current.identity());
            if (parent == parentBy.end()) break;
            current = entity(parent->second);
        }
        if (!building && !storey) continue;
        SpatialMetadata value;
        if (building) { value.building = label(building, "IfcBuilding"); value.buildingGlobalId = attribute(building, "GlobalId"); }
        if (storey) { value.storey = label(storey, "IfcBuildingStorey"); value.storeyGlobalId = attribute(storey, "GlobalId"); }
        result.emplace(productId, std::move(value));
    }
    return result;
}

QStringList representationTypes(const IfcEntity& product)
{
    QStringList result;
    const auto definition = entity(objectAttribute(product, "Representation"));
    for (const auto& base : listAttribute(definition, "Representations")) {
        const auto representation = entity(base);
        if (!representation) continue;
        const auto type = attribute(representation, "RepresentationType");
        if (!type.isEmpty()) result.append(type);
        for (const auto& itemBase : listAttribute(representation, "Items")) {
            const auto item = entity(itemBase);
            if (item) result.append(QString::fromStdString(item.declaration().name()));
        }
    }
    result.removeDuplicates();
    return result;
}

int placementDepth(const IfcEntity& product)
{
    auto placement = entity(objectAttribute(product, "ObjectPlacement"));
    int depth = 0;
    while (placement && depth++ < 100)
        placement = entity(objectAttribute(placement, "PlacementRelTo"));
    return depth;
}

}
#endif

IfcImportResult IfcOpenShellAdapter::importFile(const QString& path,
                                                IfcImportProgressCallback progress,
                                                IfcImportCancellation cancellation) const
{
    IfcImportResult result;
    QElapsedTimer total;
    total.start();
#if !defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    Q_UNUSED(path);
    Q_UNUSED(progress);
    Q_UNUSED(cancellation);
    result.diagnostics.push_back({ImportSeverity::Error, {}, {},
        QStringLiteral("IfcOpenShell support is disabled. Configure IfcOpenShell 0.9.x with "
                       "PARAMETRIC_CAD_ENABLE_IFC=ON.")});
    result.fatal = true;
    result.statistics.failedCount = 1;
    result.statistics.totalMilliseconds = total.elapsed();
    return result;
#else
    try {
        configurePluginSearchPath();
        if (progress) progress({IfcImportStage::Opening, 0, 0});
        if (cancellation && cancellation()) { result.cancelled = true; return result; }
        QElapsedTimer parse;
        parse.start();
        if (progress) progress({IfcImportStage::Parsing, 0, 0});
        auto file = std::make_unique<ifcopenshell::file>(ifcopenshell::uninitialized_tag{});
        if (!file->initialize(path.toStdString()) || !file->good()) {
            result.diagnostics.push_back({ImportSeverity::Error, {}, {},
                QStringLiteral("IfcOpenShell 0.9.x could not open the IFC file")});
            result.fatal = true;
            result.statistics.failedCount = 1;
            return result;
        }
        result.statistics.schema = QString::fromStdString(file->schema()->name());
        result.statistics.parseMilliseconds = parse.elapsed();

        ifcopenshell::geom::settings settings;
        settings.get<ifcopenshell::geom::settings::UseWorldCoords>().value = true;
        settings.get<ifcopenshell::geom::settings::ConvertBackUnits>().value = false;
        settings.get<ifcopenshell::geom::settings::UseElementHierarchy>().value = true;
        settings.get<ifcopenshell::geom::settings::IteratorOutput>().value = ifcopenshell::geom::settings::NATIVE;
        if (progress) progress({IfcImportStage::Geometry, 0, 0});
        QElapsedTimer geometry;
        geometry.start();
        ifcopenshell::logger logger;
        auto kernel = ifcopenshell::geom::kernels::construct(file.get(), "opencascade", settings, logger);
        ifcopenshell::geom::iterator iterator(std::move(kernel), settings, file.get(), logger);
        if (!iterator.initialize()) {
            result.diagnostics.push_back({ImportSeverity::Error, {}, {},
                QStringLiteral("IfcOpenShell 0.9.0 could not initialize the OCCT geometry kernel")});
            result.fatal = true;
            result.statistics.failedCount = 1;
            return result;
        }
        const auto containment = spatialIndex(*file);
        const auto semantics = semanticIndex(*file);
        std::size_t processed = 0;
        do {
            if (cancellation && cancellation()) {
                result.cancelled = true;
                result.products.clear();
                break;
            }
            auto elementHolder = iterator.get();
            auto* element = dynamic_cast<ifcopenshell::geom::native_element*>(elementHolder.get());
            if (!element) {
                ++result.statistics.failedCount;
                result.diagnostics.push_back({ImportSeverity::Warning, {}, {},
                    QStringLiteral("IfcOpenShell returned no native OCCT element")});
                continue;
            }
            const QString type = QString::fromStdString(element->type());
            ++processed;
            if (progress && (processed == 1 || processed % 32 == 0))
                progress({IfcImportStage::Geometry, processed, 0});
            ++result.statistics.consideredCount;
            ++result.statistics.consideredByEntity[type];
            if (excluded(type)) { ++result.statistics.skippedCount; continue; }
            try {
                std::unique_ptr<ifcopenshell::geom::conversion_result_shape> compound(
                    element->geometry().as_compound(true));
                const auto* openCascadeShape = dynamic_cast<
                    const ifcopenshell::geom::open_cascade_shape*>(compound.get());
                if (!openCascadeShape)
                    throw std::runtime_error("native geometry is not an OpenCASCADE shape");
                TopoDS_Shape shape = openCascadeShape->shape();
                if (shape.IsNull()) throw std::runtime_error("native geometry is null");
                gp_Trsf scale;
                scale.SetScale(gp_Pnt(0, 0, 0), 1000.0);
                shape = BRepBuilderAPI_Transform(shape, scale, true).Shape();
                if (shape.IsNull()) throw std::runtime_error("normalized shape is null");
                const auto product = element->product().as<IfcEntity>();
                const auto found = containment.find(element->product().identity());
                QString building, buildingGlobalId, storey, storeyGlobalId;
                if (found != containment.end()) {
                    building = found->second.building;
                    buildingGlobalId = found->second.buildingGlobalId;
                    storey = found->second.storey;
                    storeyGlobalId = found->second.storeyGlobalId;
                }
                result.products.push_back({std::move(shape), QString::fromStdString(element->guid()), type,
                    QString::fromStdString(element->name()), {}, building, buildingGlobalId, storey,
                    storeyGlobalId, representationTypes(product), placementDepth(product),
                    metadata(product, semantics)});
                ++result.statistics.productsWithGeometry;
                ++result.statistics.importedCount;
                ++result.statistics.importedByEntity[type];
            } catch (const std::exception& exception) {
                ++result.statistics.failedCount;
                ++result.statistics.failedByEntity[type];
                result.diagnostics.push_back({ImportSeverity::Warning,
                    QString::fromStdString(element->guid()), type, QString::fromUtf8(exception.what())});
            }
        } while (iterator.next());
        result.statistics.geometryMilliseconds = geometry.elapsed();
        if (progress) progress({IfcImportStage::Finalizing, processed, processed});
        result.statistics.totalMilliseconds = total.elapsed();
        return result;
    } catch (const std::exception& exception) {
        result.diagnostics.push_back({ImportSeverity::Error, {}, {}, QString::fromUtf8(exception.what())});
        result.fatal = true;
        result.statistics.failedCount = 1;
        result.statistics.totalMilliseconds = total.elapsed();
        return result;
    }
#endif
}

} // namespace cad::import
