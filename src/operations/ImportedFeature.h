#pragma once

#include "model/ParametricFeature.h"

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
                    QString ifcBuilding, QString ifcStorey);

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
    const QString& ifcStorey() const noexcept;

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
    QString ifcStorey_;
};

} // namespace cad::parametric
