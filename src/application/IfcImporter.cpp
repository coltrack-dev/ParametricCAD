#include "application/IfcImporter.h"

#include "model/Body.h"
#include "operations/ImportedFeature.h"

#include <QSet>

namespace cad::application {

cad::import::IfcImportResult IfcImporter::importIntoBody(
    const QString& path, cad::parametric::Body& body) const
{
    cad::import::IfcOpenShellAdapter adapter;
    auto result = adapter.importFile(path);
    if (result.products.empty()) return result;

    QSet<QString> usedIds;
    for (const auto& feature : body.features())
        usedIds.insert(QString::fromStdString(feature->id()));

    for (auto& product : result.products) {
        QString id = QStringLiteral("ifc-") + product.globalId;
        if (id == "ifc-") id = QStringLiteral("ifc-imported");
        const QString baseId = id;
        int suffix = 2;
        while (usedIds.contains(id)) id = baseId + QStringLiteral("-%1").arg(suffix++);
        usedIds.insert(id);
        body.addFeature(std::make_shared<cad::parametric::ImportedFeature>(
            id.toStdString(), product.name.isEmpty() ? product.entityType.toStdString()
                                                       : product.name.toStdString(),
            std::move(product.shape), QStringLiteral("IFC"), path,
            product.globalId, product.entityType, product.description,
            product.building, product.storey));
    }
    return result;
}

} // namespace cad::application
