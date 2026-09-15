#include "viewer/FeatureEditorPanel.h"

#include "model/ParametricFeature.h"
#include "operations/ParametricFeatures.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <memory>
#include <utility>

namespace {

constexpr int FeatureIdRole = Qt::UserRole + 1;

QDoubleSpinBox* makeLengthEditor(
    QWidget* parent,
    const double value
)
{
    auto* editor = new QDoubleSpinBox(parent);
    editor->setDecimals(3);
    editor->setRange(0.001, 1'000'000.0);
    editor->setSingleStep(1.0);
    editor->setSuffix(" mm");
    editor->setValue(value);
    return editor;
}

} // namespace

FeatureEditorPanel::FeatureEditorPanel(QWidget* parent)
    : QWidget(parent)
{
    createUi();
}

void FeatureEditorPanel::setBody(cad::parametric::Body* body)
{
    body_ = body;
    refresh();
}

void FeatureEditorPanel::setModelChangedHandler(
    std::function<void()> handler
)
{
    modelChangedHandler_ = std::move(handler);
}

void FeatureEditorPanel::createUi()
{
    auto* rootLayout = new QVBoxLayout(this);

    auto* buttonLayout = new QHBoxLayout();

    auto* boxButton = new QPushButton("Box", this);
    auto* cylinderButton = new QPushButton("Cylinder", this);

    buttonLayout->addWidget(boxButton);
    buttonLayout->addWidget(cylinderButton);

    rootLayout->addLayout(buttonLayout);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    rootLayout->addWidget(tree_, 2);

    auto* title = new QLabel("<b>Properties</b>", this);
    rootLayout->addWidget(title);

    propertiesWidget_ = new QWidget(this);
    propertiesLayout_ = new QFormLayout(propertiesWidget_);
    rootLayout->addWidget(propertiesWidget_, 1);

    connect(
        boxButton,
        &QPushButton::clicked,
        this,
        [this]() { addBox(); }
    );

    connect(
        cylinderButton,
        &QPushButton::clicked,
        this,
        [this]() { addCylinder(); }
    );

    connect(
        tree_,
        &QTreeWidget::currentItemChanged,
        this,
        [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (current == nullptr) {
                clearProperties();
                return;
            }

            const QString id =
                current->data(0, FeatureIdRole).toString();

            if (id.isEmpty()) {
                clearProperties();
                return;
            }

            showFeature(id.toStdString());
        }
    );

    clearProperties();
}

void FeatureEditorPanel::addBox()
{
    if (body_ == nullptr) {
        return;
    }

    const std::string id =
        "box-" + std::to_string(nextFeatureNumber_++);

    auto feature =
        std::make_shared<cad::parametric::BoxParametricFeature>(
            id,
            100.0,
            70.0,
            30.0
        );

    body_->addFeature(feature);
    body_->recompute();
    refresh();

    if (modelChangedHandler_) {
        modelChangedHandler_();
    }
}

void FeatureEditorPanel::addCylinder()
{
    if (body_ == nullptr) {
        return;
    }

    const std::string id =
        "cylinder-" + std::to_string(nextFeatureNumber_++);

    auto feature =
        std::make_shared<cad::parametric::CylinderParametricFeature>(
            id,
            25.0,
            60.0
        );

    body_->addFeature(feature);
    body_->recompute();
    refresh();

    if (modelChangedHandler_) {
        modelChangedHandler_();
    }
}

void FeatureEditorPanel::refresh()
{
    tree_->clear();

    auto* bodyItem =
        new QTreeWidgetItem(tree_, QStringList{"Body"});
    bodyItem->setExpanded(true);

    if (body_ == nullptr) {
        clearProperties();
        return;
    }

    for (const auto& feature : body_->features()) {
        if (!feature) {
            continue;
        }

        QString title =
            QString::fromStdString(feature->name());

        if (feature->state()
            == cad::parametric::FeatureState::Dirty) {
            title += " *";
        } else if (
            feature->state()
            == cad::parametric::FeatureState::Failed) {
            title += " [FAILED]";
        }

        auto* item =
            new QTreeWidgetItem(bodyItem, QStringList{title});

        item->setData(
            0,
            FeatureIdRole,
            QString::fromStdString(feature->id())
        );
    }

    clearProperties();
}

void FeatureEditorPanel::showFeature(
    const std::string& featureId
)
{
    if (body_ == nullptr) {
        clearProperties();
        return;
    }

    rebuildProperties(
        body_->findFeature(featureId)
    );
}

void FeatureEditorPanel::rebuildProperties(
    const std::shared_ptr<cad::parametric::ParametricFeature>& feature
)
{
    clearProperties();

    if (!feature) {
        return;
    }

    propertiesLayout_->addRow(
        "Name",
        new QLabel(
            QString::fromStdString(feature->name()),
            propertiesWidget_
        )
    );

    propertiesLayout_->addRow(
        "Id",
        new QLabel(
            QString::fromStdString(feature->id()),
            propertiesWidget_
        )
    );

    if (auto box =
            std::dynamic_pointer_cast<
                cad::parametric::BoxParametricFeature
            >(feature)) {

        auto* width =
            makeLengthEditor(propertiesWidget_, box->width());
        auto* depth =
            makeLengthEditor(propertiesWidget_, box->depth());
        auto* height =
            makeLengthEditor(propertiesWidget_, box->height());

        const auto apply =
            [this, box, width, depth, height]() {
                box->setSize(
                    width->value(),
                    depth->value(),
                    height->value()
                );
                commitFeatureChange(box);
            };

        connect(
            width,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [apply](double) { apply(); }
        );

        connect(
            depth,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [apply](double) { apply(); }
        );

        connect(
            height,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [apply](double) { apply(); }
        );

        propertiesLayout_->addRow("Width", width);
        propertiesLayout_->addRow("Depth", depth);
        propertiesLayout_->addRow("Height", height);
        return;
    }

    if (auto cylinder =
            std::dynamic_pointer_cast<
                cad::parametric::CylinderParametricFeature
            >(feature)) {

        auto* radius =
            makeLengthEditor(
                propertiesWidget_,
                cylinder->radius()
            );

        auto* height =
            makeLengthEditor(
                propertiesWidget_,
                cylinder->height()
            );

        connect(
            radius,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this, cylinder](const double value) {
                cylinder->setRadius(value);
                commitFeatureChange(cylinder);
            }
        );

        connect(
            height,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this, cylinder](const double value) {
                cylinder->setHeight(value);
                commitFeatureChange(cylinder);
            }
        );

        propertiesLayout_->addRow("Radius", radius);
        propertiesLayout_->addRow("Height", height);
        return;
    }

    auto* info =
        new QLabel(
            "No dedicated editor for this feature yet.",
            propertiesWidget_
        );
    info->setWordWrap(true);
    propertiesLayout_->addRow(info);
}

void FeatureEditorPanel::clearProperties()
{
    while (propertiesLayout_->rowCount() > 0) {
        propertiesLayout_->removeRow(0);
    }

    auto* label =
        new QLabel(
            "Select a feature in the tree.",
            propertiesWidget_
        );

    label->setWordWrap(true);
    propertiesLayout_->addRow(label);
}

void FeatureEditorPanel::commitFeatureChange(
    const std::shared_ptr<cad::parametric::ParametricFeature>& feature
)
{
    if (body_ == nullptr || !feature) {
        return;
    }

    body_->markDirtyFrom(feature->id());
    body_->recompute();

    if (modelChangedHandler_) {
        modelChangedHandler_();
    }

    refresh();
}
