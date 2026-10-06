#pragma once

#include "model/ParametricFeature.h"
#include "import/IfcMetadata.h"

#include <TopoDS_Shape.hxx>

#include <QString>

namespace cad::parametric {

class ImportedFeature final : public ParametricFeature
{
public:
    ImportedFeature(std::string id, std::string name, TopoDS_Shape sourceShape);
    ImportedFeature(std::string id, std::string name, TopoDS_Shape sourceShape,
                    QString sourceFormat, QString sourceFile, QString ifcGlobalId,
                    QString ifcEntityType, QString ifcDescription,
                    QString ifcBuilding, QString ifcStorey,
                    QString ifcBuildingGlobalId = {}, QString ifcStoreyGlobalId = {},
                    cad::import::IfcMetadata ifcMetadata = {});

    const char* typeId() const noexcept override;
    FeatureRole role() const noexcept override;
    std::vector<FeatureProperty> properties() const override;
    Ptr clone(std::string newId) const override;

    const TopoDS_Shape& sourceShape() const noexcept;
    const QString& sourceFormat() const noexcept;
    const QString& sourceFile() const noexcept;
    const QString& ifcGlobalId() const noexcept;
    const QString& ifcEntityType() const noexcept;
    const QString& ifcDescription() const noexcept;
    const QString& ifcBuilding() const noexcept;
    const QString& ifcBuildingGlobalId() const noexcept;
    const QString& ifcStorey() const noexcept;
    const QString& ifcStoreyGlobalId() const noexcept;
    const cad::import::IfcMetadata& ifcMetadata() const noexcept;

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    TopoDS_Shape sourceShape_;
    QString sourceFormat_{QStringLiteral("IFC")};
    QString sourceFile_;
    QString ifcGlobalId_;
    QString ifcEntityType_;
    QString ifcDescription_;
    QString ifcBuilding_;
    QString ifcBuildingGlobalId_;
    QString ifcStorey_;
    QString ifcStoreyGlobalId_;
    cad::import::IfcMetadata ifcMetadata_;
};

} // namespace cad::parametric
