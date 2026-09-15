#pragma once

#include "model/Body.h"

#include <QWidget>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class QFormLayout;
class QLabel;
class QTreeWidget;

namespace cad::parametric {
class ParametricFeature;
enum class BooleanOperation;
}

class FeatureEditorPanel final : public QWidget
{
public:
    explicit FeatureEditorPanel(QWidget* parent = nullptr);

    void setBody(cad::parametric::Body* body);
    void setModelChangedHandler(std::function<void()> handler);
    void refresh();

private:
    using FeaturePtr =
        std::shared_ptr<cad::parametric::ParametricFeature>;

    void createUi();

    void addBox();
    void addCylinder();
    void addCone();
    void addSphere();
    void addTorus();
    void addHexagon();

    void addBoolean(
        cad::parametric::BooleanOperation operation,
        const QString& operationName
    );

    void showFeature(const std::string& featureId);

    std::vector<FeaturePtr> selectedFeatures() const;

    void rebuildProperties(const FeaturePtr& feature);
    void clearProperties();

    void commitFeatureChange(const FeaturePtr& feature);

    void recomputeAndNotify(
        const QString& successMessage
    );

    void setPanelMessage(
        const QString& message,
        bool error = false
    );

    cad::parametric::Body* body_{nullptr};

    QTreeWidget* tree_{nullptr};
    QWidget* propertiesWidget_{nullptr};
    QFormLayout* propertiesLayout_{nullptr};
    QLabel* messageLabel_{nullptr};

    std::function<void()> modelChangedHandler_;
    int nextFeatureNumber_{1};
};
