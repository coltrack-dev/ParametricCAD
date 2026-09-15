#pragma once

#include "model/Body.h"

#include <QWidget>

#include <functional>
#include <memory>
#include <string>

class QFormLayout;
class QTreeWidget;

namespace cad::parametric {
class ParametricFeature;
}

class FeatureEditorPanel final : public QWidget
{
public:
    explicit FeatureEditorPanel(QWidget* parent = nullptr);

    void setBody(cad::parametric::Body* body);
    void setModelChangedHandler(std::function<void()> handler);
    void refresh();

private:
    void createUi();
    void addBox();
    void addCylinder();
    void showFeature(const std::string& featureId);

    void rebuildProperties(
        const std::shared_ptr<cad::parametric::ParametricFeature>& feature
    );

    void clearProperties();

    void commitFeatureChange(
        const std::shared_ptr<cad::parametric::ParametricFeature>& feature
    );

    cad::parametric::Body* body_{nullptr};
    QTreeWidget* tree_{nullptr};
    QWidget* propertiesWidget_{nullptr};
    QFormLayout* propertiesLayout_{nullptr};

    std::function<void()> modelChangedHandler_;
    int nextFeatureNumber_{1};
};
