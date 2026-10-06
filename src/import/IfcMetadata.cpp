#include "import/IfcMetadata.h"

#include <QJsonArray>

namespace cad::import {

bool IfcMetadata::empty() const noexcept
{
    return predefinedType.isEmpty() && type.entityType.isEmpty()
        && type.globalId.isEmpty() && type.name.isEmpty()
        && materials.empty() && propertySets.empty() && quantitySets.empty();
}

QJsonObject serializeIfcMetadata(const IfcMetadata& metadata)
{
    QJsonObject result;
    result.insert("predefinedType", metadata.predefinedType);
    QJsonObject type;
    type.insert("entityType", metadata.type.entityType);
    type.insert("globalId", metadata.type.globalId);
    type.insert("name", metadata.type.name);
    result.insert("type", type);

    QJsonArray materials;
    for (const auto& material : metadata.materials) {
        QJsonObject object;
        object.insert("name", material.name);
        QJsonArray layers;
        for (const auto& layer : material.layers) {
            QJsonObject item;
            item.insert("name", layer.name);
            item.insert("thickness", layer.thickness);
            layers.append(item);
        }
        object.insert("layers", layers);
        materials.append(object);
    }
    result.insert("materials", materials);

    QJsonArray propertySets;
    for (const auto& set : metadata.propertySets) {
        QJsonObject object;
        object.insert("name", set.name);
        QJsonArray properties;
        for (const auto& property : set.properties) {
            QJsonObject item;
            item.insert("name", property.name);
            item.insert("kind", property.value.kind);
            item.insert("value", property.value.text);
            properties.append(item);
        }
        object.insert("properties", properties);
        propertySets.append(object);
    }
    result.insert("propertySets", propertySets);

    QJsonArray quantitySets;
    for (const auto& set : metadata.quantitySets) {
        QJsonObject object;
        object.insert("name", set.name);
        QJsonArray quantities;
        for (const auto& quantity : set.quantities) {
            QJsonObject item;
            item.insert("name", quantity.name);
            item.insert("kind", quantity.kind);
            item.insert("value", quantity.value);
            quantities.append(item);
        }
        object.insert("quantities", quantities);
        quantitySets.append(object);
    }
    result.insert("quantitySets", quantitySets);
    return result;
}

IfcMetadata deserializeIfcMetadata(const QJsonObject& object)
{
    IfcMetadata metadata;
    metadata.predefinedType = object.value("predefinedType").toString();
    const auto type = object.value("type").toObject();
    metadata.type.entityType = type.value("entityType").toString();
    metadata.type.globalId = type.value("globalId").toString();
    metadata.type.name = type.value("name").toString();

    for (const auto& value : object.value("materials").toArray()) {
        const auto item = value.toObject();
        IfcMaterialData material;
        material.name = item.value("name").toString();
        for (const auto& layerValue : item.value("layers").toArray()) {
            const auto layer = layerValue.toObject();
            material.layers.push_back({layer.value("name").toString(),
                                       layer.value("thickness").toString()});
        }
        metadata.materials.push_back(std::move(material));
    }
    for (const auto& value : object.value("propertySets").toArray()) {
        const auto item = value.toObject();
        IfcPropertySetData set;
        set.name = item.value("name").toString();
        for (const auto& propertyValue : item.value("properties").toArray()) {
            const auto property = propertyValue.toObject();
            set.properties.push_back({property.value("name").toString(),
                {property.value("kind").toString(), property.value("value").toString()}});
        }
        metadata.propertySets.push_back(std::move(set));
    }
    for (const auto& value : object.value("quantitySets").toArray()) {
        const auto item = value.toObject();
        IfcQuantitySetData set;
        set.name = item.value("name").toString();
        for (const auto& quantityValue : item.value("quantities").toArray()) {
            const auto quantity = quantityValue.toObject();
            set.quantities.push_back({quantity.value("name").toString(),
                                      quantity.value("kind").toString(),
                                      quantity.value("value").toString()});
        }
        metadata.quantitySets.push_back(std::move(set));
    }
    return metadata;
}

} // namespace cad::import
