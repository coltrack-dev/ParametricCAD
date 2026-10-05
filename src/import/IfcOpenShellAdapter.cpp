#include "import/IfcOpenShellAdapter.h"

#include <QElapsedTimer>

#include <algorithm>
#include <memory>
#include <stdexcept>

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

QString parentValue(const IfcGeom::Element& element, const char* requestedType)
{
    for (const auto* parent : element.parents()) {
        if (parent && parent->type() == requestedType)
            return QString::fromStdString(parent->name());
    }
    return {};
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

                result.products.push_back({std::move(shape),
                    QString::fromStdString(element->guid()), type,
                    QString::fromStdString(element->name()), {},
                    parentValue(*element, "IfcBuilding"),
                    parentValue(*element, "IfcBuildingStorey"),
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
