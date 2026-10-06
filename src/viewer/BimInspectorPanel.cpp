#include "viewer/BimInspectorPanel.h"

#include "operations/ImportedFeature.h"

#include <QHeaderView>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
void addValue(QTreeWidgetItem* section, const QString& name, const QString& value)
{
    if (value.isEmpty()) return;
    auto* item = new QTreeWidgetItem(section, {name, value});
    item->setToolTip(1, value);
}
}

BimInspectorPanel::BimInspectorPanel(QWidget* parent) : QWidget(parent), tree_(new QTreeWidget(this))
{
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({"Property", "Value"});
    tree_->header()->setStretchLastSection(true);
    tree_->setRootIsDecorated(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(tree_);
    clear();
}

void BimInspectorPanel::clear()
{
    tree_->clear();
    tree_->addTopLevelItem(new QTreeWidgetItem({"BIM Inspector", "No IFC feature selected"}));
}

void BimInspectorPanel::setFeature(const cad::parametric::ImportedFeature* feature)
{
    if (!feature) { clear(); return; }
    tree_->clear();
    auto* identity = new QTreeWidgetItem(tree_, {"Identity", {}});
    addValue(identity, "Name", QString::fromStdString(feature->name()));
    addValue(identity, "IFC Entity Type", feature->ifcEntityType());
    addValue(identity, "GlobalId", feature->ifcGlobalId());
    addValue(identity, "Description", feature->ifcDescription());
    addValue(identity, "PredefinedType", feature->ifcMetadata().predefinedType);

    auto* spatial = new QTreeWidgetItem(tree_, {"Spatial", {}});
    addValue(spatial, "Building", feature->ifcBuilding());
    addValue(spatial, "Building GlobalId", feature->ifcBuildingGlobalId());
    addValue(spatial, "Storey", feature->ifcStorey());
    addValue(spatial, "Storey GlobalId", feature->ifcStoreyGlobalId());

    const auto& metadata = feature->ifcMetadata();
    if (!metadata.type.entityType.isEmpty() || !metadata.type.name.isEmpty()) {
        auto* type = new QTreeWidgetItem(tree_, {"IFC Type", {}});
        addValue(type, "Entity Type", metadata.type.entityType);
        addValue(type, "GlobalId", metadata.type.globalId);
        addValue(type, "Name", metadata.type.name);
    }
    if (!metadata.materials.empty()) {
        auto* materials = new QTreeWidgetItem(tree_, {"Materials", {}});
        for (const auto& material : metadata.materials) {
            auto* item = new QTreeWidgetItem(materials, {material.name, {}});
            for (const auto& layer : material.layers)
                addValue(item, layer.name.isEmpty() ? "Layer" : "Layer", layer.thickness.isEmpty()
                    ? layer.name : layer.name + (layer.thickness.isEmpty() ? QString{} : " (" + layer.thickness + ")"));
        }
    }
    if (!metadata.propertySets.empty()) {
        auto* sets = new QTreeWidgetItem(tree_, {"Property Sets", {}});
        for (const auto& set : metadata.propertySets) {
            auto* item = new QTreeWidgetItem(sets, {set.name, {}});
            for (const auto& property : set.properties)
                addValue(item, property.name, property.value.text);
        }
    }
    if (!metadata.quantitySets.empty()) {
        auto* sets = new QTreeWidgetItem(tree_, {"Quantities", {}});
        for (const auto& set : metadata.quantitySets) {
            auto* item = new QTreeWidgetItem(sets, {set.name, {}});
            for (const auto& quantity : set.quantities)
                addValue(item, quantity.name + " (" + quantity.kind + ")", quantity.value);
        }
    }
    auto* source = new QTreeWidgetItem(tree_, {"Source", {}});
    addValue(source, "Format", feature->sourceFormat());
    addValue(source, "File", feature->sourceFile());
    tree_->expandToDepth(1);
}
