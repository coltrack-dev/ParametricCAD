#include "viewer/BimNavigationPanel.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int KindRole = Qt::UserRole;
constexpr int IdRole = Qt::UserRole + 1;
constexpr int SearchRole = Qt::UserRole + 2;
constexpr int CountRole = Qt::UserRole + 3;
constexpr int LabelRole = Qt::UserRole + 4;
}

BimNavigationPanel::BimNavigationPanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText("Search IFC name, GlobalId, type, or storey...");
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(search_);
    layout->addWidget(tree_);

    connect(search_, &QLineEdit::textChanged, this, [this]() { filterTree(); });
    connect(tree_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item, int) {
            if (item->data(0, KindRole).toInt() == static_cast<int>(NodeKind::Feature)
                && featureSelectedHandler_) {
                featureSelectedHandler_(idsForItem(item));
            }
        });
    connect(tree_, &QTreeWidget::customContextMenuRequested, this,
        [this](const QPoint& point) { showContextMenu(point); });
}

void BimNavigationPanel::setNavigation(
    cad::application::BimNavigationModel model,
    const std::map<QString, cad::application::VisibilityMode>& modes)
{
    model_ = std::move(model);
    modes_ = modes;
    rebuildTree();
}

void BimNavigationPanel::updateVisibility(
    const std::map<QString, cad::application::VisibilityMode>& modes)
{
    modes_ = modes;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        auto* root = tree_->topLevelItem(i);
        std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
            updateItemState(item);
            for (int child = 0; child < item->childCount(); ++child) visit(item->child(child));
        };
        visit(root);
    }
}

QString BimNavigationPanel::statePrefix(
    const std::vector<cad::application::VisibilityMode>& modes)
{
    if (modes.empty()) return QStringLiteral("  ");
    const bool allHidden = std::all_of(modes.begin(), modes.end(),
        [](auto mode) { return mode == cad::application::VisibilityMode::Hidden; });
    const bool allVisible = std::all_of(modes.begin(), modes.end(),
        [](auto mode) { return mode != cad::application::VisibilityMode::Hidden; });
    return allHidden ? QStringLiteral("○ ") : allVisible ? QStringLiteral("● ")
                                                          : QStringLiteral("◐ ");
}

void BimNavigationPanel::rebuildTree()
{
    QSignalBlocker blocker(tree_);
    tree_->clear();
    for (const auto& building : model_.buildings()) {
        auto* buildingItem = new QTreeWidgetItem(tree_);
        buildingItem->setData(0, KindRole, static_cast<int>(NodeKind::Building));
        buildingItem->setData(0, IdRole, QString::fromStdString(building.name));
        buildingItem->setData(0, SearchRole, QString::fromStdString(building.name));
        int featureCount = 0;
        for (const auto& storey : building.storeys) {
            auto* storeyItem = new QTreeWidgetItem(buildingItem);
            storeyItem->setData(0, KindRole, static_cast<int>(NodeKind::Storey));
            storeyItem->setData(0, IdRole, QString::fromStdString(storey.name));
            storeyItem->setData(0, SearchRole, QString::fromStdString(storey.name));
            for (const auto& category : storey.categories) {
                auto* categoryItem = new QTreeWidgetItem(storeyItem);
                categoryItem->setData(0, KindRole, static_cast<int>(NodeKind::Category));
                categoryItem->setData(0, IdRole, QString::fromStdString(category.name));
                categoryItem->setData(0, SearchRole, QString::fromStdString(category.name));
                for (const auto& feature : category.features) {
                    ++featureCount;
                    auto* featureItem = new QTreeWidgetItem(categoryItem);
                    featureItem->setData(0, KindRole, static_cast<int>(NodeKind::Feature));
                    featureItem->setData(0, IdRole, QString::fromStdString(feature.featureId));
                    featureItem->setData(0, SearchRole, QString::fromStdString(
                        feature.name + " " + feature.globalId + " " + feature.entityType
                        + " " + feature.storey));
                    featureItem->setData(0, LabelRole, QString::fromStdString(feature.name));
                }
                categoryItem->setData(0, CountRole,
                    static_cast<int>(category.features.size()));
            }
            storeyItem->setData(0, CountRole, [&]() {
                int count = 0;
                for (const auto& category : storey.categories)
                    count += static_cast<int>(category.features.size());
                return count;
            }());
        }
        buildingItem->setData(0, CountRole, featureCount);
        buildingItem->setExpanded(true);
    }
    updateVisibility(modes_);
    filterTree();
}

QStringList BimNavigationPanel::idsForItem(const QTreeWidgetItem* item) const
{
    QStringList ids;
    if (!item) return ids;
    const auto kind = static_cast<NodeKind>(item->data(0, KindRole).toInt());
    if (kind == NodeKind::Feature) {
        ids << item->data(0, IdRole).toString();
        return ids;
    }
    std::function<void(const QTreeWidgetItem*)> visit = [&](const QTreeWidgetItem* node) {
        if (static_cast<NodeKind>(node->data(0, KindRole).toInt()) == NodeKind::Feature)
            ids << node->data(0, IdRole).toString();
        for (int i = 0; i < node->childCount(); ++i) visit(node->child(i));
    };
    visit(item);
    return ids;
}

void BimNavigationPanel::updateItemState(QTreeWidgetItem* item)
{
    const auto ids = idsForItem(item);
    std::vector<cad::application::VisibilityMode> states;
    states.reserve(ids.size());
    for (const auto& id : ids) {
        const auto found = modes_.find(id);
        states.push_back(found == modes_.end()
            ? cad::application::VisibilityMode::Visible : found->second);
    }
    const auto base = item->data(0, LabelRole).toString().isEmpty()
        ? item->data(0, IdRole).toString() : item->data(0, LabelRole).toString();
    const auto kind = static_cast<NodeKind>(item->data(0, KindRole).toInt());
    if (kind == NodeKind::Feature) {
        item->setText(0, statePrefix(states) + base);
    } else {
        const auto count = item->data(0, CountRole).toInt();
        item->setText(0, statePrefix(states) + base + QString(" (%1)").arg(count));
    }
}

bool BimNavigationPanel::itemMatchesSearch(const QTreeWidgetItem* item,
                                           const QString& text) const
{
    return text.isEmpty() || item->data(0, SearchRole).toString()
        .contains(text, Qt::CaseInsensitive);
}

void BimNavigationPanel::filterTree()
{
    const auto text = search_->text().trimmed();
    std::function<bool(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        bool childMatch = false;
        for (int i = 0; i < item->childCount(); ++i)
            childMatch = visit(item->child(i)) || childMatch;
        const bool match = itemMatchesSearch(item, text) || childMatch;
        item->setHidden(!match);
        return match;
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) visit(tree_->topLevelItem(i));
}

void BimNavigationPanel::selectFeatures(const QStringList& featureIds)
{
    if (featureIds.isEmpty()) return;
    const auto wanted = featureIds.front();
    QSignalBlocker blocker(tree_);
    std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find = [&](QTreeWidgetItem* item) {
        if (item->data(0, KindRole).toInt() == static_cast<int>(NodeKind::Feature)
            && item->data(0, IdRole).toString() == wanted) return item;
        for (int i = 0; i < item->childCount(); ++i)
            if (auto* result = find(item->child(i))) return result;
        return static_cast<QTreeWidgetItem*>(nullptr);
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        if (auto* item = find(tree_->topLevelItem(i))) {
            tree_->setCurrentItem(item);
            tree_->scrollToItem(item);
            return;
        }
    }
}

void BimNavigationPanel::showContextMenu(const QPoint& position)
{
    auto* item = tree_->itemAt(position);
    if (!item) return;
    const auto ids = idsForItem(item);
    if (ids.isEmpty()) return;
    QMenu menu(this);
    auto* select = menu.addAction("Select All");
    auto* show = menu.addAction("Show");
    auto* hide = menu.addAction("Hide");
    auto* isolate = menu.addAction("Isolate");
    menu.addSeparator();
    auto* clear = menu.addAction("Clear Isolation");
    const auto kind = static_cast<NodeKind>(item->data(0, KindRole).toInt());
    select->setEnabled(kind != NodeKind::Feature);
    if (kind == NodeKind::Feature) select->setText("Select");
    const auto chosen = menu.exec(tree_->viewport()->mapToGlobal(position));
    if (chosen == select && featureSelectedHandler_) featureSelectedHandler_(ids);
    else if (chosen == show && showHideHandler_) showHideHandler_(ids, true);
    else if (chosen == hide && showHideHandler_) showHideHandler_(ids, false);
    else if (chosen == isolate && isolateHandler_) isolateHandler_(ids);
    else if (chosen == clear && clearIsolationHandler_) clearIsolationHandler_();
}

void BimNavigationPanel::setFeatureSelectedHandler(std::function<void(const QStringList&)> handler)
{ featureSelectedHandler_ = std::move(handler); }
void BimNavigationPanel::setShowHideHandler(std::function<void(const QStringList&, bool)> handler)
{ showHideHandler_ = std::move(handler); }
void BimNavigationPanel::setIsolateHandler(std::function<void(const QStringList&)> handler)
{ isolateHandler_ = std::move(handler); }
void BimNavigationPanel::setClearIsolationHandler(std::function<void()> handler)
{ clearIsolationHandler_ = std::move(handler); }
