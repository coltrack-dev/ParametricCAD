#include "import/IfcOpenShellAdapter.h"

#include <QElapsedTimer>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
#include <BRepBuilderAPI_Transform.hxx>
#include <ifcgeom_schema_agnostic/IfcGeomIterator.h>
#include <ifcgeom_schema_agnostic/IfcGeomElement.h>
#include <ifcparse/Ifc2x3.h>
#include <ifcparse/Ifc4.h>
#include <ifcparse/IfcFile.h>
#include <ifcparse/IfcBaseClass.h>
#include <ifcparse/IfcParse.h>
#include <gp_Trsf.hxx>
#endif

namespace cad::import {

#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
namespace {

bool excluded(const QString& type)
{
    return type == "IfcOpeningElement" || type == "IfcSpace" || type == "IfcGrid"
        || type == "IfcDistributionPort" || type == "IfcAnnotation"
        || type == "IfcProject" || type == "IfcSite" || type == "IfcBuilding"
        || type == "IfcBuildingStorey";
}

struct SpatialMetadata
{
    QString building;
    QString buildingGlobalId;
    QString storey;
    QString storeyGlobalId;
};

QString entityAttribute(IfcUtil::IfcBaseClass* entity, const char* name)
{
    auto* base = entity ? entity->as<IfcUtil::IfcBaseEntity>() : nullptr;
    if (!base) return {};
    Argument* argument = nullptr;
    try { argument = base->get(name); } catch (...) { return {}; }
    if (!argument || argument->isNull()) return {};
    return QString::fromStdString(argument->toString());
}

IfcUtil::IfcBaseClass* entityAttributeObject(IfcUtil::IfcBaseClass* entity, const char* name)
{
    auto* base = entity ? entity->as<IfcUtil::IfcBaseEntity>() : nullptr;
    if (!base) return nullptr;
    Argument* argument = nullptr;
    try { argument = base->get(name); } catch (...) { return nullptr; }
    if (!argument || argument->isNull()) return nullptr;
    return static_cast<IfcUtil::IfcBaseClass*>(*argument);
}

boost::shared_ptr<aggregate_of_instance> entityAttributeList(
    IfcUtil::IfcBaseClass* entity, const char* name)
{
    auto* base = entity ? entity->as<IfcUtil::IfcBaseEntity>() : nullptr;
    if (!base) return {};
    Argument* argument = nullptr;
    try { argument = base->get(name); } catch (...) { return {}; }
    if (!argument || argument->isNull()) return {};
    return static_cast<boost::shared_ptr<aggregate_of_instance>>(*argument);
}

QString propertyValue(IfcUtil::IfcBaseClass* property)
{
    auto* nominal = entityAttributeObject(property, "NominalValue");
    if (!nominal) {
        for (const char* name : {"EnumerationValues", "ListValues", "UpperBoundValue",
                                 "LowerBoundValue", "PropertyReference"}) {
            const auto value = entityAttribute(property, name);
            if (!value.isEmpty()) return value;
        }
        return {};
    }
    return QString::fromStdString(nominal->data().toString());
}

struct SemanticIndex
{
    std::unordered_map<uint32_t, IfcUtil::IfcBaseClass*> typeByProduct;
    std::unordered_map<uint32_t, std::vector<IfcUtil::IfcBaseClass*>> definitionsByProduct;
    std::unordered_map<uint32_t, std::vector<IfcUtil::IfcBaseClass*>> materialsByProduct;
};

SemanticIndex semanticIndex(IfcParse::IfcFile& file)
{
    SemanticIndex index;
    const auto types = file.instances_by_type("IfcRelDefinesByType");
    for (auto* relation : *types) {
        auto* type = entityAttributeObject(relation, "RelatingType");
        const auto related = entityAttributeList(relation, "RelatedObjects");
        if (!type || !related) continue;
        for (auto* object : *related)
            if (object) index.typeByProduct[object->identity()] = type;
    }
    const auto definitions = file.instances_by_type("IfcRelDefinesByProperties");
    for (auto* relation : *definitions) {
        auto* definition = entityAttributeObject(relation, "RelatingPropertyDefinition");
        const auto related = entityAttributeList(relation, "RelatedObjects");
        if (!definition || !related) continue;
        for (auto* object : *related)
            if (object) index.definitionsByProduct[object->identity()].push_back(definition);
    }
    const auto materials = file.instances_by_type("IfcRelAssociatesMaterial");
    for (auto* relation : *materials) {
        auto* material = entityAttributeObject(relation, "RelatingMaterial");
        const auto related = entityAttributeList(relation, "RelatedObjects");
        if (!material || !related) continue;
        for (auto* object : *related)
            if (object) index.materialsByProduct[object->identity()].push_back(material);
    }
    return index;
}

IfcMetadata semanticMetadata(IfcUtil::IfcBaseClass* product, const SemanticIndex& index)
{
    IfcMetadata metadata;
    metadata.predefinedType = entityAttribute(product, "PredefinedType");

    if (product) {
        const auto type = index.typeByProduct.find(product->identity());
        if (type != index.typeByProduct.end() && type->second) {
            metadata.type.entityType = QString::fromLatin1(type->second->declaration().name().c_str());
            metadata.type.globalId = entityAttribute(type->second, "GlobalId");
            metadata.type.name = entityAttribute(type->second, "Name");
        }
    }

    const auto definitions = product ? index.definitionsByProduct.find(product->identity())
                                     : index.definitionsByProduct.end();
    if (definitions != index.definitionsByProduct.end()) {
        for (auto* definition : definitions->second) {
            if (!definition) continue;
            const QString definitionType = QString::fromLatin1(definition->declaration().name().c_str());
            if (definitionType == "IfcPropertySet") {
                IfcPropertySetData set;
                set.name = entityAttribute(definition, "Name");
                const auto properties = entityAttributeList(definition, "HasProperties");
                if (properties) for (auto* property : *properties) {
                    if (!property) continue;
                    IfcPropertyValue value;
                    value.name = entityAttribute(property, "Name");
                    value.value.kind = QString::fromLatin1(property->declaration().name().c_str());
                    value.value.text = propertyValue(property);
                    set.properties.push_back(std::move(value));
                }
                metadata.propertySets.push_back(std::move(set));
            } else if (definitionType == "IfcElementQuantity") {
                IfcQuantitySetData set;
                set.name = entityAttribute(definition, "Name");
                const auto quantities = entityAttributeList(definition, "Quantities");
                if (quantities) for (auto* quantity : *quantities) {
                    if (!quantity) continue;
                    const QString kind = QString::fromLatin1(quantity->declaration().name().c_str())
                        .mid(QStringLiteral("IfcQuantity").size());
                    QString value;
                    if (kind == "Length") value = entityAttribute(quantity, "LengthValue");
                    else if (kind == "Area") value = entityAttribute(quantity, "AreaValue");
                    else if (kind == "Volume") value = entityAttribute(quantity, "VolumeValue");
                    else if (kind == "Count") value = entityAttribute(quantity, "CountValue");
                    else if (kind == "Weight") value = entityAttribute(quantity, "WeightValue");
                    else value = entityAttribute(quantity, "Value");
                    IfcQuantityValue quantityValue;
                    quantityValue.name = entityAttribute(quantity, "Name");
                    quantityValue.kind = kind;
                    quantityValue.value = value;
                    set.quantities.push_back(std::move(quantityValue));
                }
                metadata.quantitySets.push_back(std::move(set));
            }
        }
    }

    const auto associations = product ? index.materialsByProduct.find(product->identity())
                                      : index.materialsByProduct.end();
    if (associations != index.materialsByProduct.end()) for (auto* material : associations->second) {
        if (!material) continue;
        IfcMaterialData data;
        const QString type = QString::fromLatin1(material->declaration().name().c_str());
        if (type == "IfcMaterial") {
            data.name = entityAttribute(material, "Name");
        } else if (type == "IfcMaterialLayerSet" || type == "IfcMaterialLayerSetUsage") {
            data.name = entityAttribute(material, "LayerSetName");
            auto* layerSet = type == "IfcMaterialLayerSetUsage"
                ? entityAttributeObject(material, "ForLayerSet") : material;
            const auto layers = entityAttributeList(layerSet, "MaterialLayers");
            if (layers) for (auto* layer : *layers) {
                if (!layer) continue;
                auto* layerMaterial = entityAttributeObject(layer, "Material");
                IfcMaterialLayerData layerData;
                layerData.name = layerMaterial ? entityAttribute(layerMaterial, "Name") : QString{};
                layerData.thickness = entityAttribute(layer, "LayerThickness");
                data.layers.push_back(std::move(layerData));
            }
        } else if (type == "IfcMaterialList") {
            const auto materials = entityAttributeList(material, "Materials");
            if (materials) for (auto* item : *materials)
                if (item) {
                    IfcMaterialLayerData layerData;
                    layerData.name = entityAttribute(item, "Name");
                    data.layers.push_back(std::move(layerData));
                }
        } else if (type == "IfcMaterialConstituentSet") {
            data.name = entityAttribute(material, "Name");
            const auto constituents = entityAttributeList(material, "MaterialConstituents");
            if (constituents) for (auto* item : *constituents) {
                if (!item) continue;
                auto* constituent = entityAttributeObject(item, "Material");
                IfcMaterialLayerData layerData;
                layerData.name = constituent ? entityAttribute(constituent, "Name") : QString{};
                data.layers.push_back(std::move(layerData));
            }
        }
        if (!data.name.isEmpty() || !data.layers.empty()) metadata.materials.push_back(std::move(data));
    }
    return metadata;
}

template <typename Schema>
struct SpatialElementType;

template <>
struct SpatialElementType<Ifc2x3>
{
    using type = Ifc2x3::IfcSpatialStructureElement;
};

template <>
struct SpatialElementType<Ifc4>
{
    using type = Ifc4::IfcSpatialElement;
};

template <typename Object>
QString objectLabel(const Object* object, const char* fallbackPrefix)
{
    if (!object) return {};
    const auto name = object->Name();
    if (name && !name->empty()) return QString::fromStdString(*name);
    const auto globalId = object->GlobalId();
    if (!globalId.empty()) {
        return QStringLiteral("%1 %2").arg(QString::fromLatin1(fallbackPrefix),
                                            QString::fromStdString(globalId.substr(0, 8)));
    }
    return QString::fromLatin1(fallbackPrefix);
}

template <typename Schema>
void buildSpatialIndex(IfcParse::IfcFile& file,
                       std::unordered_map<uint32_t, SpatialMetadata>& index)
{
    using SpatialElement = typename SpatialElementType<Schema>::type;
    using ObjectDefinition = typename Schema::IfcObjectDefinition;

    std::unordered_map<uint32_t, SpatialElement*> containedBy;
    const auto containment = file.instances_by_type<
        typename Schema::IfcRelContainedInSpatialStructure>();
    for (auto it = containment->begin(); it != containment->end(); ++it) {
        auto* relationship = *it;
        if (!relationship || !relationship->RelatingStructure()) continue;
        const auto elements = relationship->RelatedElements();
        if (!elements) continue;
        for (auto elementIt = elements->begin(); elementIt != elements->end(); ++elementIt) {
            if (*elementIt)
                containedBy[(*elementIt)->identity()] = relationship->RelatingStructure();
        }
    }

    std::unordered_map<uint32_t, ObjectDefinition*> decomposedBy;
    const auto aggregates = file.instances_by_type<typename Schema::IfcRelAggregates>();
    for (auto it = aggregates->begin(); it != aggregates->end(); ++it) {
        auto* relationship = *it;
        if (!relationship || !relationship->RelatingObject()) continue;
        const auto parts = relationship->RelatedObjects();
        if (!parts) continue;
        for (auto partIt = parts->begin(); partIt != parts->end(); ++partIt) {
            if (*partIt)
                decomposedBy[(*partIt)->identity()] = relationship->RelatingObject();
        }
    }

    for (const auto& [productIdentity, initialStructure] : containedBy) {
        auto* current = initialStructure;
        typename Schema::IfcBuildingStorey* storey = nullptr;
        typename Schema::IfcBuilding* building = nullptr;
        std::unordered_set<uint32_t> visited;
        while (current && visited.insert(current->identity()).second) {
            if (!storey) storey = current->template as<typename Schema::IfcBuildingStorey>();
            if (!building) building = current->template as<typename Schema::IfcBuilding>();
            const auto parent = decomposedBy.find(current->identity());
            if (parent == decomposedBy.end()) break;
            current = parent->second->template as<SpatialElement>();
        }
        if (!storey && !building) continue;

        SpatialMetadata metadata;
        if (building) {
            metadata.building = objectLabel(building, "IfcBuilding");
            metadata.buildingGlobalId = QString::fromStdString(building->GlobalId());
        }
        if (storey) {
            metadata.storey = objectLabel(storey, "IfcBuildingStorey");
            metadata.storeyGlobalId = QString::fromStdString(storey->GlobalId());
        }
        index.emplace(productIdentity, std::move(metadata));
    }
}

std::unordered_map<uint32_t, SpatialMetadata> spatialIndex(IfcParse::IfcFile& file)
{
    std::unordered_map<uint32_t, SpatialMetadata> result;
    const auto schema = QString::fromStdString(file.schema()->name());
    if (schema.contains(QStringLiteral("2X3"), Qt::CaseInsensitive))
        buildSpatialIndex<Ifc2x3>(file, result);
    else
        buildSpatialIndex<Ifc4>(file, result);
    return result;
}

template <typename Schema>
QStringList schemaRepresentationTypes(IfcUtil::IfcBaseClass* base)
{
    QStringList result;
    const auto* product = base ? base->as<typename Schema::IfcProduct>() : nullptr;
    if (!product || !product->Representation()) return result;
    const auto representations = product->Representation()->Representations();
    if (!representations) return result;
    for (auto* item : *representations) {
        if (!item) continue;
        const auto type = item->RepresentationType();
        if (type)
            result.append(QString::fromStdString(*type));
        const auto items = item->Items();
        if (!items) continue;
        for (auto* representationItem : *items) {
            if (representationItem)
                result.append(QString::fromStdString(representationItem->declaration().name()));
        }
    }
    result.removeDuplicates();
    return result;
}

QStringList representationTypes(IfcUtil::IfcBaseClass* product)
{
    QStringList result = schemaRepresentationTypes<Ifc2x3>(product);
    if (result.isEmpty())
        result = schemaRepresentationTypes<Ifc4>(product);
    return result;
}

template <typename Schema>
int schemaPlacementDepth(IfcUtil::IfcBaseClass* base)
{
    const auto* product = base ? base->as<typename Schema::IfcProduct>() : nullptr;
    if (!product) return 0;
    auto* placement = product->ObjectPlacement();
    int depth = 0;
    while (placement && depth < 100) {
        ++depth;
        auto* local = placement->template as<typename Schema::IfcLocalPlacement>();
        placement = local ? local->PlacementRelTo() : nullptr;
    }
    return depth;
}

int placementDepth(IfcUtil::IfcBaseClass* product)
{
    const int ifc2x3Depth = schemaPlacementDepth<Ifc2x3>(product);
    return ifc2x3Depth != 0 ? ifc2x3Depth : schemaPlacementDepth<Ifc4>(product);
}

}
#endif

IfcImportResult IfcOpenShellAdapter::importFile(
    const QString& path,
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
        QStringLiteral("IfcOpenShell support is disabled. Configure a pinned v0.7.1 build "
                       "with PARAMETRIC_CAD_ENABLE_IFC=ON.")});
    result.fatal = true;
    result.statistics.failedCount = 1;
    result.statistics.totalMilliseconds = total.elapsed();
    return result;
#else
    try {
        if (progress) progress({IfcImportStage::Opening, 0, 0});
        if (cancellation && cancellation()) {
            result.cancelled = true;
            return result;
        }
        QElapsedTimer parse;
        parse.start();
        if (progress) progress({IfcImportStage::Parsing, 0, 0});
        auto file = std::make_unique<IfcParse::IfcFile>(path.toStdString());
        if (!file->good()) {
            result.diagnostics.push_back({ImportSeverity::Error, {}, {},
                QStringLiteral("IfcOpenShell could not open the IFC file")});
            result.fatal = true;
            result.statistics.failedCount = 1;
            return result;
        }
        result.statistics.schema = QString::fromStdString(file->schema()->name());
        result.statistics.parseMilliseconds = parse.elapsed();

        IfcGeom::IteratorSettings settings;
        settings.set(IfcGeom::IteratorSettings::USE_BREP_DATA, true);
        settings.set(IfcGeom::IteratorSettings::USE_WORLD_COORDS, true);
        settings.set(IfcGeom::IteratorSettings::CONVERT_BACK_UNITS, false);
        settings.set(IfcGeom::IteratorSettings::SEW_SHELLS, true);
        settings.set(IfcGeom::IteratorSettings::DISABLE_TRIANGULATION, true);
        settings.set(IfcGeom::IteratorSettings::ELEMENT_HIERARCHY, true);

        QElapsedTimer geometry;
        geometry.start();
        if (progress) progress({IfcImportStage::Geometry, 0, 0});
        IfcGeom::Iterator iterator(settings, file.get());
        if (!iterator.initialize()) {
            result.diagnostics.push_back({ImportSeverity::Error, {}, {},
                QStringLiteral("IfcOpenShell could not initialize the geometry iterator")});
            result.fatal = true;
            result.statistics.failedCount = 1;
            return result;
        }
        const auto containment = spatialIndex(*file);
        const auto semantic = semanticIndex(*file);

        std::size_t processed = 0;
        do {
            if (cancellation && cancellation()) {
                result.cancelled = true;
                result.products.clear();
                break;
            }
            auto* element = iterator.get_native();
            if (!element) {
                ++result.statistics.failedCount;
                result.diagnostics.push_back({ImportSeverity::Warning, {}, {},
                    QStringLiteral("Geometry iterator returned no native B-Rep element")});
                continue;
            }
            const QString type = QString::fromStdString(element->type());
            ++processed;
            if (progress && (processed == 1 || processed % 32 == 0))
                progress({IfcImportStage::Geometry, processed, 0});
            ++result.statistics.consideredCount;
            ++result.statistics.consideredByEntity[type];
            if (excluded(type)) {
                ++result.statistics.skippedCount;
                continue;
            }

            try {
                // IfcOpenShell's native iterator returns metres when
                // CONVERT_BACK_UNITS is false. ParametricCAD's canonical unit
                // is millimetres, so this is the single normalization point.
                TopoDS_Shape shape = element->geometry().as_compound(true);
                if (shape.IsNull()) {
                    ++result.statistics.failedCount;
                    ++result.statistics.failedByEntity[type];
                    continue;
                }
                gp_Trsf scale;
                scale.SetScale(gp_Pnt(0, 0, 0), 1000.0);
                shape = BRepBuilderAPI_Transform(shape, scale, true).Shape();
                if (shape.IsNull()) throw std::runtime_error("normalized shape is null");

                QString building;
                QString buildingGlobalId;
                QString storey;
                QString storeyGlobalId;
                if (const auto found = containment.find(element->product()->identity());
                    found != containment.end()) {
                    building = found->second.building;
                    buildingGlobalId = found->second.buildingGlobalId;
                    storey = found->second.storey;
                    storeyGlobalId = found->second.storeyGlobalId;
                }
                result.products.push_back({std::move(shape),
                    QString::fromStdString(element->guid()), type,
                    QString::fromStdString(element->name()), {},
                    building, buildingGlobalId, storey, storeyGlobalId,
                    representationTypes(element->product()),
                    placementDepth(element->product()), semanticMetadata(element->product(), semantic)});
                ++result.statistics.productsWithGeometry;
                ++result.statistics.importedCount;
                ++result.statistics.importedByEntity[type];
            } catch (const std::exception& exception) {
                ++result.statistics.failedCount;
                ++result.statistics.failedByEntity[type];
                result.diagnostics.push_back({ImportSeverity::Warning,
                    QString::fromStdString(element->guid()), type,
                    QString::fromUtf8(exception.what())});
            }
        } while (iterator.next());

        result.statistics.geometryMilliseconds = geometry.elapsed();
        if (progress) progress({IfcImportStage::Finalizing, processed, processed});
        result.statistics.totalMilliseconds = total.elapsed();
        return result;
    } catch (const std::exception& exception) {
        result.diagnostics.push_back({ImportSeverity::Error, {}, {},
            QString::fromUtf8(exception.what())});
        result.fatal = true;
        result.statistics.failedCount = 1;
        result.statistics.totalMilliseconds = total.elapsed();
        return result;
    }
#endif
}

} // namespace cad::import
