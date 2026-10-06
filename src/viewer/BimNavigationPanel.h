#pragma once

#include "application/BimNavigationModel.h"
#include "application/VisibilityMode.h"

#include <QWidget>
#include <QStringList>

#include <functional>
#include <map>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

class BimNavigationPanel final : public QWidget
{
public:
    explicit BimNavigationPanel(QWidget* parent = nullptr);

    void setNavigation(cad::application::BimNavigationModel model,
                       const std::map<QString, cad::application::VisibilityMode>& modes);
    void updateVisibility(
        const std::map<QString, cad::application::VisibilityMode>& modes);
    void selectFeatures(const QStringList& featureIds);

    void setFeatureSelectedHandler(std::function<void(const QStringList&)> handler);
    void setShowHideHandler(std::function<void(const QStringList&, bool)> handler);
    void setIsolateHandler(std::function<void(const QStringList&)> handler);
    void setClearIsolationHandler(std::function<void()> handler);

private:
    enum class NodeKind { Building, Storey, Category, Feature };

    void rebuildTree();
    void filterTree();
    void updateItemState(QTreeWidgetItem* item);
    void showContextMenu(const QPoint& position);
    QStringList idsForItem(const QTreeWidgetItem* item) const;
    bool itemMatchesSearch(const QTreeWidgetItem* item, const QString& text) const;
    static QString statePrefix(const std::vector<cad::application::VisibilityMode>& modes);

    QLineEdit* search_{nullptr};
    QTreeWidget* tree_{nullptr};
    cad::application::BimNavigationModel model_;
    std::map<QString, cad::application::VisibilityMode> modes_;
    std::function<void(const QStringList&)> featureSelectedHandler_;
    std::function<void(const QStringList&, bool)> showHideHandler_;
    std::function<void(const QStringList&)> isolateHandler_;
    std::function<void()> clearIsolationHandler_;
};
