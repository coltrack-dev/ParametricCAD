#include "application/IfcImporter.h"

#include "model/Body.h"
#include "operations/ImportedFeature.h"

#include <QSet>

namespace cad::application {

cad::import::IfcImportResult IfcImporter::prepare(
    const QString& path,
    cad::import::IfcImportProgressCallback progress,
    cad::import::IfcImportCancellation cancellation) const
{
    cad::import::IfcOpenShellAdapter adapter;
    return adapter.importFile(path, std::move(progress), std::move(cancellation));
}

std::vector<cad::parametric::ParametricFeature::Ptr> IfcImporter::makeFeatures(
    const cad::import::IfcImportResult& result,
    const QString& path,
    const cad::parametric::Body& body) const
{
    std::vector<cad::parametric::ParametricFeature::Ptr> features;
    features.reserve(result.products.size());
    QSet<QString> usedIds;
    for (const auto& feature : body.features())
        usedIds.insert(QString::fromStdString(feature->id()));

    for (const auto& product : result.products) {
        QString id = QStringLiteral("ifc-") + product.globalId;
        if (id == "ifc-") id = QStringLiteral("ifc-imported");
        const QString baseId = id;
        int suffix = 2;
        while (usedIds.contains(id)) id = baseId + QStringLiteral("-%1").arg(suffix++);
        usedIds.insert(id);
        features.push_back(std::make_shared<cad::parametric::ImportedFeature>(
            id.toStdString(), product.name.isEmpty() ? product.entityType.toStdString()
                                                       : product.name.toStdString(),
            product.shape, QStringLiteral("IFC"), path,
            product.globalId, product.entityType, product.description,
            product.building, product.storey,
            product.buildingGlobalId, product.storeyGlobalId));
    }
    return features;
}

cad::import::IfcImportResult IfcImporter::importIntoBody(
    const QString& path, cad::parametric::Body& body) const
{
    auto result = prepare(path);
    if (!result.cancelled && result.succeeded()) {
        for (auto& feature : makeFeatures(result, path, body)) body.addFeature(feature);
    }
    return result;
}

} // namespace cad::application
