#pragma once

#include <QWidget>
#include <QStringList>
#include "application/FeatureEditingService.h"

#include <functional>
#include <string>
#include <vector>

class QFormLayout;
class QLabel;
class QTreeWidget;

class FeatureEditorPanel final : public QWidget
{
public:
    explicit FeatureEditorPanel(QWidget* parent = nullptr);

    void setService(cad::application::FeatureEditingService* service);
    void setFeatures(std::vector<cad::application::FeatureDescriptor> features);
    void setActionState(const cad::application::ModelingActionState& state);
    void scheduleRefresh();
    void commitPendingEdits();
    void setModelChangedHandler(std::function<void()> handler);
    void setFeatureSelectedHandler(
        std::function<void(const QStringList&)> handler
    );
    void setFeatureDoubleClickedHandler(std::function<void(const QString&)> handler);
    void refresh();
    QStringList selectedFeatureIds() const;
    void selectFeatures(const QStringList& featureIds);

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

    std::function<void()> modelChangedHandler_;
    std::function<void(const QStringList&)> featureSelectedHandler_;
    std::function<void(const QString&)> featureDoubleClickedHandler_;
};
