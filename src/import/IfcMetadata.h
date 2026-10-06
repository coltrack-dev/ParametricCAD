#pragma once

#include <QJsonObject>
#include <QString>

#include <vector>

namespace cad::import {

struct IfcValue
{
    QString kind;
    QString text;
};

struct IfcPropertyValue
{
    QString name;
    IfcValue value;
};

struct IfcPropertySetData
{
    QString name;
    std::vector<IfcPropertyValue> properties;
};

struct IfcQuantityValue
{
    QString name;
    QString kind;
    QString value;
};

struct IfcQuantitySetData
{
    QString name;
    std::vector<IfcQuantityValue> quantities;
};

struct IfcMaterialLayerData
{
    QString name;
    QString thickness;
};

struct IfcMaterialData
{
    QString name;
    std::vector<IfcMaterialLayerData> layers;
};

struct IfcTypeInfo
{
    QString entityType;
    QString globalId;
    QString name;
};

struct IfcMetadata
{
    QString predefinedType;
    IfcTypeInfo type;
    std::vector<IfcMaterialData> materials;
    std::vector<IfcPropertySetData> propertySets;
    std::vector<IfcQuantitySetData> quantitySets;

    bool empty() const noexcept;
};

QJsonObject serializeIfcMetadata(const IfcMetadata& metadata);
IfcMetadata deserializeIfcMetadata(const QJsonObject& object);

} // namespace cad::import
