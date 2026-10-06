#pragma once

#include <QWidget>

class QTreeWidget;

namespace cad::parametric { class ImportedFeature; }

class BimInspectorPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit BimInspectorPanel(QWidget* parent = nullptr);
    void setFeature(const cad::parametric::ImportedFeature* feature);
    void clear();

private:
    QTreeWidget* tree_{nullptr};
};
