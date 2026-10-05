#pragma once

#include <QWidget>
#include <QStringList>
#include "application/FeatureEditingService.h"
#include "application/VisibilityManager.h"
#include "operations/ParametricFeatures.h"

#include <functional>
#include <string>
#include <vector>

class QFormLayout;
class QLabel;
class QTreeWidget;
class QListWidget;
class QPushButton;

struct SketchConstraintListItem
{
    cad::parametric::SketchConstraintId id;
    QString label;
    bool editable{false};
};

class FeatureEditorPanel final : public QWidget
{
public:
    explicit FeatureEditorPanel(QWidget* parent = nullptr);

    void setService(cad::application::FeatureEditingService* service);
    void setFeatures(std::vector<cad::application::FeatureDescriptor> features);
    void setVisibilityGroups(std::vector<cad::application::VisibilityGroup> groups);
    void setVisibilityFilters(cad::application::VisibilityFilterState filters);
    void setVisibilityPresets(std::vector<cad::application::VisibilityPreset> presets);
    void updateVisibilityPresentation(
        std::vector<cad::application::FeatureDescriptor> features,
        std::vector<cad::application::VisibilityGroup> groups,
        cad::application::VisibilityFilterState filters,
        std::vector<cad::application::VisibilityPreset> presets);
    void setActionState(const cad::application::ModelingActionState& state);
    void scheduleRefresh();
    void commitPendingEdits();
    void setModelChangedHandler(std::function<void()> handler);
    void setFeatureSelectedHandler(
        std::function<void(const QStringList&)> handler
    );
    void setFeatureDoubleClickedHandler(std::function<void(const QString&)> handler);
    void setVisibilityHandlers(
        std::function<void(const QStringList&)> hide,
        std::function<void(const QStringList&)> show,
        std::function<void(const QStringList&)> isolate,
        std::function<void()> showAll,
        std::function<void(const QStringList&)> ghostOthers,
        std::function<void()> clearGhosting);
    void setGroupHandlers(
        std::function<void(const QStringList&)> createGroup,
        std::function<void(const QString&, cad::application::VisibilityMode)> setVisibility,
        std::function<void(const QString&)> isolateGroup,
        std::function<void(const QString&)> removeGroup,
        std::function<void(const QString&, const QStringList&)> addToGroup,
        std::function<void(const QString&, const QStringList&)> removeFromGroup);
    void setFilterHandlers(
        std::function<void(cad::application::VisibilityCategory,
                           std::optional<cad::application::VisibilityMode>)> category,
        std::function<void(const QString&,
                           std::optional<cad::application::VisibilityMode>)> type,
        std::function<void(cad::parametric::FeatureRole,
                           std::optional<cad::application::VisibilityMode>)> role,
        std::function<void()> clear,
        std::function<void(cad::application::VisibilityCategory)> showOnlyCategory);
    void setPresetHandlers(
        std::function<void()> saveCurrent,
        std::function<void(const QString&)> apply,
        std::function<void(const QString&)> update,
        std::function<void(const QString&)> rename,
        std::function<void(const QString&)> remove);
    void refresh();
    QStringList selectedFeatureIds() const;
    void selectFeatures(const QStringList& featureIds);
    void setSketchConstraints(std::vector<SketchConstraintListItem> items, bool editable);
    void clearSketchConstraintSelection();
    void setSketchConstraintSelected(const QString& id);
    void setSketchConstraintSelectionHandler(std::function<void(const QString&)> handler);
    void setSketchConstraintEditHandler(std::function<void(const QString&)> handler);
    void setSketchConstraintDeleteHandler(std::function<void(const QString&)> handler);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createUi();
    void updateSelectedProperties();

    void addBox();
    void addCylinder();
    void addCone();
    void addSphere();
    void addTorus();
    void addHexagon();

    void addBoolean(cad::application::BooleanKind operation, const QString& operationName);

    void showFeature(const std::string& featureId);

    void rebuildProperties(const cad::application::FeatureDescriptor* feature);
    void clearProperties();

    void reportResult(const cad::application::ModelingResult& result);

    bool propertyMatchesCurrentValue(
        const std::string& featureId,
        const std::string& propertyKey,
        const cad::parametric::PropertyValue& value
    ) const;

    void setPanelMessage(
        const QString& message,
        bool error = false
    );

    cad::application::FeatureEditingService* service_{nullptr};
    std::vector<cad::application::FeatureDescriptor> features_;
    std::vector<cad::application::VisibilityGroup> groups_;
    cad::application::VisibilityFilterState filters_;
    std::vector<cad::application::VisibilityPreset> presets_;
    bool canDelete_{false};
    bool canCreateFace_{false};
    bool canExtrude_{false};
    bool canBoolean_{false};
    bool canFillet_{false};
    bool canChamfer_{false};
    bool canSketchOnFace_{false};
    bool refreshPending_{false};
    bool updatingProperties_{false};
    bool committingPendingEdit_{false};

    QTreeWidget* tree_{nullptr};
    QWidget* propertiesWidget_{nullptr};
    QFormLayout* propertiesLayout_{nullptr};
    QLabel* messageLabel_{nullptr};
    QListWidget* constraintList_{nullptr};
    QPushButton* editConstraintButton_{nullptr};
    QPushButton* deleteConstraintButton_{nullptr};
    std::function<void(const QString&)> sketchConstraintSelectionHandler_;
    std::function<void(const QString&)> sketchConstraintEditHandler_;
    std::function<void(const QString&)> sketchConstraintDeleteHandler_;

    std::function<void()> modelChangedHandler_;
    std::function<void(const QStringList&)> featureSelectedHandler_;
    std::function<void(const QString&)> featureDoubleClickedHandler_;
    std::function<void(const QStringList&)> hideHandler_;
    std::function<void(const QStringList&)> showHandler_;
    std::function<void(const QStringList&)> isolateHandler_;
    std::function<void()> showAllHandler_;
    std::function<void(const QStringList&)> ghostOthersHandler_;
    std::function<void()> clearGhostingHandler_;
    std::function<void(const QStringList&)> createGroupHandler_;
    std::function<void(const QString&, cad::application::VisibilityMode)> groupVisibilityHandler_;
    std::function<void(const QString&)> isolateGroupHandler_;
    std::function<void(const QString&)> removeGroupHandler_;
    std::function<void(const QString&, const QStringList&)> addToGroupHandler_;
    std::function<void(const QString&, const QStringList&)> removeFromGroupHandler_;
    std::function<void(cad::application::VisibilityCategory,
                       std::optional<cad::application::VisibilityMode>)> categoryFilterHandler_;
    std::function<void(const QString&,
                       std::optional<cad::application::VisibilityMode>)> typeFilterHandler_;
    std::function<void(cad::parametric::FeatureRole,
                       std::optional<cad::application::VisibilityMode>)> roleFilterHandler_;
    std::function<void()> clearFiltersHandler_;
    std::function<void(cad::application::VisibilityCategory)> showOnlyCategoryHandler_;
    std::function<void()> savePresetHandler_;
    std::function<void(const QString&)> applyPresetHandler_;
    std::function<void(const QString&)> updatePresetHandler_;
    std::function<void(const QString&)> renamePresetHandler_;
    std::function<void(const QString&)> deletePresetHandler_;
};
