#pragma once

#include <TopoDS_Shape.hxx>

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <map>
#include <functional>
#include <vector>

namespace cad::import {

enum class ImportSeverity { Info, Warning, Error };

enum class IfcImportStage { Opening, Parsing, Geometry, Finalizing };

struct IfcImportProgress
{
    IfcImportStage stage{IfcImportStage::Opening};
    std::size_t processed{0};
    std::size_t total{0};
};

using IfcImportProgressCallback = std::function<void(const IfcImportProgress&)>;
using IfcImportCancellation = std::function<bool()>;

struct IfcImportDiagnostic
{
    ImportSeverity severity{ImportSeverity::Info};
    QString globalId;
    QString entityType;
    QString message;
};

struct IfcImportProduct
{
    TopoDS_Shape shape;
    QString globalId;
    QString entityType;
    QString name;
    QString description;
    QString building;
    QString storey;
    QStringList representationTypes;
    int placementDepth{0};
};

struct IfcImportStatistics
{
    QString schema;
    int consideredCount{0};
    int productsWithGeometry{0};
    int importedCount{0};
    int skippedCount{0};
    int failedCount{0};
    qint64 parseMilliseconds{0};
    qint64 geometryMilliseconds{0};
    qint64 totalMilliseconds{0};
    std::map<QString, int> consideredByEntity;
    std::map<QString, int> importedByEntity;
    std::map<QString, int> failedByEntity;
};

struct IfcImportResult
{
    IfcImportStatistics statistics;
    std::vector<IfcImportProduct> products;
    std::vector<IfcImportDiagnostic> diagnostics;
    bool cancelled{false};
    bool fatal{false};

    bool succeeded() const noexcept { return !fatal; }
};

class IfcOpenShellAdapter final
{
public:
    IfcImportResult importFile(const QString& path,
                               IfcImportProgressCallback progress = {},
                               IfcImportCancellation cancellation = {}) const;
};

} // namespace cad::import
