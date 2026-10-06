#include "operations/ImportedFeature.h"

#include "model/ShapePayload.h"

#include <QJsonObject>
#include <stdexcept>

namespace cad::parametric {

ImportedFeature::ImportedFeature(std::string id, std::string name, TopoDS_Shape sourceShape)
    : ParametricFeature(std::move(id), std::move(name)), sourceShape_(std::move(sourceShape))
{
    if (sourceShape_.IsNull()) throw std::invalid_argument("Imported feature shape must not be null");
    if (!recompute()) throw std::runtime_error(error());
}

ImportedFeature::ImportedFeature(std::string id, std::string name, TopoDS_Shape sourceShape,
                                 QString sourceFormat, QString sourceFile, QString ifcGlobalId,
                                 QString ifcEntityType, QString ifcDescription,
                                 QString ifcBuilding, QString ifcStorey,
                                 QString ifcBuildingGlobalId, QString ifcStoreyGlobalId)
    : ParametricFeature(std::move(id), std::move(name)), sourceShape_(std::move(sourceShape)),
      sourceFormat_(std::move(sourceFormat)), sourceFile_(std::move(sourceFile)),
      ifcGlobalId_(std::move(ifcGlobalId)), ifcEntityType_(std::move(ifcEntityType)),
      ifcDescription_(std::move(ifcDescription)), ifcBuilding_(std::move(ifcBuilding)),
      ifcBuildingGlobalId_(std::move(ifcBuildingGlobalId)), ifcStorey_(std::move(ifcStorey)),
      ifcStoreyGlobalId_(std::move(ifcStoreyGlobalId))
{
    if (sourceShape_.IsNull()) throw std::invalid_argument("Imported feature shape must not be null");
    if (!recompute()) throw std::runtime_error(error());
}

const char* ImportedFeature::typeId() const noexcept { return "IfcImported"; }
FeatureRole ImportedFeature::role() const noexcept { return FeatureRole::Generic; }

std::vector<FeatureProperty> ImportedFeature::properties() const
{
    return {
        {"ifcGlobalId", "IFC GlobalId", ifcGlobalId_.toStdString(), {}, {}, false},
        {"ifcEntityType", "IFC entity type", ifcEntityType_.toStdString(), {}, {}, false},
        {"ifcBuilding", "IFC building", ifcBuilding_.toStdString(), {}, {}, false},
        {"ifcBuildingGlobalId", "IFC building GlobalId", ifcBuildingGlobalId_.toStdString(), {}, {}, false},
        {"ifcStorey", "IFC storey", ifcStorey_.toStdString(), {}, {}, false},
        {"ifcStoreyGlobalId", "IFC storey GlobalId", ifcStoreyGlobalId_.toStdString(), {}, {}, false}
    };
}

ParametricFeature::Ptr ImportedFeature::clone(std::string newId) const
{
    auto result = std::make_shared<ImportedFeature>(std::move(newId), name(), sourceShape_,
        sourceFormat_, sourceFile_, ifcGlobalId_, ifcEntityType_, ifcDescription_,
        ifcBuilding_, ifcStorey_, ifcBuildingGlobalId_, ifcStoreyGlobalId_);
    result->setUserVisible(userVisible());
    return result;
}

const TopoDS_Shape& ImportedFeature::sourceShape() const noexcept { return sourceShape_; }
const QString& ImportedFeature::sourceFormat() const noexcept { return sourceFormat_; }
const QString& ImportedFeature::sourceFile() const noexcept { return sourceFile_; }
const QString& ImportedFeature::ifcGlobalId() const noexcept { return ifcGlobalId_; }
const QString& ImportedFeature::ifcEntityType() const noexcept { return ifcEntityType_; }
const QString& ImportedFeature::ifcDescription() const noexcept { return ifcDescription_; }
const QString& ImportedFeature::ifcBuilding() const noexcept { return ifcBuilding_; }
const QString& ImportedFeature::ifcBuildingGlobalId() const noexcept { return ifcBuildingGlobalId_; }
const QString& ImportedFeature::ifcStorey() const noexcept { return ifcStorey_; }
const QString& ImportedFeature::ifcStoreyGlobalId() const noexcept { return ifcStoreyGlobalId_; }

TopoDS_Shape ImportedFeature::build() const { return sourceShape_; }

void ImportedFeature::writeParameters(QJsonObject& object) const
{
    object.insert("sourceFormat", sourceFormat_);
    object.insert("sourceFile", sourceFile_);
    object.insert("ifcGlobalId", ifcGlobalId_);
    object.insert("ifcEntityType", ifcEntityType_);
    object.insert("ifcDescription", ifcDescription_);
    object.insert("ifcBuilding", ifcBuilding_);
    object.insert("ifcBuildingGlobalId", ifcBuildingGlobalId_);
    object.insert("ifcStorey", ifcStorey_);
    object.insert("ifcStoreyGlobalId", ifcStoreyGlobalId_);
}

} // namespace cad::parametric
