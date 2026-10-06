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
                    placementDepth(element->product())});
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
