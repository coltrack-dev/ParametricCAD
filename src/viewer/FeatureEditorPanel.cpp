#include "viewer/FeatureEditorPanel.h"

#include "application/FeatureEditingService.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QLoggingCategory>
#include <QScopedValueRollback>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QSignalBlocker>
#include <QTreeWidgetItemIterator>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

Q_LOGGING_CATEGORY(pcadEditorLog, "parametric.editor")

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

void FeatureEditorPanel::setService(cad::application::FeatureEditingService* service)
{
    service_ = service;
    refresh();
}

void FeatureEditorPanel::setFeatures(
    std::vector<cad::application::FeatureDescriptor> features
)
{
    features_ = std::move(features);
    refresh();
}

void FeatureEditorPanel::setActionState(
    const cad::application::ModelingActionState& state
)
{
    canDelete_ = state.canDelete;
    canCreateFace_ = state.canCreateFace;
    canExtrude_ = state.canExtrude;
    canBoolean_ = state.canBoolean;
    canFillet_ = state.canFillet;
    canChamfer_ = state.canChamfer;
    canSketchOnFace_ = state.canSketchOnFace;
    refresh();
}

void FeatureEditorPanel::commitPendingEdits()
{
    if (refreshPending_) return;
    auto* focus = QApplication::focusWidget();
    if (!focus || !propertiesWidget_->isAncestorOf(focus)) return;
    auto* editor = qobject_cast<QDoubleSpinBox*>(focus);
    if (!editor) editor = qobject_cast<QDoubleSpinBox*>(focus->parentWidget());
    if (!editor) return;
    editor->interpretText();
    committingPendingEdit_ = true;
    QMetaObject::invokeMethod(editor, "editingFinished", Qt::DirectConnection);
    committingPendingEdit_ = false;
}

bool FeatureEditorPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (service_ && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const bool undo = key->matches(QKeySequence::Undo)
            || (key->modifiers() == Qt::ControlModifier && key->key() == Qt::Key_Z);
        const bool redo = key->matches(QKeySequence::Redo)
            || (key->modifiers() == Qt::ControlModifier && key->key() == Qt::Key_Y);
        if (undo || redo) {
            event->accept();
            if (event->type() == QEvent::KeyPress) {
                commitPendingEdits();
                if (undo) service_->undo();
                else service_->redo();
                scheduleRefresh();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FeatureEditorPanel::reportResult(const cad::application::ModelingResult& result)
{
    qCDebug(pcadEditorLog) << "reportResult" << "success" << result.success
                           << "id" << QString::fromStdString(result.id)
                           << "error" << QString::fromStdString(result.error);
    if (!result.success) {
        setPanelMessage(QString::fromStdString(result.error), true);
        return;
    }
    setPanelMessage({}, false);
    // Property editors call reportResult from their own Qt signal handlers.
    // Rebuilding the form synchronously would delete the emitting widget and
    // its callback while Qt is still dispatching the signal. Defer the model
    // and panel refresh until the current event has returned.
    const QString resultId = QString::fromStdString(result.id);
    QTimer::singleShot(0, this, [this, resultId]() {
        qCDebug(pcadEditorLog) << "deferred refresh begin" << resultId
                               << "panel" << static_cast<const void*>(this);
        if (modelChangedHandler_) {
            modelChangedHandler_();
        } else {
            refresh();
        }
        if (!resultId.isEmpty()) selectFeatures({resultId});
        qCDebug(pcadEditorLog) << "deferred refresh complete" << resultId;
    });
}

bool FeatureEditorPanel::propertyMatchesCurrentValue(
    const std::string& featureId,
    const std::string& propertyKey,
    const cad::parametric::PropertyValue& value
) const
{
    const auto feature = std::find_if(features_.begin(), features_.end(),
        [&featureId](const auto& candidate) { return candidate.id == featureId; });
    if (feature == features_.end()) return false;

    const auto property = std::find_if(feature->properties.begin(), feature->properties.end(),
        [&propertyKey](const auto& candidate) { return candidate.key == propertyKey; });
    return property != feature->properties.end() && property->value == value;
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

void FeatureEditorPanel::setFeatureDoubleClickedHandler(
    std::function<void(const QString&)> handler)
{
    featureDoubleClickedHandler_ = std::move(handler);
}

void FeatureEditorPanel::createUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);
    setSizePolicy(
        QSizePolicy::Fixed,
        QSizePolicy::Expanding
    );

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
    tree_->setMinimumWidth(0);
    tree_->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Expanding
    );

    rootLayout->addWidget(tree_, 2);

    auto* propertiesTitle =
        new QLabel("<b>Properties</b>", this);
    rootLayout->addWidget(propertiesTitle);

    propertiesWidget_ = new QWidget(this);
    propertiesWidget_->setMinimumWidth(0);
    propertiesWidget_->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Expanding
    );
    propertiesLayout_ =
        new QFormLayout(propertiesWidget_);
    propertiesLayout_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    rootLayout->addWidget(propertiesWidget_, 1);

    auto* constraintsTitle = new QLabel("<b>Sketch Constraints</b>", this);
    rootLayout->addWidget(constraintsTitle);
    constraintList_ = new QListWidget(this);
    constraintList_->setSelectionMode(QAbstractItemView::SingleSelection);
    rootLayout->addWidget(constraintList_, 1);
    auto* constraintButtons = new QHBoxLayout();
    editConstraintButton_ = new QPushButton("Edit", this);
    deleteConstraintButton_ = new QPushButton("Delete", this);
    editConstraintButton_->setEnabled(false);
    deleteConstraintButton_->setEnabled(false);
    constraintButtons->addWidget(editConstraintButton_);
    constraintButtons->addWidget(deleteConstraintButton_);
    rootLayout->addLayout(constraintButtons);
    connect(constraintList_, &QListWidget::itemClicked, this,
        [this](QListWidgetItem* item) {
            if (item) {
                editConstraintButton_->setEnabled(item->data(Qt::UserRole + 1).toBool());
                deleteConstraintButton_->setEnabled(true);
            }
            if (item && sketchConstraintSelectionHandler_)
                sketchConstraintSelectionHandler_(item->data(Qt::UserRole).toString());
        });
    connect(constraintList_, &QListWidget::itemDoubleClicked, this,
        [this](QListWidgetItem* item) {
            if (item && sketchConstraintEditHandler_)
                sketchConstraintEditHandler_(item->data(Qt::UserRole).toString());
        });
    connect(editConstraintButton_, &QPushButton::clicked, this, [this]() {
        if (constraintList_->currentItem() && sketchConstraintEditHandler_)
            sketchConstraintEditHandler_(constraintList_->currentItem()->data(Qt::UserRole).toString());
    });
    connect(deleteConstraintButton_, &QPushButton::clicked, this, [this]() {
        if (constraintList_->currentItem() && sketchConstraintDeleteHandler_)
            sketchConstraintDeleteHandler_(constraintList_->currentItem()->data(Qt::UserRole).toString());
    });

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
                cad::application::BooleanKind::Fuse,
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
                cad::application::BooleanKind::Cut,
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
                cad::application::BooleanKind::Common,
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
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item, int) {
            if (featureDoubleClickedHandler_ && item) {
                const auto id = item->data(0, FeatureIdRole).toString();
                if (!id.isEmpty()) featureDoubleClickedHandler_(id);
            }
        });

    clearProperties();
}

void FeatureEditorPanel::setSketchConstraints(
    std::vector<SketchConstraintListItem> items, const bool editable)
{
    constraintList_->clear();
    for (const auto& item : items) {
        auto* row = new QListWidgetItem(item.label, constraintList_);
        row->setData(Qt::UserRole, QString::fromStdString(item.id));
        row->setData(Qt::UserRole + 1, item.editable);
    }
    editConstraintButton_->setEnabled(editable && constraintList_->currentItem()
        && constraintList_->currentItem()->data(Qt::UserRole + 1).toBool());
    deleteConstraintButton_->setEnabled(editable && constraintList_->currentItem());
}

void FeatureEditorPanel::clearSketchConstraintSelection()
{
    constraintList_->clearSelection();
    editConstraintButton_->setEnabled(false);
    deleteConstraintButton_->setEnabled(false);
}

void FeatureEditorPanel::setSketchConstraintSelected(const QString& id)
{
    for (int i = 0; i < constraintList_->count(); ++i) {
        auto* item = constraintList_->item(i);
        if (item->data(Qt::UserRole).toString() == id) {
            constraintList_->setCurrentItem(item);
            item->setSelected(true);
            editConstraintButton_->setEnabled(item->data(Qt::UserRole + 1).toBool());
            deleteConstraintButton_->setEnabled(true);
            return;
        }
    }
    clearSketchConstraintSelection();
}

void FeatureEditorPanel::setSketchConstraintSelectionHandler(
    std::function<void(const QString&)> handler)
{ sketchConstraintSelectionHandler_ = std::move(handler); }

void FeatureEditorPanel::setSketchConstraintEditHandler(
    std::function<void(const QString&)> handler)
{ sketchConstraintEditHandler_ = std::move(handler); }

void FeatureEditorPanel::setSketchConstraintDeleteHandler(
    std::function<void(const QString&)> handler)
{ sketchConstraintDeleteHandler_ = std::move(handler); }

void FeatureEditorPanel::addBox()
{
    reportResult(service_->createPrimitive(cad::application::PrimitiveKind::Box));
}

void FeatureEditorPanel::addCylinder()
{
    reportResult(service_->createPrimitive(cad::application::PrimitiveKind::Cylinder));
}

void FeatureEditorPanel::addCone()
{
    reportResult(service_->createPrimitive(cad::application::PrimitiveKind::Cone));
}

void FeatureEditorPanel::addSphere()
{
    reportResult(service_->createPrimitive(cad::application::PrimitiveKind::Sphere));
}

void FeatureEditorPanel::addTorus()
{
    reportResult(service_->createPrimitive(cad::application::PrimitiveKind::Torus));
}


void FeatureEditorPanel::addHexagon()
{
    reportResult(service_->createPrimitive(cad::application::PrimitiveKind::Hexagon));
}

void FeatureEditorPanel::addBoolean(
    const cad::application::BooleanKind operation,
    const QString& operationName
)
{
    Q_UNUSED(operationName);
    const auto ids = selectedFeatureIds();
    const auto current = tree_->currentItem();
    reportResult(service_->createBoolean(
        operation,
        [&ids]() {
            std::vector<std::string> result;
            for (const auto& id : ids) result.push_back(id.toStdString());
            return result;
        }(),
        current ? current->data(0, FeatureIdRole).toString().toStdString() : std::string{}));
    return;
}

void FeatureEditorPanel::refresh()
{
    const QScopedValueRollback guard(updatingProperties_, true);
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

    QTreeWidgetItem* currentItemToRestore = nullptr;

    for (const auto& feature : features_) {
        QString title = QString::fromStdString(feature.name);

        if (feature.state == cad::parametric::FeatureState::Dirty) {
            title += " *";
        } else if (feature.state == cad::parametric::FeatureState::Failed) {
            title += " [FAILED]";
        }

        auto* item =
            new QTreeWidgetItem(
                bodyItem,
                QStringList{title}
            );

        const QString id = QString::fromStdString(feature.id);

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
    const auto feature = std::find_if(features_.begin(), features_.end(),
        [&featureId](const auto& candidate) { return candidate.id == featureId; });
    rebuildProperties(feature == features_.end() ? nullptr : &*feature);
}

void FeatureEditorPanel::rebuildProperties(
    const cad::application::FeatureDescriptor* feature
)
{
    const QScopedValueRollback guard(updatingProperties_, true);
    clearProperties();

    if (!feature) {
        return;
    }

    const auto makePropertyLabel = [this](const QString& text) {
        auto* label = new QLabel(text, propertiesWidget_);
        label->setWordWrap(true);
        label->setMinimumWidth(0);
        label->setSizePolicy(
            QSizePolicy::Expanding,
            QSizePolicy::Preferred
        );
        return label;
    };

    propertiesLayout_->addRow(
        "Name",
        makePropertyLabel(QString::fromStdString(feature->name))
    );

    propertiesLayout_->addRow(
        "Id",
        makePropertyLabel(QString::fromStdString(feature->id))
    );

    if (feature->state == cad::parametric::FeatureState::Failed) {

        auto* errorLabel = makePropertyLabel(QString::fromStdString(feature->error));
        errorLabel->setStyleSheet(
            "QLabel { color: #c0392b; }"
        );

        propertiesLayout_->addRow(
            "Error",
            errorLabel
        );
    }

    const auto addParameter = [this, feature](const cad::parametric::FeatureProperty& property) {
        const double value = std::get<double>(property.value);
        const std::string featureId = feature->id;
        QDoubleSpinBox* editor = makeLengthEditor(propertiesWidget_, value);
        if (property.minimum) editor->setMinimum(*property.minimum);
        if (property.maximum) editor->setMaximum(*property.maximum);
        editor->installEventFilter(this);
        if (auto* lineEdit = editor->findChild<QLineEdit*>()) lineEdit->installEventFilter(this);
        const QString label = QString::fromStdString(property.label);
        propertiesLayout_->addRow(label, editor);
        connect(editor, &QDoubleSpinBox::editingFinished, this,
            [this, featureId, property, editor]() {
                if (updatingProperties_ || refreshPending_ || !service_) return;
                const double after = editor->value();
                qCDebug(pcadEditorLog) << "queue property edit"
                                       << QString::fromStdString(featureId)
                                       << QString::fromStdString(property.key)
                                       << after;
                const auto apply = [this, featureId, property, after]() {
                    if (!service_) return;
                    if (propertyMatchesCurrentValue(featureId, property.key, after)) return;
                    reportResult(service_->setFeatureProperty(
                        featureId, property.key, after));
                };
                if (committingPendingEdit_) apply();
                else QTimer::singleShot(0, this, apply);
            });
    };

    const auto& properties = feature->properties;
    for (const auto& property : properties) {
        if (property.editable && std::holds_alternative<double>(property.value)) {
            addParameter(property);
        } else if (property.editable && std::holds_alternative<int>(property.value)) {
            auto* editor = new QSpinBox(propertiesWidget_);
            editor->setRange(
                property.minimum ? static_cast<int>(*property.minimum) : -1'000'000,
                property.maximum ? static_cast<int>(*property.maximum) : 1'000'000
            );
            editor->setValue(std::get<int>(property.value));
            editor->setKeyboardTracking(false);
            editor->installEventFilter(this);
            const QString label = QString::fromStdString(property.label);
            const std::string featureId = feature->id;
            propertiesLayout_->addRow(label, editor);
            connect(editor, &QSpinBox::editingFinished, this,
                [this, featureId, property, editor]() {
                    if (updatingProperties_ || refreshPending_ || !service_) return;
                    const int after = editor->value();
                    qCDebug(pcadEditorLog) << "queue property edit"
                                           << QString::fromStdString(featureId)
                                           << QString::fromStdString(property.key)
                                           << after;
                    const auto apply = [this, featureId, property, after]() {
                        if (!service_) return;
                        if (propertyMatchesCurrentValue(featureId, property.key, after)) return;
                        reportResult(service_->setFeatureProperty(
                            featureId, property.key, after));
                    };
                    if (committingPendingEdit_) apply();
                    else QTimer::singleShot(0, this, apply);
                });
        } else if (property.editable && std::holds_alternative<bool>(property.value)) {
            auto* editor = new QCheckBox(propertiesWidget_);
            editor->setChecked(std::get<bool>(property.value));
            const QString label = QString::fromStdString(property.label);
            const std::string featureId = feature->id;
            propertiesLayout_->addRow(label, editor);
            connect(editor, &QCheckBox::toggled, this,
                [this, featureId, property](const bool checked) {
                    if (updatingProperties_ || refreshPending_ || !service_) return;
                    QTimer::singleShot(0, this, [this, featureId, property, checked]() {
                        if (!service_) return;
                        if (propertyMatchesCurrentValue(featureId, property.key, checked)) return;
                        reportResult(service_->setFeatureProperty(
                            featureId, property.key, checked));
                    });
                });
        } else if (property.editable && std::holds_alternative<std::string>(property.value)
                   && (property.key == "distribution" || property.key == "orientation")) {
            auto* editor = new QComboBox(propertiesWidget_);
            if (property.key == "distribution") {
                editor->addItems({QStringLiteral("FixedSpacing"), QStringLiteral("FitCount")});
            } else {
                editor->addItems({QStringLiteral("Fixed"), QStringLiteral("Tangent")});
            }
            editor->setCurrentText(QString::fromStdString(std::get<std::string>(property.value)));
            const QString label = QString::fromStdString(property.label);
            const std::string featureId = feature->id;
            propertiesLayout_->addRow(label, editor);
            connect(editor, &QComboBox::currentTextChanged, this,
                [this, featureId, property](const QString& value) {
                    if (updatingProperties_ || refreshPending_ || !service_) return;
                    const std::string after = value.toStdString();
                    QTimer::singleShot(0, this, [this, featureId, property, after]() {
                        if (!service_) return;
                        if (propertyMatchesCurrentValue(featureId, property.key, after)) return;
                        reportResult(service_->setFeatureProperty(
                            featureId, property.key, after));
                    });
                });
        } else {
            const auto value = std::visit([](const auto& item) {
                using Value = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<Value, bool>) return QString(item ? "true" : "false");
                else if constexpr (std::is_same_v<Value, int>) return QString::number(item);
                else if constexpr (std::is_same_v<Value, double>) return QString::number(item);
                else return QString::fromStdString(item);
            }, property.value);
            propertiesLayout_->addRow(QString::fromStdString(property.label),
                makePropertyLabel(value));
        }
    }

    if (!properties.empty()) return;

    auto* info = makePropertyLabel(
        "This feature is part of the parametric history. "
        "A specialized editor will be added later."
    );
    propertiesLayout_->addRow(info);
}

void FeatureEditorPanel::clearProperties()
{
    while (propertiesLayout_->rowCount() > 0) {
        propertiesLayout_->removeRow(0);
    }

    auto* label = new QLabel(
        "Select a feature in the tree.",
        propertiesWidget_
    );
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    propertiesLayout_->addRow(label);
}

void FeatureEditorPanel::scheduleRefresh()
{
    if (refreshPending_) return;
    refreshPending_ = true;
    // Rebuilding Properties synchronously from QDoubleSpinBox::valueChanged
    // can delete the spin box while it is still emitting the signal.
    // Queue the refresh until Qt returns to the event loop.
    QTimer::singleShot(
        0,
        this,
        [this]() {
            refreshPending_ = false;
            refresh();
        }
    );
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
    if (selectedFeatureIds() == featureIds) {
        return;
    }

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
