#include "viewer/FeatureEditorPanel.h"

#include "application/FeatureEditingService.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QBrush>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QScopedValueRollback>
#include <QDoubleSpinBox>
#include <QDebug>
#include <QElapsedTimer>
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
#include <array>
#include <unordered_map>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr int FeatureIdRole = Qt::UserRole + 1;
constexpr int GroupIdRole = Qt::UserRole + 2;
constexpr int FilterKindRole = Qt::UserRole + 3;
constexpr int FilterKeyRole = Qt::UserRole + 4;
constexpr int FilterRootRole = Qt::UserRole + 5;
constexpr int PresetIdRole = Qt::UserRole + 6;
constexpr int PresetRootRole = Qt::UserRole + 7;

QString groupModeText(const cad::application::VisibilityMode mode)
{
    switch (mode) {
    case cad::application::VisibilityMode::Visible: return "Visible";
    case cad::application::VisibilityMode::Ghosted: return "Ghosted";
    case cad::application::VisibilityMode::Hidden: return "Hidden";
    }
    return {};
}

QString filterModeText(const std::optional<cad::application::VisibilityMode>& mode)
{
    return mode ? groupModeText(*mode) : QStringLiteral("Visible");
}

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

void FeatureEditorPanel::setOperationSession(
    cad::application::InteractiveOperationSession* session)
{
    operationSession_ = session;
}

void FeatureEditorPanel::setFeatures(
    std::vector<cad::application::FeatureDescriptor> features
)
{
    features_ = std::move(features);
    refresh();
}

void FeatureEditorPanel::setVisibilityGroups(
    std::vector<cad::application::VisibilityGroup> groups)
{
    groups_ = std::move(groups);
    refresh();
}

void FeatureEditorPanel::setVisibilityFilters(
    cad::application::VisibilityFilterState filters)
{
    filters_ = std::move(filters);
    refresh();
}

void FeatureEditorPanel::setVisibilityPresets(
    std::vector<cad::application::VisibilityPreset> presets)
{
    presets_ = std::move(presets);
    refresh();
}

void FeatureEditorPanel::updateVisibilityPresentation(
    std::vector<cad::application::FeatureDescriptor> features,
    std::vector<cad::application::VisibilityGroup> groups,
    cad::application::VisibilityFilterState filters,
    std::vector<cad::application::VisibilityPreset> presets)
{
    const auto ids = [](const auto& values, const auto& idOf) {
        std::vector<std::string> result;
        result.reserve(values.size());
        for (const auto& value : values) result.push_back(idOf(value));
        std::sort(result.begin(), result.end());
        return result;
    };
    const bool structureChanged =
        ids(features_, [](const auto& value) { return value.id; })
            != ids(features, [](const auto& value) { return value.id; })
        || ids(groups_, [](const auto& value) { return value.id; })
            != ids(groups, [](const auto& value) { return value.id; })
        || ids(presets_, [](const auto& value) { return value.id; })
            != ids(presets, [](const auto& value) { return value.id; })
        || (!groups_.empty() != !groups.empty());

    features_ = std::move(features);
    groups_ = std::move(groups);
    filters_ = std::move(filters);
    presets_ = std::move(presets);
    if (structureChanged) {
        refresh();
        return;
    }

    std::unordered_map<std::string, const cad::application::FeatureDescriptor*> featureById;
    for (const auto& feature : features_) featureById.emplace(feature.id, &feature);
    std::unordered_map<std::string, const cad::application::VisibilityGroup*> groupById;
    for (const auto& group : groups_) groupById.emplace(group.id, &group);
    std::unordered_map<std::string, const cad::application::VisibilityPreset*> presetById;
    for (const auto& preset : presets_) presetById.emplace(preset.id, &preset);

    for (QTreeWidgetItemIterator iterator(tree_); *iterator; ++iterator) {
        auto* item = *iterator;
        const auto feature = featureById.find(item->data(0, FeatureIdRole).toString().toStdString());
        if (feature != featureById.end()) {
            item->setForeground(0, feature->second->visible
                ? QBrush() : QBrush(QColor(130, 130, 130)));
            item->setToolTip(0, feature->second->visible ? QString{} : QStringLiteral("Hidden"));
            continue;
        }
        const auto group = groupById.find(item->data(0, GroupIdRole).toString().toStdString());
        if (group != groupById.end()) {
            const auto& value = *group->second;
            item->setText(0, QString::fromStdString(value.name)
                + " (" + QString::number(static_cast<qulonglong>(value.memberFeatureIds.size()))
                + ") — " + groupModeText(value.mode));
            item->setForeground(0, value.mode == cad::application::VisibilityMode::Hidden
                ? QBrush(QColor(130, 130, 130)) : QBrush());
            continue;
        }
        const auto preset = presetById.find(item->data(0, PresetIdRole).toString().toStdString());
        if (preset != presetById.end()) {
            item->setText(0, QString::fromStdString(preset->second->name));
            continue;
        }
        const int filterKind = item->data(0, FilterKindRole).toInt();
        if (filterKind != 0) {
            const QString key = item->data(0, FilterKeyRole).toString();
            std::optional<cad::application::VisibilityMode> mode;
            if (filterKind == 1) {
                for (const auto category : cad::application::visibilityCategories()) {
                    if (key == QString::fromLatin1(cad::application::visibilityCategoryId(category))) {
                        const auto found = filters_.categoryModes.find(category);
                        if (found != filters_.categoryModes.end()) mode = found->second;
                        break;
                    }
                }
            } else if (filterKind == 2) {
                const auto found = filters_.typeModes.find(key.toStdString());
                if (found != filters_.typeModes.end()) mode = found->second;
            } else if (filterKind == 3) {
                const auto role = key == "Sketch" ? cad::parametric::FeatureRole::Sketch
                    : key == "Face" ? cad::parametric::FeatureRole::Face
                    : cad::parametric::FeatureRole::Generic;
                const auto found = filters_.roleModes.find(role);
                if (found != filters_.roleModes.end()) mode = found->second;
            }
            const QString title = item->text(0).section(" — ", 0, 0);
            item->setText(0, title + " — " + filterModeText(mode));
            item->setForeground(0, mode && *mode == cad::application::VisibilityMode::Hidden
                ? QBrush(QColor(130, 130, 130)) : QBrush());
        }
    }
}

void FeatureEditorPanel::setGroupHandlers(
    std::function<void(const QStringList&)> createGroup,
    std::function<void(const QString&, cad::application::VisibilityMode)> setVisibility,
    std::function<void(const QString&)> isolateGroup,
    std::function<void(const QString&)> removeGroup,
    std::function<void(const QString&, const QStringList&)> addToGroup,
    std::function<void(const QString&, const QStringList&)> removeFromGroup)
{
    createGroupHandler_ = std::move(createGroup);
    groupVisibilityHandler_ = std::move(setVisibility);
    isolateGroupHandler_ = std::move(isolateGroup);
    removeGroupHandler_ = std::move(removeGroup);
    addToGroupHandler_ = std::move(addToGroup);
    removeFromGroupHandler_ = std::move(removeFromGroup);
}

void FeatureEditorPanel::setFilterHandlers(
    std::function<void(cad::application::VisibilityCategory,
                       std::optional<cad::application::VisibilityMode>)> category,
    std::function<void(const QString&,
                       std::optional<cad::application::VisibilityMode>)> type,
    std::function<void(cad::parametric::FeatureRole,
                       std::optional<cad::application::VisibilityMode>)> role,
    std::function<void()> clear,
    std::function<void(cad::application::VisibilityCategory)> showOnlyCategory)
{
    categoryFilterHandler_ = std::move(category);
    typeFilterHandler_ = std::move(type);
    roleFilterHandler_ = std::move(role);
    clearFiltersHandler_ = std::move(clear);
    showOnlyCategoryHandler_ = std::move(showOnlyCategory);
}

void FeatureEditorPanel::setPresetHandlers(
    std::function<void()> saveCurrent,
    std::function<void(const QString&)> apply,
    std::function<void(const QString&)> update,
    std::function<void(const QString&)> rename,
    std::function<void(const QString&)> remove)
{
    savePresetHandler_ = std::move(saveCurrent);
    applyPresetHandler_ = std::move(apply);
    updatePresetHandler_ = std::move(update);
    renamePresetHandler_ = std::move(rename);
    deletePresetHandler_ = std::move(remove);
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
        if (modelChangedHandler_) {
            modelChangedHandler_();
        } else {
            refresh();
        }
        if (!resultId.isEmpty()) selectFeatures({resultId});
    });
}

void FeatureEditorPanel::applyPropertyChange(
    const std::string& featureId,
    const cad::parametric::FeatureProperty& property,
    const cad::parametric::PropertyValue& value)
{
    if (!service_) return;
    bool sessionStarted = false;
    if (operationSession_) {
        std::map<std::string, double> original;
        if (const auto number = std::get_if<double>(&property.value)) {
            original[property.key] = *number;
        } else if (const auto integer = std::get_if<int>(&property.value)) {
            original[property.key] = static_cast<double>(*integer);
        } else if (const auto boolean = std::get_if<bool>(&property.value)) {
            original[property.key] = *boolean ? 1.0 : 0.0;
        }
        sessionStarted = operationSession_->beginEdit(
            cad::application::InteractiveOperationKind::FeatureEdit,
            featureId, std::move(original));
        if (sessionStarted) {
            if (const auto number = std::get_if<double>(&value))
                operationSession_->updatePreview(property.key, *number);
            else if (const auto integer = std::get_if<int>(&value))
                operationSession_->updatePreview(property.key, static_cast<double>(*integer));
            else if (const auto boolean = std::get_if<bool>(&value))
                operationSession_->updatePreview(property.key, *boolean ? 1.0 : 0.0);
        }
    }
    const auto result = service_->setFeatureProperty(featureId, property.key, value);
    if (sessionStarted) {
        if (result.success) operationSession_->commit();
        else operationSession_->cancel();
    }
    reportResult(result);
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

void FeatureEditorPanel::setVisibilityHandlers(
    std::function<void(const QStringList&)> hide,
    std::function<void(const QStringList&)> show,
    std::function<void(const QStringList&)> isolate,
    std::function<void()> showAll,
    std::function<void(const QStringList&)> ghostOthers,
    std::function<void()> clearGhosting)
{
    hideHandler_ = std::move(hide);
    showHandler_ = std::move(show);
    isolateHandler_ = std::move(isolate);
    showAllHandler_ = std::move(showAll);
    ghostOthersHandler_ = std::move(ghostOthers);
    clearGhostingHandler_ = std::move(clearGhosting);
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
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);

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
    connect(tree_, &QTreeWidget::customContextMenuRequested, this,
        [this](const QPoint& position) {
            auto* item = tree_->itemAt(position);
            if (!item) return;
            if (item->data(0, PresetRootRole).toBool()) {
                QMenu menu(tree_);
                auto* save = menu.addAction("Save Current as Preset...");
                QObject::connect(save, &QAction::triggered, this,
                    [this]() { if (savePresetHandler_) savePresetHandler_(); });
                menu.exec(tree_->viewport()->mapToGlobal(position));
                return;
            }
            const auto presetId = item->data(0, PresetIdRole).toString();
            if (!presetId.isEmpty()) {
                QMenu menu(tree_);
                auto* apply = menu.addAction("Apply Preset");
                auto* update = menu.addAction("Update Preset");
                auto* rename = menu.addAction("Rename Preset...");
                auto* remove = menu.addAction("Delete Preset");
                QObject::connect(apply, &QAction::triggered, this,
                    [this, presetId]() { if (applyPresetHandler_) applyPresetHandler_(presetId); });
                QObject::connect(update, &QAction::triggered, this,
                    [this, presetId]() { if (updatePresetHandler_) updatePresetHandler_(presetId); });
                QObject::connect(rename, &QAction::triggered, this,
                    [this, presetId]() { if (renamePresetHandler_) renamePresetHandler_(presetId); });
                QObject::connect(remove, &QAction::triggered, this,
                    [this, presetId]() { if (deletePresetHandler_) deletePresetHandler_(presetId); });
                menu.exec(tree_->viewport()->mapToGlobal(position));
                return;
            }
            if (item->data(0, FilterRootRole).toBool()) {
                QMenu menu(tree_);
                auto* clear = menu.addAction("Clear Filters");
                QObject::connect(clear, &QAction::triggered, this,
                    [this]() { if (clearFiltersHandler_) clearFiltersHandler_(); });
                menu.exec(tree_->viewport()->mapToGlobal(position));
                return;
            }
            const int filterKind = item->data(0, FilterKindRole).toInt();
            if (filterKind != 0) {
                const auto key = item->data(0, FilterKeyRole).toString();
                QMenu menu(tree_);
                auto* visible = menu.addAction("Show");
                auto* ghosted = menu.addAction("Ghost");
                auto* hidden = menu.addAction("Hide");
                auto* clear = menu.addAction("Clear Filter");
                QAction* showOnly = nullptr;
                if (filterKind == 1) showOnly = menu.addAction("Show Only This Category");
                const auto apply = [this, filterKind, key](
                    const std::optional<cad::application::VisibilityMode> mode) {
                    if (filterKind == 1) {
                        for (const auto category : cad::application::visibilityCategories()) {
                            if (QString::fromLatin1(cad::application::visibilityCategoryId(category)) == key) {
                                if (categoryFilterHandler_) categoryFilterHandler_(category, mode);
                                return;
                            }
                        }
                    } else if (filterKind == 2) {
                        if (typeFilterHandler_) typeFilterHandler_(key, mode);
                    } else if (filterKind == 3) {
                        const auto role = key == "Sketch"
                            ? cad::parametric::FeatureRole::Sketch
                            : key == "Face" ? cad::parametric::FeatureRole::Face
                            : cad::parametric::FeatureRole::Generic;
                        if (roleFilterHandler_) roleFilterHandler_(role, mode);
                    }
                };
                QObject::connect(visible, &QAction::triggered, this,
                    [apply]() { apply(cad::application::VisibilityMode::Visible); });
                QObject::connect(ghosted, &QAction::triggered, this,
                    [apply]() { apply(cad::application::VisibilityMode::Ghosted); });
                QObject::connect(hidden, &QAction::triggered, this,
                    [apply]() { apply(cad::application::VisibilityMode::Hidden); });
                QObject::connect(clear, &QAction::triggered, this,
                    [apply]() { apply({}); });
                if (showOnly) {
                    QObject::connect(showOnly, &QAction::triggered, this,
                        [this, key]() {
                            for (const auto category : cad::application::visibilityCategories()) {
                                if (QString::fromLatin1(cad::application::visibilityCategoryId(category)) == key
                                    && showOnlyCategoryHandler_) {
                                    showOnlyCategoryHandler_(category);
                                    return;
                                }
                            }
                        });
                }
                menu.exec(tree_->viewport()->mapToGlobal(position));
                return;
            }
            const auto groupId = item->data(0, GroupIdRole).toString();
            if (!groupId.isEmpty()) {
                const auto selected = selectedFeatureIds();
                auto group = std::find_if(groups_.begin(), groups_.end(),
                    [&groupId](const auto& candidate) {
                        return QString::fromStdString(candidate.id) == groupId;
                    });
                if (group == groups_.end()) return;
                QMenu menu(tree_);
                auto* visible = menu.addAction("Show Group");
                auto* ghosted = menu.addAction("Ghost Group");
                auto* hidden = menu.addAction("Hide Group");
                auto* isolate = menu.addAction("Isolate Group");
                menu.addSeparator();
                auto* add = menu.addAction("Add Selection to Group");
                auto* remove = menu.addAction("Remove Selection from Group");
                auto* deleteGroup = menu.addAction("Delete Group");
                add->setEnabled(!selected.isEmpty());
                remove->setEnabled(!selected.isEmpty());
                QObject::connect(visible, &QAction::triggered, this,
                    [this, groupId]() { if (groupVisibilityHandler_)
                        groupVisibilityHandler_(groupId, cad::application::VisibilityMode::Visible); });
                QObject::connect(ghosted, &QAction::triggered, this,
                    [this, groupId]() { if (groupVisibilityHandler_)
                        groupVisibilityHandler_(groupId, cad::application::VisibilityMode::Ghosted); });
                QObject::connect(hidden, &QAction::triggered, this,
                    [this, groupId]() { if (groupVisibilityHandler_)
                        groupVisibilityHandler_(groupId, cad::application::VisibilityMode::Hidden); });
                QObject::connect(isolate, &QAction::triggered, this,
                    [this, groupId]() { if (isolateGroupHandler_) isolateGroupHandler_(groupId); });
                QObject::connect(add, &QAction::triggered, this,
                    [this, groupId, selected]() { if (addToGroupHandler_)
                        addToGroupHandler_(groupId, selected); });
                QObject::connect(remove, &QAction::triggered, this,
                    [this, groupId, selected]() { if (removeFromGroupHandler_)
                        removeFromGroupHandler_(groupId, selected); });
                QObject::connect(deleteGroup, &QAction::triggered, this,
                    [this, groupId]() { if (removeGroupHandler_) removeGroupHandler_(groupId); });
                menu.exec(tree_->viewport()->mapToGlobal(position));
                return;
            }
            if (!item->data(0, FeatureIdRole).isValid()) return;
            if (!item->isSelected()) {
                tree_->clearSelection();
                item->setSelected(true);
                tree_->setCurrentItem(item);
            }
            const auto ids = selectedFeatureIds();
            if (ids.isEmpty()) return;
            bool hasVisible = false;
            bool hasHidden = false;
            for (const auto& feature : features_) {
                if (!ids.contains(QString::fromStdString(feature.id))) continue;
                if (feature.visible) hasVisible = true;
                else hasHidden = true;
            }
            QMenu menu(tree_);
            auto* hide = menu.addAction("Hide");
            auto* show = menu.addAction("Show");
            auto* isolate = menu.addAction("Isolate");
            menu.addSeparator();
            auto* ghostOthers = menu.addAction("Ghost Others");
            auto* clearGhosting = menu.addAction("Clear Ghosting");
            auto* createGroup = menu.addAction("Create Group...");
            menu.addSeparator();
            auto* showAll = menu.addAction("Show All");
            hide->setEnabled(hasVisible);
            show->setEnabled(hasHidden);
            QObject::connect(hide, &QAction::triggered, this,
                [this, ids]() { if (hideHandler_) hideHandler_(ids); });
            QObject::connect(show, &QAction::triggered, this,
                [this, ids]() { if (showHandler_) showHandler_(ids); });
            QObject::connect(isolate, &QAction::triggered, this,
                [this, ids]() { if (isolateHandler_) isolateHandler_(ids); });
            QObject::connect(ghostOthers, &QAction::triggered, this,
                [this, ids]() { if (ghostOthersHandler_) ghostOthersHandler_(ids); });
            QObject::connect(clearGhosting, &QAction::triggered, this,
                [this]() { if (clearGhostingHandler_) clearGhostingHandler_(); });
            QObject::connect(createGroup, &QAction::triggered, this,
                [this, ids]() { if (createGroupHandler_) createGroupHandler_(ids); });
            QObject::connect(showAll, &QAction::triggered, this,
                [this]() { if (showAllHandler_) showAllHandler_(); });
            menu.exec(tree_->viewport()->mapToGlobal(position));
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
    QElapsedTimer performanceTimer;
    const bool performanceDiagnostics = qEnvironmentVariableIsSet("PARAMETRIC_CAD_PERF");
    if (performanceDiagnostics) performanceTimer.start();
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

    QTreeWidgetItem* featureParent = bodyItem;
    auto* presetsItem = new QTreeWidgetItem(bodyItem, QStringList{"Visibility Presets"});
    presetsItem->setData(0, PresetRootRole, true);
    presetsItem->setExpanded(true);
    for (const auto& preset : presets_) {
        auto* item = new QTreeWidgetItem(presetsItem,
            QStringList{QString::fromStdString(preset.name)});
        item->setData(0, PresetIdRole, QString::fromStdString(preset.id));
    }
    if (!groups_.empty()) {
        auto* groupsItem = new QTreeWidgetItem(bodyItem, QStringList{"Visibility Groups"});
        groupsItem->setExpanded(true);
        std::unordered_map<std::string, QTreeWidgetItem*> groupItems;
        for (const auto& group : groups_) {
            const auto title = QString::fromStdString(group.name)
                + " (" + QString::number(static_cast<qulonglong>(group.memberFeatureIds.size()))
                + ") — " + groupModeText(group.mode);
            auto* item = new QTreeWidgetItem(groupsItem, QStringList{title});
            item->setData(0, GroupIdRole, QString::fromStdString(group.id));
            groupItems.emplace(group.id, item);
            if (group.mode == cad::application::VisibilityMode::Hidden) {
                item->setForeground(0, QBrush(QColor(130, 130, 130)));
            }
        }
        for (const auto& group : groups_) {
            if (!group.parentId) continue;
            const auto item = groupItems.find(group.id);
            const auto parent = groupItems.find(*group.parentId);
            if (item == groupItems.end() || parent == groupItems.end()) continue;
            groupsItem->removeChild(item->second);
            parent->second->addChild(item->second);
        }
        featureParent = new QTreeWidgetItem(bodyItem, QStringList{"Features"});
        featureParent->setExpanded(true);
    }

    auto* filtersItem = new QTreeWidgetItem(bodyItem, QStringList{"Visibility Filters"});
    filtersItem->setData(0, FilterRootRole, true);
    filtersItem->setExpanded(true);
    const auto addFilterItem = [](QTreeWidgetItem* parent, const QString& title,
                                              const int kind, const QString& key,
                                              const std::optional<cad::application::VisibilityMode>& mode) {
        auto* item = new QTreeWidgetItem(parent, QStringList{title + " — " + filterModeText(mode)});
        item->setData(0, FilterKindRole, kind);
        item->setData(0, FilterKeyRole, key);
        if (mode && *mode == cad::application::VisibilityMode::Hidden)
            item->setForeground(0, QBrush(QColor(130, 130, 130)));
        return item;
    };
    const auto categoryMode = [this](const cad::application::VisibilityCategory category)
        -> std::optional<cad::application::VisibilityMode> {
        const auto found = filters_.categoryModes.find(category);
        return found == filters_.categoryModes.end()
            ? std::optional<cad::application::VisibilityMode>{} : found->second;
    };
    for (const auto category : cad::application::visibilityCategories()) {
        addFilterItem(filtersItem,
            QString::fromLatin1(cad::application::visibilityCategoryName(category)), 1,
            QString::fromLatin1(cad::application::visibilityCategoryId(category)),
            categoryMode(category));
    }
    auto* rolesItem = new QTreeWidgetItem(filtersItem, QStringList{"Roles"});
    rolesItem->setExpanded(false);
    const std::array<std::pair<cad::parametric::FeatureRole, const char*>, 3> roles{{
        {cad::parametric::FeatureRole::Generic, "Generic"},
        {cad::parametric::FeatureRole::Sketch, "Sketch"},
        {cad::parametric::FeatureRole::Face, "Face"}}};
    for (const auto& [role, name] : roles) {
        const auto found = filters_.roleModes.find(role);
        addFilterItem(rolesItem, QString::fromLatin1(name), 3,
            QString::fromLatin1(name), found == filters_.roleModes.end()
                ? std::optional<cad::application::VisibilityMode>{} : found->second);
    }
    auto* typesItem = new QTreeWidgetItem(filtersItem, QStringList{"Types"});
    typesItem->setExpanded(false);
    for (const auto& type : cad::application::knownVisibilityTypeIds()) {
        const auto found = filters_.typeModes.find(type);
        addFilterItem(typesItem, QString::fromStdString(type), 2,
            QString::fromStdString(type), found == filters_.typeModes.end()
                ? std::optional<cad::application::VisibilityMode>{} : found->second);
    }

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
                featureParent,
                QStringList{title}
            );

        const QString id = QString::fromStdString(feature.id);

        item->setData(
            0,
            FeatureIdRole,
            id
        );

        if (!feature.visible) {
            item->setForeground(0, QBrush(QColor(130, 130, 130)));
            item->setToolTip(0, "Hidden");
        }

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
    if (performanceDiagnostics) {
        std::size_t itemCount = 0;
        for (QTreeWidgetItemIterator iterator(tree_); *iterator; ++iterator) ++itemCount;
        qInfo().noquote() << "FeatureEditorPanel tree rebuild: features=" << features_.size()
                          << "items=" << itemCount
                          << "ms=" << performanceTimer.elapsed();
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
                const auto apply = [this, featureId, property, after]() {
                    if (!service_) return;
                    if (propertyMatchesCurrentValue(featureId, property.key, after)) return;
                    applyPropertyChange(featureId, property, after);
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
                    const auto apply = [this, featureId, property, after]() {
                        if (!service_) return;
                        if (propertyMatchesCurrentValue(featureId, property.key, after)) return;
                        applyPropertyChange(featureId, property, after);
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
                        applyPropertyChange(featureId, property, checked);
                    });
                });
        } else if (property.editable && std::holds_alternative<std::string>(property.value)
                   && (property.key == "distribution" || property.key == "orientation"
                       || property.key == "axisType")) {
            auto* editor = new QComboBox(propertiesWidget_);
            if (property.key == "distribution") {
                editor->addItems({QStringLiteral("FixedSpacing"), QStringLiteral("FitCount")});
            } else if (property.key == "orientation") {
                editor->addItems({QStringLiteral("Fixed"), QStringLiteral("Tangent")});
            } else {
                editor->addItems({QStringLiteral("GlobalX"), QStringLiteral("GlobalY"),
                    QStringLiteral("GlobalZ")});
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
                        applyPropertyChange(featureId, property, after);
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
