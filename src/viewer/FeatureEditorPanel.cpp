#include "viewer/FeatureEditorPanel.h"

#include "model/ParametricFeature.h"
#include "operations/ParametricFeatures.h"

#include <QAbstractItemView>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QUuid>
#include <QSignalBlocker>
#include <QTreeWidgetItemIterator>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <memory>
#include <utility>
#include <vector>

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
    editor->setKeyboardTracking(false);
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

void FeatureEditorPanel::setFeatureSelectedHandler(
    std::function<void(const QStringList&)> handler
)
{
    featureSelectedHandler_ = std::move(handler);
}

void FeatureEditorPanel::createUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    auto* primitivesTitle =
        new QLabel("<b>Primitives</b>", this);
    rootLayout->addWidget(primitivesTitle);

    auto* primitiveLayout = new QGridLayout();

    auto* boxButton = new QPushButton("Box", this);
    auto* cylinderButton = new QPushButton("Cylinder", this);
    auto* coneButton = new QPushButton("Cone", this);
    auto* sphereButton = new QPushButton("Sphere", this);
    auto* torusButton = new QPushButton("Torus", this);
    auto* hexagonButton = new QPushButton("Hexagon", this);

    primitiveLayout->addWidget(boxButton, 0, 0);
    primitiveLayout->addWidget(cylinderButton, 0, 1);
    primitiveLayout->addWidget(coneButton, 1, 0);
    primitiveLayout->addWidget(sphereButton, 1, 1);
    primitiveLayout->addWidget(torusButton, 2, 0);
    primitiveLayout->addWidget(hexagonButton, 2, 1);

    rootLayout->addLayout(primitiveLayout);

    auto* booleanTitle =
        new QLabel("<b>Boolean</b>", this);
    rootLayout->addWidget(booleanTitle);

    auto* booleanLayout = new QGridLayout();

    auto* fuseButton = new QPushButton("Fuse", this);
    auto* cutButton = new QPushButton("Cut", this);
    auto* commonButton = new QPushButton("Common", this);

    fuseButton->setToolTip(
        "Select two features in the tree with Ctrl, then Fuse"
    );
    cutButton->setToolTip(
        "Select two features: first is base, second is cutting tool"
    );
    commonButton->setToolTip(
        "Select two features and keep their intersection"
    );

    booleanLayout->addWidget(fuseButton, 0, 0);
    booleanLayout->addWidget(cutButton, 0, 1);
    booleanLayout->addWidget(commonButton, 1, 0, 1, 2);

    rootLayout->addLayout(booleanLayout);

    messageLabel_ = new QLabel(this);
    messageLabel_->setWordWrap(true);
    messageLabel_->hide();
    rootLayout->addWidget(messageLabel_);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setSelectionMode(
        QAbstractItemView::ExtendedSelection
    );
    tree_->setMinimumWidth(260);

    rootLayout->addWidget(tree_, 2);

    auto* propertiesTitle =
        new QLabel("<b>Properties</b>", this);
    rootLayout->addWidget(propertiesTitle);

    propertiesWidget_ = new QWidget(this);
    propertiesLayout_ =
        new QFormLayout(propertiesWidget_);

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
        coneButton,
        &QPushButton::clicked,
        this,
        [this]() { addCone(); }
    );

    connect(
        sphereButton,
        &QPushButton::clicked,
        this,
        [this]() { addSphere(); }
    );

    connect(
        torusButton,
        &QPushButton::clicked,
        this,
        [this]() { addTorus(); }
    );

    connect(
        hexagonButton,
        &QPushButton::clicked,
        this,
        [this]() { addHexagon(); }
    );

    connect(
        fuseButton,
        &QPushButton::clicked,
        this,
        [this]() {
            addBoolean(
                cad::parametric::BooleanOperation::Fuse,
                "Fuse"
            );
        }
    );

    connect(
        cutButton,
        &QPushButton::clicked,
        this,
        [this]() {
            addBoolean(
                cad::parametric::BooleanOperation::Cut,
                "Cut"
            );
        }
    );

    connect(
        commonButton,
        &QPushButton::clicked,
        this,
        [this]() {
            addBoolean(
                cad::parametric::BooleanOperation::Common,
                "Common"
            );
        }
    );

    connect(
        tree_,
        &QTreeWidget::itemSelectionChanged,
        this,
        [this]() {
            updateSelectedProperties();
            if (featureSelectedHandler_) {
                featureSelectedHandler_(selectedFeatureIds());
            }
        }
    );

    connect(tree_, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem*, QTreeWidgetItem*) { updateSelectedProperties(); });

    clearProperties();
}

void FeatureEditorPanel::addBox()
{
    if (body_ == nullptr) {
        return;
    }

    const std::string id =
        "box-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();

    body_->addFeature(
        std::make_shared<
            cad::parametric::BoxParametricFeature
        >(
            id,
            100.0,
            70.0,
            30.0
        )
    );

    recomputeAndNotify("Box added");
}

void FeatureEditorPanel::addCylinder()
{
    if (body_ == nullptr) {
        return;
    }

    const std::string id =
        "cylinder-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();

    body_->addFeature(
        std::make_shared<
            cad::parametric::CylinderParametricFeature
        >(
            id,
            25.0,
            60.0
        )
    );

    recomputeAndNotify("Cylinder added");
}

void FeatureEditorPanel::addCone()
{
    if (body_ == nullptr) {
        return;
    }

    const std::string id =
        "cone-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();

    body_->addFeature(
        std::make_shared<cad::parametric::ConeFeature>(
            id,
            30.0,
            15.0,
            60.0
        )
    );

    recomputeAndNotify("Cone added");
}

void FeatureEditorPanel::addSphere()
{
    if (body_ == nullptr) {
        return;
    }

    const std::string id =
        "sphere-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();

    body_->addFeature(
        std::make_shared<cad::parametric::SphereFeature>(
            id,
            35.0
        )
    );

    recomputeAndNotify("Sphere added");
}

void FeatureEditorPanel::addTorus()
{
    if (body_ == nullptr) {
        return;
    }

    const std::string id =
        "torus-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();

    body_->addFeature(
        std::make_shared<cad::parametric::TorusFeature>(
            id,
            45.0,
            12.0
        )
    );

    recomputeAndNotify("Torus added");
}


void FeatureEditorPanel::addHexagon()
{
    if (body_ == nullptr) {
        return;
    }

    const std::string id =
        "hexagon-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();

    body_->addFeature(
        std::make_shared<cad::parametric::HexagonFeature>(
            id,
            30.0,
            12.0
        )
    );

    recomputeAndNotify("Hexagon added");
}

void FeatureEditorPanel::addBoolean(
    const cad::parametric::BooleanOperation operation,
    const QString& operationName
)
{
    if (body_ == nullptr) {
        return;
    }

    std::vector<FeaturePtr> features =
        selectedFeatures();

    if (features.size() != 2) {
        setPanelMessage(
            "Select exactly two features in the tree. "
            "Use Ctrl+Click for multi-selection.",
            true
        );
        return;
    }

    // For Cut, make the operation order deterministic:
    // select the base first, then Ctrl+Click the cutting tool.
    // QTreeWidget::currentItem() is the last/currently focused item,
    // so we treat it as the cutting tool and the other selected item
    // as the base.
    if (operation == cad::parametric::BooleanOperation::Cut) {
        QTreeWidgetItem* currentItem = tree_->currentItem();

        if (currentItem == nullptr) {
            setPanelMessage(
                "For Cut: select the base, then Ctrl+Click the cutting tool.",
                true
            );
            return;
        }

        const QString currentId =
            currentItem->data(0, FeatureIdRole).toString();

        FeaturePtr cuttingTool =
            body_->findFeature(currentId.toStdString());

        if (!cuttingTool) {
            setPanelMessage(
                "Could not determine the cutting tool.",
                true
            );
            return;
        }

        FeaturePtr baseFeature;

        for (const FeaturePtr& feature : features) {
            if (feature && feature->id() != cuttingTool->id()) {
                baseFeature = feature;
                break;
            }
        }

        if (!baseFeature) {
            setPanelMessage(
                "Could not determine the base feature.",
                true
            );
            return;
        }

        features[0] = baseFeature;
        features[1] = cuttingTool;

        // For the Hexagon/Cylinder workflow, always use the cylinder as the tool.
        if (std::dynamic_pointer_cast<cad::parametric::CylinderParametricFeature>(features[0])
            && std::dynamic_pointer_cast<cad::parametric::HexagonFeature>(features[1])) {
            std::swap(features[0], features[1]);
        }
    }

    const std::string id =
        operationName.toLower().toStdString()
        + "-"
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();

    auto booleanFeature =
        std::make_shared<cad::parametric::BooleanFeature>(
            id,
            features[0],
            features[1],
            operation
        );

    booleanFeature->setName(
        operationName.toStdString()
    );

    if (operation == cad::parametric::BooleanOperation::Cut) {
        int sequence = 1;
        QString name;
        bool exists;
        do {
            name = QString("Cut%1").arg(sequence++, 3, 10, QLatin1Char('0'));
            exists = false;
            for (const auto& feature : body_->features()) {
                if (feature->name() == name.toStdString()) exists = true;
            }
        } while (exists);
        booleanFeature->setName(name.toStdString());
    }

    if (!booleanFeature->recompute()) {
        setPanelMessage(QString::fromStdString(booleanFeature->error()), true);
        return;
    }

    body_->addFeature(booleanFeature);

    recomputeAndNotify(
        operationName + " added"
    );
    if (booleanFeature->state() == cad::parametric::FeatureState::UpToDate) {
        selectFeatures({QString::fromStdString(booleanFeature->id())});
        if (featureSelectedHandler_) featureSelectedHandler_(selectedFeatureIds());
    }
}

std::vector<FeatureEditorPanel::FeaturePtr>
FeatureEditorPanel::selectedFeatures() const
{
    std::vector<FeaturePtr> result;

    if (body_ == nullptr || tree_ == nullptr) {
        return result;
    }

    const QList<QTreeWidgetItem*> selectedItems =
        tree_->selectedItems();

    for (QTreeWidgetItem* item : selectedItems) {
        if (item == nullptr) {
            continue;
        }

        const QString id =
            item->data(0, FeatureIdRole).toString();

        if (id.isEmpty()) {
            continue;
        }

        FeaturePtr feature =
            body_->findFeature(id.toStdString());

        if (feature) {
            result.push_back(feature);
        }
    }

    return result;
}

void FeatureEditorPanel::refresh()
{
    const QSignalBlocker blocker(tree_);
    const auto selectedIds = selectedFeatureIds();
    QString currentId;

    if (tree_->currentItem() != nullptr) {
        currentId =
            tree_->currentItem()
                ->data(0, FeatureIdRole)
                .toString();
    }

    tree_->clear();

    auto* bodyItem =
        new QTreeWidgetItem(tree_, QStringList{"Body"});
    bodyItem->setExpanded(true);

    if (body_ == nullptr) {
        clearProperties();
        return;
    }

    QTreeWidgetItem* currentItemToRestore = nullptr;

    for (const FeaturePtr& feature : body_->features()) {
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
            new QTreeWidgetItem(
                bodyItem,
                QStringList{title}
            );

        const QString id =
            QString::fromStdString(feature->id());

        item->setData(
            0,
            FeatureIdRole,
            id
        );

        item->setSelected(selectedIds.contains(id));
        if (id == currentId) {
            currentItemToRestore = item;
        }
    }

    if (currentItemToRestore != nullptr) {
        tree_->setCurrentItem(currentItemToRestore, 0, QItemSelectionModel::NoUpdate);
        updateSelectedProperties();
    } else {
        clearProperties();
    }
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
    const FeaturePtr& feature
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

    if (feature->state()
        == cad::parametric::FeatureState::Failed) {

        auto* errorLabel =
            new QLabel(
                QString::fromStdString(feature->error()),
                propertiesWidget_
            );

        errorLabel->setWordWrap(true);
        errorLabel->setStyleSheet(
            "QLabel { color: #c0392b; }"
        );

        propertiesLayout_->addRow(
            "Error",
            errorLabel
        );
    }

    if (auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature)) {
        auto* width = makeLengthEditor(propertiesWidget_, sketch->width());
        auto* height = makeLengthEditor(propertiesWidget_, sketch->height());
        const auto apply = [this, sketch, width, height]() {
            if (sketch->width() == width->value() && sketch->height() == height->value()) return;
            sketch->setSize(width->value(), height->value());
            commitFeatureChange(sketch);
        };
        connect(width, &QDoubleSpinBox::editingFinished, this, apply);
        connect(height, &QDoubleSpinBox::editingFinished, this, apply);
        propertiesLayout_->addRow("Width", width);
        propertiesLayout_->addRow("Height", height);
        propertiesLayout_->addRow("Plane", new QLabel("XY", propertiesWidget_));
        return;
    }

    if (auto face = std::dynamic_pointer_cast<cad::parametric::FaceFeature>(feature)) {
        propertiesLayout_->addRow("Source Sketch",
            new QLabel(QString::fromStdString(face->sourceFeatureId()), propertiesWidget_));
        return;
    }

    if (auto box =
            std::dynamic_pointer_cast<
                cad::parametric::BoxParametricFeature
            >(feature)) {

        auto* width =
            makeLengthEditor(
                propertiesWidget_,
                box->width()
            );
        auto* depth =
            makeLengthEditor(
                propertiesWidget_,
                box->depth()
            );
        auto* height =
            makeLengthEditor(
                propertiesWidget_,
                box->height()
            );

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
            &QDoubleSpinBox::editingFinished,
            this,
            [apply]() { apply(); }
        );

        connect(
            depth,
            &QDoubleSpinBox::editingFinished,
            this,
            [apply]() { apply(); }
        );

        connect(
            height,
            &QDoubleSpinBox::editingFinished,
            this,
            [apply]() { apply(); }
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
            &QDoubleSpinBox::editingFinished,
            this,
            [this, cylinder, radius]() {
                cylinder->setRadius(radius->value());
                commitFeatureChange(cylinder);
            }
        );

        connect(
            height,
            &QDoubleSpinBox::editingFinished,
            this,
            [this, cylinder, height]() {
                cylinder->setHeight(height->value());
                commitFeatureChange(cylinder);
            }
        );

        propertiesLayout_->addRow("Radius", radius);
        propertiesLayout_->addRow("Height", height);
        return;
    }

    if (auto cone =
            std::dynamic_pointer_cast<
                cad::parametric::ConeFeature
            >(feature)) {

        auto* bottomRadius =
            makeLengthEditor(
                propertiesWidget_,
                cone->bottomRadius()
            );
        auto* topRadius =
            makeLengthEditor(
                propertiesWidget_,
                cone->topRadius()
            );
        auto* height =
            makeLengthEditor(
                propertiesWidget_,
                cone->height()
            );

        connect(
            bottomRadius,
            &QDoubleSpinBox::editingFinished,
            this,
            [this, cone, bottomRadius]() {
                cone->setBottomRadius(bottomRadius->value());
                commitFeatureChange(cone);
            }
        );

        connect(
            topRadius,
            &QDoubleSpinBox::editingFinished,
            this,
            [this, cone, topRadius]() {
                cone->setTopRadius(topRadius->value());
                commitFeatureChange(cone);
            }
        );

        connect(
            height,
            &QDoubleSpinBox::editingFinished,
            this,
            [this, cone, height]() {
                cone->setHeight(height->value());
                commitFeatureChange(cone);
            }
        );

        propertiesLayout_->addRow(
            "Bottom radius",
            bottomRadius
        );
        propertiesLayout_->addRow(
            "Top radius",
            topRadius
        );
        propertiesLayout_->addRow(
            "Height",
            height
        );
        return;
    }

    if (auto sphere =
            std::dynamic_pointer_cast<
                cad::parametric::SphereFeature
            >(feature)) {

        auto* radius =
            makeLengthEditor(
                propertiesWidget_,
                sphere->radius()
            );

        connect(
            radius,
            &QDoubleSpinBox::editingFinished,
            this,
            [this, sphere, radius]() {
                sphere->setRadius(radius->value());
                commitFeatureChange(sphere);
            }
        );

        propertiesLayout_->addRow("Radius", radius);
        return;
    }

    if (auto torus =
            std::dynamic_pointer_cast<
                cad::parametric::TorusFeature
            >(feature)) {

        auto* majorRadius =
            makeLengthEditor(
                propertiesWidget_,
                torus->majorRadius()
            );
        auto* minorRadius =
            makeLengthEditor(
                propertiesWidget_,
                torus->minorRadius()
            );

        connect(
            majorRadius,
            &QDoubleSpinBox::editingFinished,
            this,
            [this, torus, majorRadius]() {
                torus->setMajorRadius(majorRadius->value());
                commitFeatureChange(torus);
            }
        );

        connect(
            minorRadius,
            &QDoubleSpinBox::editingFinished,
            this,
            [this, torus, minorRadius]() {
                torus->setMinorRadius(minorRadius->value());
                commitFeatureChange(torus);
            }
        );

        propertiesLayout_->addRow(
            "Major radius",
            majorRadius
        );
        propertiesLayout_->addRow(
            "Minor radius",
            minorRadius
        );
        return;
    }

    if (auto hexagon =
            std::dynamic_pointer_cast<
                cad::parametric::HexagonFeature
            >(feature)) {

        auto* acrossFlats =
            makeLengthEditor(
                propertiesWidget_,
                hexagon->acrossFlats()
            );

        auto* height =
            makeLengthEditor(
                propertiesWidget_,
                hexagon->height()
            );

        connect(
            acrossFlats,
            &QDoubleSpinBox::editingFinished,
            this,
            [this, hexagon, acrossFlats]() {
                hexagon->setAcrossFlats(acrossFlats->value());
                commitFeatureChange(hexagon);
            }
        );

        connect(
            height,
            &QDoubleSpinBox::editingFinished,
            this,
            [this, hexagon, height]() {
                hexagon->setHeight(height->value());
                commitFeatureChange(hexagon);
            }
        );

        propertiesLayout_->addRow(
            "Across flats",
            acrossFlats
        );

        propertiesLayout_->addRow(
            "Height",
            height
        );

        return;
    }

    if (auto booleanFeature =
            std::dynamic_pointer_cast<
                cad::parametric::BooleanFeature
            >(feature)) {

        QString operation;

        switch (booleanFeature->operation()) {
            case cad::parametric::BooleanOperation::Fuse:
                operation = "Fuse";
                break;
            case cad::parametric::BooleanOperation::Cut:
                operation = "Cut";
                break;
            case cad::parametric::BooleanOperation::Common:
                operation = "Common";
                break;
        }

        propertiesLayout_->addRow(
            "Operation",
            new QLabel(operation, propertiesWidget_)
        );

        propertiesLayout_->addRow(
            "Left",
            new QLabel(
                QString::fromStdString(
                    booleanFeature->left()->name()
                ),
                propertiesWidget_
            )
        );

        propertiesLayout_->addRow(
            "Right",
            new QLabel(
                QString::fromStdString(
                    booleanFeature->right()->name()
                ),
                propertiesWidget_
            )
        );

        return;
    }

    auto* info =
        new QLabel(
            "This feature is part of the parametric history. "
            "A specialized editor will be added later.",
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
    const FeaturePtr& feature
)
{
    if (body_ == nullptr || !feature) {
        return;
    }

    body_->markDirtyFrom(feature->id());

    if (!body_->recompute()) {
        setPanelMessage(
            QString::fromStdString(
                body_->lastError()
            ),
            true
        );
    } else {
        setPanelMessage(
            "Feature updated",
            false
        );
    }

    if (modelChangedHandler_) {
        modelChangedHandler_();
    }

    // Rebuilding Properties synchronously from QDoubleSpinBox::valueChanged
    // can delete the spin box while it is still emitting the signal.
    // Queue the refresh until Qt returns to the event loop.
    QTimer::singleShot(
        0,
        this,
        [this]() {
            refresh();
        }
    );
}

void FeatureEditorPanel::recomputeAndNotify(
    const QString& successMessage
)
{
    if (body_ == nullptr) {
        return;
    }

    if (!body_->recompute()) {
        setPanelMessage(
            QString::fromStdString(
                body_->lastError()
            ),
            true
        );

        refresh();

        if (modelChangedHandler_) {
            modelChangedHandler_();
        }

        return;
    }

    setPanelMessage(
        successMessage,
        false
    );

    refresh();

    if (modelChangedHandler_) {
        modelChangedHandler_();
    }
}

void FeatureEditorPanel::setPanelMessage(
    const QString& message,
    const bool error
)
{
    if (messageLabel_ == nullptr) {
        return;
    }

    messageLabel_->setText(message);

    messageLabel_->setStyleSheet(
        error
            ? "QLabel { color: #c0392b; font-weight: 600; }"
            : "QLabel { color: #2e7d32; }"
    );

    messageLabel_->setVisible(!message.isEmpty());
}

QStringList FeatureEditorPanel::selectedFeatureIds() const
{
    QStringList ids;
    for (auto* item : tree_->selectedItems()) {
        const auto id = item->data(0, FeatureIdRole).toString();
        if (!id.isEmpty()) ids.append(id);
    }
    return ids;
}

void FeatureEditorPanel::updateSelectedProperties()
{
    auto* item = tree_->currentItem();
    if (!item || !item->isSelected()) {
        const auto items = tree_->selectedItems();
        item = items.isEmpty() ? nullptr : items.first();
    }
    showFeature(item ? item->data(0, FeatureIdRole).toString().toStdString() : std::string{});
}

void FeatureEditorPanel::selectFeatures(const QStringList& featureIds)
{
    // Viewer-to-tree updates must not reselect whole objects in the viewer:
    // doing so would discard a picked face/edge and recurse through the signals.
    const QSignalBlocker blocker(tree_);
    auto* current = tree_->currentItem();
    QTreeWidgetItem* first = nullptr;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        auto* item = *it;
        const auto id = item->data(0, FeatureIdRole).toString();
        const bool selected = !id.isEmpty() && featureIds.contains(id);
        item->setSelected(selected);
        if (selected && !first) first = item;
    }
    if (!current || !current->isSelected()) current = first;
    tree_->setCurrentItem(current, 0, QItemSelectionModel::NoUpdate);
    updateSelectedProperties();
}
