#include "viewer/MainWindow.h"
#include "viewer/CadViewer.h"
#include "viewer/FeatureEditorPanel.h"
#include "viewer/BimNavigationPanel.h"
#include "viewer/BimInspectorPanel.h"
#include "viewer/ModelPresenter.h"
#include "commands/FeatureCommands.h"
#include "model/FeatureVisibility.h"
#include "operations/SketchTrimService.h"
#include "operations/SketchExtendService.h"
#include "operations/SketchConstraintSolver.h"
#include "operations/ImportedFeature.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QKeySequence>
#include <QMessageBox>
#include <QInputDialog>
#include <QStandardPaths>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QTimer>
#include <QDebug>
#include <QHash>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace
{
constexpr int ModelPanelWidth = 320;
constexpr qint64 ProjectLoadTimeBudgetMs = 30;

std::string pointRoleText(const cad::parametric::SketchPointRole role)
{
    switch (role) {
    case cad::parametric::SketchPointRole::LineStart: return "Start";
    case cad::parametric::SketchPointRole::LineEnd: return "End";
    case cad::parametric::SketchPointRole::ArcStart: return "Start";
    case cad::parametric::SketchPointRole::ArcEnd: return "End";
    case cad::parametric::SketchPointRole::CircleCenter: return "Center";
    case cad::parametric::SketchPointRole::ArcCenter: return "Center";
    }
    return "Point";
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      operationSession_(),
      visibilityManager_(),
      project_(modeling_, &visibilityManager_),
      viewer_(new CadViewer(this, &operationSession_)),
      presenter_(std::make_unique<cad::viewer::ModelPresenter>(
          modeling_.body(), visibilityManager_, *viewer_))
{
    updateTitle();
    setCentralWidget(viewer_);

    createActions();
    createParametricPanel();
    createBimPanel();
    featureEditorPanel_->setSketchConstraintSelectionHandler(
        [this](const QString& id) { selectSketchConstraint(id); });
    featureEditorPanel_->setSketchConstraintEditHandler(
        [this](const QString& id) { editSketchConstraint(id); });
    featureEditorPanel_->setSketchConstraintDeleteHandler(
        [this](const QString& id) { deleteSketchConstraint(id); });
    viewer_->setPushPullCommittedHandler(
        [this](const QString& featureId,
               const cad::topology::TopologicalReference& faceReference,
               const gp_Vec& normal, const double distance) {
            reportResult(modeling_.pushPull(
                featureId.toStdString(), faceReference, normal, distance));
        }
    );
    viewer_->setTransformCommittedHandler(
        [this](const QString& featureId, const gp_Trsf& delta) {
            reportResult(modeling_.transformFeatureDelta(
                featureId.toStdString(), delta));
        }
    );
    viewer_->setTransformCopyCommittedHandler(
        [this](const QString& featureId, const gp_Trsf& delta) {
            reportResult(modeling_.duplicateFeatureWithDelta(
                featureId.toStdString(), delta));
        }
    );
    viewer_->setSpatialBoxChangedHandler(
        [this](const gp_Pnt& min, const gp_Pnt& max) {
            auto rule = visibilityManager_.spatialRule();
            if (!rule.enabled) return;
            rule.min = min;
            rule.max = max;
            visibilityManager_.setSpatialRule(rule);
            refreshVisibilityView();
        });
    viewer_->setSectionInteractionStatusHandler(
        [this](const QString& message) { statusBar()->showMessage(message); });
    viewer_->setSketchPointClickedHandler(
        [this](const gp_Pnt2d& point, const double tolerance) {
            if (sketchTool_ == SketchTool::Trim || sketchTool_ == SketchTool::Extend)
                viewer_->clearSketchTrimPreview();
            if (activeSketchId_.empty()
                || (sketchTool_ != SketchTool::Trim && sketchTool_ != SketchTool::Extend)) {
                handleSketchPoint(point, tolerance);
                return;
            }
            const auto result = sketchTool_ == SketchTool::Trim
                ? modeling_.trimSketchEntity(activeSketchId_, point, tolerance)
                : modeling_.extendSketchEntity(activeSketchId_, point, tolerance);
            if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
            else refreshModelView(false);
        });
    viewer_->setSketchMouseMovedHandler(
        [this](const gp_Pnt2d& point, const double tolerance) {
            if (activeSketchId_.empty()
                || (sketchTool_ != SketchTool::Trim && sketchTool_ != SketchTool::Extend)) return;
            const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
                modeling_.body().findFeature(activeSketchId_));
            if (!sketch) { viewer_->clearSketchTrimPreview(); return; }
            if (sketchTool_ == SketchTool::Trim) {
                const auto plan = cad::operations::SketchTrimService::previewTrim(
                    *sketch, point, tolerance);
                if (plan) viewer_->setSketchTrimPreview(plan->removedEntities);
                else viewer_->clearSketchTrimPreview();
            } else {
                const auto plan = cad::operations::SketchExtendService::previewExtend(
                    *sketch, point, tolerance);
                if (plan) viewer_->setSketchExtendPreview(plan->extensionSpan);
                else viewer_->clearSketchTrimPreview();
            }
        });
    viewer_->setSketchCancelHandler(
        [this]() {
            sketchFirstPoint_.reset(); sketchSecondPoint_.reset();
            if (sketchTool_ == SketchTool::Trim || sketchTool_ == SketchTool::Extend
                || sketchTool_ == SketchTool::Coincident
                || sketchTool_ == SketchTool::Horizontal
                || sketchTool_ == SketchTool::Vertical
                || sketchTool_ == SketchTool::Distance
                || sketchTool_ == SketchTool::Radius
                || sketchTool_ == SketchTool::HorizontalDistance
                || sketchTool_ == SketchTool::VerticalDistance
                || sketchTool_ == SketchTool::Angle
                || sketchTool_ == SketchTool::Parallel
                || sketchTool_ == SketchTool::Perpendicular
                || sketchTool_ == SketchTool::AngleBetweenLines
                || sketchTool_ == SketchTool::Tangent || sketchTool_ == SketchTool::Equal) selectSketchLineTool();
            constraintFirstPoint_.reset();
        });
    viewer_->setSketchConstraintMarkerClickedHandler(
        [this](const std::string& id) { selectSketchConstraint(QString::fromStdString(id)); });
    viewer_->setSketchConstraintMarkerHoveredHandler(
        [this](const std::string& id) {
            const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
                modeling_.body().findFeature(activeSketchId_));
            if (!sketch) return;
            if (id.empty()) {
                if (!selectedConstraintId_.isEmpty()) viewer_->setSketchConstraintHighlight(
                    *sketch, selectedConstraintId_.toStdString());
                else viewer_->clearSketchConstraintHighlight();
            } else if (QString::fromStdString(id) != selectedConstraintId_) {
                viewer_->setSketchConstraintHighlight(*sketch, id);
            }
        });
    connect(&modeling_.undoStack(), &QUndoStack::indexChanged, this, [this]() {
        viewer_->clearSketchTrimPreview();
        const int currentIndex = modeling_.undoStack().index();
        const int changedIndex = currentIndex > lastUndoStackIndex_
            ? currentIndex - 1 : currentIndex;
        const bool visibilityChanged = currentIndex != lastUndoStackIndex_
            && modeling_.isVisibilityCommandAt(changedIndex);
        lastUndoStackIndex_ = currentIndex;
        if (visibilityChanged) refreshVisibilityView();
        else refreshModelView();
        featureEditorPanel_->scheduleRefresh();
    });
    connect(&modeling_.undoStack(), &QUndoStack::cleanChanged, this, [this](bool) { updateTitle(); });
    connect(&projectLoadProgressTimer_, &QTimer::timeout,
        this, &MainWindow::updateProjectLoadProgress);
    connect(&projectLoadHeartbeatTimer_, &QTimer::timeout,
        this, &MainWindow::sampleProjectLoadEventLoop);
    connect(&projectLoadWatcher_, &QFutureWatcher<std::shared_ptr<cad::application::ProjectLoadResult>>::finished,
        this, &MainWindow::finishProjectLoad);
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    connect(&ifcImportProgressTimer_, &QTimer::timeout,
        this, &MainWindow::updateIfcImportProgress);
    connect(&ifcImportWatcher_, &QFutureWatcher<std::shared_ptr<cad::import::IfcImportResult>>::finished,
        this, &MainWindow::finishIfcImport);
#endif
    statusBar()->showMessage("Ready");
}


MainWindow::~MainWindow()
{
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    if (ifcImporting_ && ifcImportCancelRequested_) {
        ifcImportCancelRequested_->store(true, std::memory_order_relaxed);
        ifcImportWatcher_.future().waitForFinished();
    }
#endif
    disconnect(&modeling_.undoStack(), nullptr, this, nullptr);
    featureEditorPanel_->setService(nullptr);
}

void MainWindow::createParametricPanel()
{
    auto* dockWidget =
        new QDockWidget("Model", this);

    dockWidget->setObjectName("ParametricModelDock");
    dockWidget->setFixedWidth(ModelPanelWidth);
    dockWidget->setSizePolicy(
        QSizePolicy::Fixed,
        QSizePolicy::Expanding
    );

    featureEditorPanel_ =
        new FeatureEditorPanel(dockWidget);
    featureEditorPanel_->setSizePolicy(
        QSizePolicy::Fixed,
        QSizePolicy::Expanding
    );

    featureEditorPanel_->setService(&modeling_);
    featureEditorPanel_->setOperationSession(&operationSession_);
    featureEditorPanel_->setFeatures(modeling_.features());
    featureEditorPanel_->setActionState(modeling_.actionState(selectedIds()));

    featureEditorPanel_->setModelChangedHandler(
        [this]() {
            refreshModelView();
        }
    );

    featureEditorPanel_->setFeatureSelectedHandler(
        [this](const QStringList& featureIds) {
            applySelection(featureIds);
        }
    );
    featureEditorPanel_->setFeatureDoubleClickedHandler(
        [this](const QString& featureId) { editSketchById(featureId); });
    featureEditorPanel_->setRevolveAxisPickHandler(
        [this](const QString& featureId, const QString& axisType) {
            beginRevolveAxisPick(featureId, axisType, true);
        });
    featureEditorPanel_->setVisibilityHandlers(
        [this](const QStringList& featureIds) {
            std::vector<std::string> ids;
            for (const auto& id : featureIds) ids.push_back(id.toStdString());
            const auto result = modeling_.setFeatureVisibility(ids, false);
            if (!result.success) statusBar()->showMessage(
                QString::fromStdString(result.error), 3000);
            else refreshVisibilityView();
        },
        [this](const QStringList& featureIds) {
            std::vector<std::string> ids;
            for (const auto& id : featureIds) ids.push_back(id.toStdString());
            const auto result = modeling_.setFeatureVisibility(ids, true);
            if (!result.success) statusBar()->showMessage(
                QString::fromStdString(result.error), 3000);
            else refreshVisibilityView();
        },
        [this](const QStringList& featureIds) {
            std::vector<std::string> ids;
            for (const auto& id : featureIds) ids.push_back(id.toStdString());
            visibilityManager_.setIsolatedFeatures(ids);
            refreshVisibilityView();
        },
        [this]() {
            visibilityManager_.clearIsolation();
            visibilityManager_.clearGhosting();
            visibilityManager_.clearSpatialRule();
            viewer_->clearSpatialBox();
            const auto beforeGroups = visibilityManager_.groups();
            const auto beforeFilters = visibilityManager_.filters();
            visibilityManager_.resetGroupVisibility();
            visibilityManager_.clearFilters();
            const auto afterGroups = visibilityManager_.groups();
            const auto afterFilters = visibilityManager_.filters();
            modeling_.undoStack().beginMacro("Show All");
            bool groupVisibilityChanged = false;
            for (std::size_t index = 0; index < beforeGroups.size(); ++index) {
                if (beforeGroups[index].mode != afterGroups[index].mode) {
                    groupVisibilityChanged = true;
                    break;
                }
            }
            if (groupVisibilityChanged) {
                modeling_.undoStack().push(new cad::commands::SetVisibilityGroupsCommand(
                    visibilityManager_, beforeGroups, afterGroups, "Reset Group Visibility"));
            }
            if (!(beforeFilters == afterFilters)) {
                modeling_.undoStack().push(new cad::commands::SetVisibilityFiltersCommand(
                    visibilityManager_, beforeFilters, afterFilters, "Reset Visibility Filters"));
            }
            const auto result = modeling_.showAllFeatures();
            modeling_.undoStack().endMacro();
            if (!result.success) statusBar()->showMessage(
                QString::fromStdString(result.error), 3000);
            else refreshVisibilityView();
        },
        [this](const QStringList& featureIds) {
            std::vector<std::string> ids;
            for (const auto& id : featureIds) ids.push_back(id.toStdString());
            visibilityManager_.ghostOthers(ids);
            refreshVisibilityView();
        },
        [this]() {
            visibilityManager_.clearGhosting();
            refreshVisibilityView();
        });
    featureEditorPanel_->setFilterHandlers(
        [this](const cad::application::VisibilityCategory category,
               const std::optional<cad::application::VisibilityMode> mode) {
            const auto before = visibilityManager_.filters();
            if (mode) visibilityManager_.setCategoryFilter(category, *mode);
            else visibilityManager_.clearCategoryFilter(category);
            const auto after = visibilityManager_.filters();
            if (!(before == after)) commitVisibilityFilters(before, after, "Change Category Filter");
        },
        [this](const QString& typeId,
               const std::optional<cad::application::VisibilityMode> mode) {
            const auto before = visibilityManager_.filters();
            if (mode) visibilityManager_.setTypeFilter(typeId.toStdString(), *mode);
            else visibilityManager_.clearTypeFilter(typeId.toStdString());
            const auto after = visibilityManager_.filters();
            if (!(before == after)) commitVisibilityFilters(before, after, "Change Type Filter");
        },
        [this](const cad::parametric::FeatureRole role,
               const std::optional<cad::application::VisibilityMode> mode) {
            const auto before = visibilityManager_.filters();
            if (mode) visibilityManager_.setRoleFilter(role, *mode);
            else visibilityManager_.clearRoleFilter(role);
            const auto after = visibilityManager_.filters();
            if (!(before == after)) commitVisibilityFilters(before, after, "Change Role Filter");
        },
        [this]() {
            const auto before = visibilityManager_.filters();
            visibilityManager_.clearFilters();
            const auto after = visibilityManager_.filters();
            if (!(before == after)) commitVisibilityFilters(before, after, "Clear Visibility Filters");
        },
        [this](const cad::application::VisibilityCategory category) {
            const auto before = visibilityManager_.filters();
            visibilityManager_.clearFilters();
            for (const auto candidate : cad::application::visibilityCategories())
                visibilityManager_.setCategoryFilter(candidate,
                    candidate == category ? cad::application::VisibilityMode::Visible
                                          : cad::application::VisibilityMode::Hidden);
            const auto after = visibilityManager_.filters();
            if (!(before == after)) commitVisibilityFilters(before, after, "Show Only Category");
        });
    featureEditorPanel_->setPresetHandlers(
        [this]() {
            bool ok = false;
            const auto name = QInputDialog::getText(this, "Save Visibility Preset",
                "Name:", QLineEdit::Normal, {}, &ok);
            if (!ok || name.trimmed().isEmpty()) return;
            const auto before = visibilityManager_.presets();
            std::string error;
            if (visibilityManager_.saveCurrentAsPreset(
                name.trimmed().toStdString(), modeling_.body(), error)) {
                commitVisibilityPresets(before, visibilityManager_.presets(),
                    "Save Visibility Preset");
            } else {
                statusBar()->showMessage(QString::fromStdString(error), 3000);
            }
        },
        [this](const QString& presetId) {
            const auto before = visibilityManager_.captureConfiguration(modeling_.body());
            std::string error;
            if (visibilityManager_.applyPreset(presetId.toStdString(), modeling_.body(), error)) {
                commitVisibilityConfiguration(before,
                    visibilityManager_.captureConfiguration(modeling_.body()),
                    "Apply Visibility Preset");
            } else {
                statusBar()->showMessage(QString::fromStdString(error), 3000);
            }
        },
        [this](const QString& presetId) {
            const auto before = visibilityManager_.presets();
            if (visibilityManager_.updatePreset(presetId.toStdString(), modeling_.body()))
                commitVisibilityPresets(before, visibilityManager_.presets(),
                    "Update Visibility Preset");
        },
        [this](const QString& presetId) {
            bool ok = false;
            const auto name = QInputDialog::getText(this, "Rename Visibility Preset",
                "Name:", QLineEdit::Normal, {}, &ok);
            if (!ok || name.trimmed().isEmpty()) return;
            const auto before = visibilityManager_.presets();
            std::string error;
            if (visibilityManager_.renamePreset(presetId.toStdString(),
                name.trimmed().toStdString(), error)) {
                commitVisibilityPresets(before, visibilityManager_.presets(),
                    "Rename Visibility Preset");
            } else {
                statusBar()->showMessage(QString::fromStdString(error), 3000);
            }
        },
        [this](const QString& presetId) {
            const auto before = visibilityManager_.presets();
            if (visibilityManager_.deletePreset(presetId.toStdString()))
                commitVisibilityPresets(before, visibilityManager_.presets(),
                    "Delete Visibility Preset");
        });
    featureEditorPanel_->setGroupHandlers(
        [this](const QStringList& featureIds) {
            bool ok = false;
            const auto name = QInputDialog::getText(this, "Create Visibility Group",
                "Name:", QLineEdit::Normal, {}, &ok);
            if (!ok || name.trimmed().isEmpty()) return;
            const auto before = visibilityManager_.groups();
            const auto id = visibilityManager_.createGroup(name.trimmed().toStdString());
            if (!id || !visibilityManager_.addFeaturesToGroup(*id,
                [&featureIds]() {
                    std::vector<std::string> ids;
                    for (const auto& featureId : featureIds) ids.push_back(featureId.toStdString());
                    return ids;
                }())) return;
            commitVisibilityGroups(before, visibilityManager_.groups(), "Create Visibility Group");
        },
        [this](const QString& groupId, const cad::application::VisibilityMode mode) {
            const auto before = visibilityManager_.groups();
            if (visibilityManager_.setGroupVisibility(groupId.toStdString(), mode)) {
                commitVisibilityGroups(before, visibilityManager_.groups(), "Change Group Visibility");
            }
        },
        [this](const QString& groupId) {
            std::vector<std::string> ids;
            for (const auto& group : visibilityManager_.groups()) {
                if (QString::fromStdString(group.id) != groupId) continue;
                ids.assign(group.memberFeatureIds.begin(), group.memberFeatureIds.end());
                break;
            }
            visibilityManager_.setIsolatedFeatures(ids);
            refreshVisibilityView();
        },
        [this](const QString& groupId) {
            const auto before = visibilityManager_.groups();
            if (visibilityManager_.removeGroup(groupId.toStdString())) {
                commitVisibilityGroups(before, visibilityManager_.groups(), "Delete Visibility Group");
            }
        },
        [this](const QString& groupId, const QStringList& featureIds) {
            const auto before = visibilityManager_.groups();
            std::vector<std::string> ids;
            for (const auto& featureId : featureIds) ids.push_back(featureId.toStdString());
            if (visibilityManager_.addFeaturesToGroup(groupId.toStdString(), ids)) {
                commitVisibilityGroups(before, visibilityManager_.groups(), "Add Features to Group");
            }
        },
        [this](const QString& groupId, const QStringList& featureIds) {
            const auto before = visibilityManager_.groups();
            std::vector<std::string> ids;
            for (const auto& featureId : featureIds) ids.push_back(featureId.toStdString());
            if (visibilityManager_.removeFeaturesFromGroup(groupId.toStdString(), ids)) {
                commitVisibilityGroups(before, visibilityManager_.groups(), "Remove Features from Group");
            }
        });

    connect(viewer_, &CadViewer::selectionChanged, this,
        [this](const cad::application::SelectionSnapshot& selection) {
            if (revolveRestoringSelectionMode_) return;
            if (handleRevolveAxisSelection(selection)) return;
            applySelectionSnapshot(selection);
    });

    dockWidget->setWidget(featureEditorPanel_);

    addDockWidget(
        Qt::LeftDockWidgetArea,
        dockWidget
    );
}

void MainWindow::createBimPanel()
{
    auto* dockWidget = new QDockWidget("BIM", this);
    dockWidget->setObjectName("BimNavigationDock");
    dockWidget->setMinimumWidth(ModelPanelWidth);
    bimNavigationPanel_ = new BimNavigationPanel(dockWidget);
    dockWidget->setWidget(bimNavigationPanel_);

    bimNavigationPanel_->setFeatureSelectedHandler(
        [this](const QStringList& ids) { applySelection(ids); });
    bimNavigationPanel_->setShowHideHandler(
        [this](const QStringList& ids, const bool visible) {
            std::vector<std::string> featureIds;
            for (const auto& id : ids) featureIds.push_back(id.toStdString());
            const auto result = modeling_.setFeatureVisibility(featureIds, visible);
            if (!result.success)
                statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        });
    bimNavigationPanel_->setIsolateHandler(
        [this](const QStringList& ids) {
            std::vector<std::string> featureIds;
            for (const auto& id : ids) featureIds.push_back(id.toStdString());
            visibilityManager_.setIsolatedFeatures(featureIds);
            refreshVisibilityView();
        });
    bimNavigationPanel_->setClearIsolationHandler(
        [this]() {
            visibilityManager_.clearIsolation();
            refreshVisibilityView();
        });
    addDockWidget(Qt::RightDockWidgetArea, dockWidget);
    auto* inspectorDock = new QDockWidget("BIM Inspector", this);
    inspectorDock->setObjectName("BimInspectorDock");
    inspectorDock->setMinimumWidth(ModelPanelWidth);
    bimInspectorPanel_ = new BimInspectorPanel(inspectorDock);
    inspectorDock->setWidget(bimInspectorPanel_);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    for (auto* action : menuBar()->actions()) {
        auto* menu = action->menu();
        if (menu && menu->title() == QStringLiteral("&View")) {
            menu->addSeparator();
            menu->addAction(dockWidget->toggleViewAction());
            break;
        }
    }
    refreshBimNavigation();
}

void MainWindow::refreshBimNavigation()
{
    if (!bimNavigationPanel_) return;
    bimNavigationModel_.rebuild(modeling_.body());
    std::map<QString, cad::application::VisibilityMode> modes;
    for (const auto& state : visibilityManager_.projection(modeling_.body()))
        modes.emplace(QString::fromStdString(state.featureId), state.mode);
    bimNavigationPanel_->setNavigation(bimNavigationModel_, modes);
}

void MainWindow::updateBimInspector(const QStringList& featureIds)
{
    if (!bimInspectorPanel_) return;
    if (featureIds.size() != 1) {
        bimInspectorPanel_->clear();
        return;
    }
    const auto feature = modeling_.body().findFeature(featureIds.front().toStdString());
    if (!feature || std::string(feature->typeId()) != "IfcImported") {
        bimInspectorPanel_->clear();
        return;
    }
    bimInspectorPanel_->setFeature(
        static_cast<const cad::parametric::ImportedFeature*>(feature.get()));
}

void MainWindow::commitVisibilityGroups(
    std::vector<cad::application::VisibilityGroup> before,
    std::vector<cad::application::VisibilityGroup> after,
    const QString& text)
{
    modeling_.undoStack().push(new cad::commands::SetVisibilityGroupsCommand(
        visibilityManager_, std::move(before), std::move(after), text));
}

void MainWindow::commitVisibilityFilters(
    cad::application::VisibilityFilterState before,
    cad::application::VisibilityFilterState after,
    const QString& text)
{
    modeling_.undoStack().push(new cad::commands::SetVisibilityFiltersCommand(
        visibilityManager_, std::move(before), std::move(after), text));
}

void MainWindow::commitVisibilityPresets(
    std::vector<cad::application::VisibilityPreset> before,
    std::vector<cad::application::VisibilityPreset> after,
    const QString& text)
{
    modeling_.undoStack().push(new cad::commands::SetVisibilityPresetsCommand(
        visibilityManager_, std::move(before), std::move(after), text));
}

void MainWindow::commitVisibilityConfiguration(
    cad::application::VisibilityConfiguration before,
    cad::application::VisibilityConfiguration after,
    const QString& text)
{
    modeling_.undoStack().push(new cad::commands::SetVisibilityConfigurationCommand(
        visibilityManager_, modeling_.body(), std::move(before), std::move(after), text));
}

void MainWindow::refreshModelView(const bool fitView)
{
    const auto result = presenter_->refreshModel();
    featureEditorPanel_->setFeatures(modeling_.features());
    featureEditorPanel_->setVisibilityGroups(visibilityManager_.groups());
    featureEditorPanel_->setVisibilityFilters(visibilityManager_.filters());
    featureEditorPanel_->setVisibilityPresets(visibilityManager_.presets());
    refreshBimNavigation();
    // ModelPresenter restores the OCCT selection, including topology
    // references. Do not select feature objects again here: that would
    // discard restored face/edge/vertex selection.
    applySelectionSnapshot(viewer_->selectionSnapshot(), false);
    featureEditorPanel_->setActionState(modeling_.actionState(selectedIds()));
    if (fitView) viewer_->fitAll();
    if (!activeSketchId_.empty()) {
        const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            modeling_.body().findFeature(activeSketchId_));
        if (sketch) viewer_->setSketchConstraintMarkers(*sketch, selectedConstraintId_.toStdString());
    }
    refreshConstraintManager();
    statusBar()->showMessage(result.rebuilt ? "Model updated" : QString::fromStdString(result.error), 3000);
}

void MainWindow::refreshVisibilityView()
{
    presenter_->refreshVisibility();
    featureEditorPanel_->updateVisibilityPresentation(
        modeling_.features(), visibilityManager_.groups(),
        visibilityManager_.filters(), visibilityManager_.presets());
    if (bimNavigationPanel_) {
        std::map<QString, cad::application::VisibilityMode> modes;
        for (const auto& state : visibilityManager_.projection(modeling_.body()))
            modes.emplace(QString::fromStdString(state.featureId), state.mode);
        bimNavigationPanel_->updateVisibility(modes);
    }
    applySelectionSnapshot(viewer_->selectionSnapshot(), false);
    featureEditorPanel_->setActionState(modeling_.actionState(selectedIds()));
    if (!activeSketchId_.empty()) {
        const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            modeling_.body().findFeature(activeSketchId_));
        if (sketch) viewer_->setSketchConstraintMarkers(*sketch, selectedConstraintId_.toStdString());
    }
    refreshConstraintManager();
}

void MainWindow::activateSpatialVisibility(
    const cad::application::VisibilityMode outsideMode)
{
    const auto bounds = visibilityManager_.spatialBounds(modeling_.body(), selectedIds());
    if (!bounds) {
        statusBar()->showMessage("No feature geometry is available for a spatial box", 3000);
        return;
    }
    const auto& min = bounds->first;
    const auto& max = bounds->second;
    const double margin = std::max({max.X() - min.X(), max.Y() - min.Y(), max.Z() - min.Z()})
        * 0.05 + 1.0e-3;
    cad::application::SpatialVisibilityRule rule;
    rule.enabled = true;
    rule.min = gp_Pnt(min.X() - margin, min.Y() - margin, min.Z() - margin);
    rule.max = gp_Pnt(max.X() + margin, max.Y() + margin, max.Z() + margin);
    rule.outsideMode = outsideMode;
    visibilityManager_.setSpatialRule(rule);
    viewer_->setSpatialBox(rule.min, rule.max);
    refreshVisibilityView();
}

void MainWindow::clearSpatialVisibility()
{
    visibilityManager_.clearSpatialRule();
    viewer_->clearSpatialBox();
    refreshVisibilityView();
}

void MainWindow::saveCurrentView()
{
    bool ok = false;
    const auto name = QInputDialog::getText(this, "Save Current View", "Name:",
        QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    cad::application::SavedView view;
    view.name = name.toStdString();
    view.visibility = visibilityManager_.captureConfiguration(modeling_.body());
    const auto isolated = visibilityManager_.isolatedFeatureIds();
    view.isolatedFeatureIds.insert(isolated.begin(), isolated.end());
    const auto ghosted = visibilityManager_.ghostedSelectionIds();
    view.ghostedSelectionIds.insert(ghosted.begin(), ghosted.end());
    view.spatialRule = visibilityManager_.spatialRule();
    const auto camera = viewer_->cameraState();
    view.cameraValid = camera.valid;
    view.cameraEye = camera.eye;
    view.cameraCenter = camera.center;
    view.cameraUp = camera.up;
    view.cameraScale = camera.scale;
    const auto& section = viewer_->sectionState();
    view.sectionActive = section.active;
    view.sectionAxis = static_cast<int>(section.axis);
    view.sectionFlipped = section.flipped;
    view.sectionOrigin = section.origin;
    if (!visibilityManager_.createSavedView(std::move(view)))
        statusBar()->showMessage("A saved view with that name already exists", 3000);
}

void MainWindow::restoreSavedView()
{
    const auto views = visibilityManager_.savedViews();
    if (views.empty()) return;
    QStringList names;
    for (const auto& view : views) names.append(QString::fromStdString(view.name));
    bool ok = false;
    const auto name = QInputDialog::getItem(this, "Restore View", "View:", names,
        0, false, &ok);
    if (!ok) return;
    const auto found = std::find_if(views.begin(), views.end(),
        [&name](const auto& view) { return QString::fromStdString(view.name) == name; });
    if (found == views.end()) return;
    std::string error;
    if (!visibilityManager_.applySavedView(*found, modeling_.body(), error)) {
        statusBar()->showMessage(QString::fromStdString(error), 3000);
        return;
    }
    if (found->spatialRule.enabled)
        viewer_->setSpatialBox(found->spatialRule.min, found->spatialRule.max);
    else
        viewer_->clearSpatialBox();
    if (found->sectionActive) {
        const auto axis = found->sectionAxis == 0 ? CadViewer::SectionAxis::X
            : found->sectionAxis == 1 ? CadViewer::SectionAxis::Y : CadViewer::SectionAxis::Z;
        viewer_->restoreSection(axis, found->sectionOrigin, found->sectionFlipped);
    } else {
        viewer_->clearSection();
    }
    viewer_->restoreCamera({found->cameraValid, found->cameraEye, found->cameraCenter,
        found->cameraUp, found->cameraScale});
    applySelection({});
    refreshVisibilityView();
}

void MainWindow::renameSavedView()
{
    const auto views = visibilityManager_.savedViews();
    if (views.empty()) return;
    QStringList names;
    for (const auto& view : views) names.append(QString::fromStdString(view.name));
    bool ok = false;
    const auto current = QInputDialog::getItem(this, "Rename View", "View:", names,
        0, false, &ok);
    if (!ok) return;
    const auto found = std::find_if(views.begin(), views.end(),
        [&current](const auto& view) { return QString::fromStdString(view.name) == current; });
    if (found == views.end()) return;
    const auto name = QInputDialog::getText(this, "Rename View", "Name:",
        QLineEdit::Normal, current, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    std::string error;
    if (!visibilityManager_.renameSavedView(found->id, name.toStdString(), error))
        statusBar()->showMessage(QString::fromStdString(error), 3000);
}

void MainWindow::deleteSavedView()
{
    const auto views = visibilityManager_.savedViews();
    if (views.empty()) return;
    QStringList names;
    for (const auto& view : views) names.append(QString::fromStdString(view.name));
    bool ok = false;
    const auto name = QInputDialog::getItem(this, "Delete View", "View:", names,
        0, false, &ok);
    if (!ok) return;
    const auto found = std::find_if(views.begin(), views.end(),
        [&name](const auto& view) { return QString::fromStdString(view.name) == name; });
    if (found != views.end()) visibilityManager_.deleteSavedView(found->id);
}

void MainWindow::refreshConstraintManager()
{
    if (activeSketchId_.empty()) {
        selectedConstraintId_.clear();
        featureEditorPanel_->setSketchConstraints({}, false);
        viewer_->clearSketchConstraintHighlight();
        return;
    }
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    if (!sketch) return;
    std::vector<SketchConstraintListItem> items;
    const auto solved = cad::operations::SketchConstraintSolver::solve(
        sketch->entities(), sketch->constraints());
    int dof = 0;
    for (const auto& entity : sketch->entities()) dof += std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, cad::parametric::SketchLine>) return 4;
        if constexpr (std::is_same_v<T, cad::parametric::SketchCircle>) return 3;
        return 5;
    }, entity);
    dof -= static_cast<int>(sketch->constraints().size());
    dof = std::max(0, dof);
    const QString sketchStatus = solved.status == cad::operations::SolveStatus::Solved
        ? (dof == 0 ? QString("Fully constrained") : QString("Under-constrained"))
        : QString("Conflicting");
    statusBar()->showMessage(QString("Constraints: %1 | Status: %2 | DOF: %3%4")
        .arg(sketch->constraintCount()).arg(sketchStatus).arg(dof)
        .arg(solved.status == cad::operations::SolveStatus::Solved ? QString() : QString(" — %1").arg(QString::fromStdString(solved.error))));
    const QString status = solved.status == cad::operations::SolveStatus::Solved
        ? QString() : QString(" [conflict: %1]").arg(QString::fromStdString(solved.error));
    for (const auto& constraint : sketch->constraints()) {
        SketchConstraintListItem item;
        item.id = std::visit([](const auto& value) { return value.id; }, constraint);
        if (const auto* value = std::get_if<cad::parametric::HorizontalConstraint>(&constraint)) {
            item.label = QString("H  %1").arg(QString::fromStdString(value->entityId));
        } else if (const auto* value = std::get_if<cad::parametric::VerticalConstraint>(&constraint)) {
            item.label = QString("V  %1").arg(QString::fromStdString(value->entityId));
        } else if (const auto* value = std::get_if<cad::parametric::CoincidentConstraint>(&constraint)) {
            item.label = QString("Coincident  %1.%2 ↔ %3.%4")
                .arg(QString::fromStdString(value->a.entityId),
                     QString::fromStdString(pointRoleText(value->a.role)),
                     QString::fromStdString(value->b.entityId),
                     QString::fromStdString(pointRoleText(value->b.role)));
        } else if (const auto* value = std::get_if<cad::parametric::DistanceConstraint>(&constraint)) {
            item.label = QString("Distance  %1 = %2")
                .arg(QString::fromStdString(value->entityId)).arg(value->value, 0, 'f', 2);
            item.editable = true;
        } else if (const auto* value = std::get_if<cad::parametric::RadiusConstraint>(&constraint)) {
            item.label = QString("Radius  %1 = %2")
                .arg(QString::fromStdString(value->entityId)).arg(value->value, 0, 'f', 2);
            item.editable = true;
        } else if (const auto* value = std::get_if<cad::parametric::HorizontalDistanceConstraint>(&constraint)) {
            item.label = QString("X Distance  %1 → %2 = %3")
                .arg(QString::fromStdString(value->first.entityId),
                     QString::fromStdString(value->second.entityId)).arg(value->value, 0, 'f', 2);
            item.editable = true;
        } else if (const auto* value = std::get_if<cad::parametric::VerticalDistanceConstraint>(&constraint)) {
            item.label = QString("Y Distance  %1 → %2 = %3")
                .arg(QString::fromStdString(value->first.entityId),
                     QString::fromStdString(value->second.entityId)).arg(value->value, 0, 'f', 2);
            item.editable = true;
        } else if (const auto* value = std::get_if<cad::parametric::AngleConstraint>(&constraint)) {
            item.label = QString("Angle  %1 = %2°")
                .arg(QString::fromStdString(value->entityId))
                .arg(value->radians * 180.0 / 3.14159265358979323846, 0, 'f', 2);
            item.editable = true;
        } else if (const auto* value = std::get_if<cad::parametric::ParallelConstraint>(&constraint)) {
            item.label = QString("Parallel  %1 ∥ %2")
                .arg(QString::fromStdString(value->firstLineId), QString::fromStdString(value->secondLineId));
        } else if (const auto* value = std::get_if<cad::parametric::PerpendicularConstraint>(&constraint)) {
            item.label = QString("Perpendicular  %1 ⟂ %2")
                .arg(QString::fromStdString(value->firstLineId), QString::fromStdString(value->secondLineId));
        } else if (const auto* value = std::get_if<cad::parametric::AngleBetweenLinesConstraint>(&constraint)) {
            item.label = QString("Angle  %1 → %2 = %3°")
                .arg(QString::fromStdString(value->referenceLineId),
                     QString::fromStdString(value->dependentLineId))
                .arg(value->angleRadians * 180.0 / 3.14159265358979323846, 0, 'f', 2);
            item.editable = true;
        } else if (const auto* value = std::get_if<cad::parametric::TangentConstraint>(&constraint)) {
            item.label = QString("Tangent  %1 ↔ %2").arg(QString::fromStdString(value->firstEntityId), QString::fromStdString(value->secondEntityId));
        } else if (const auto* value = std::get_if<cad::parametric::EqualConstraint>(&constraint)) {
            item.label = QString("Equal  %1 = %2").arg(QString::fromStdString(value->referenceEntityId), QString::fromStdString(value->dependentEntityId));
        }
        item.label += status;
        items.push_back(std::move(item));
    }
    if (!selectedConstraintId_.isEmpty() && std::none_of(items.begin(), items.end(),
        [this](const auto& item) { return QString::fromStdString(item.id) == selectedConstraintId_; }))
        selectedConstraintId_.clear();
    featureEditorPanel_->setSketchConstraints(std::move(items), !activeSketchId_.empty());
    if (!selectedConstraintId_.isEmpty()) {
        featureEditorPanel_->setSketchConstraintSelected(selectedConstraintId_);
        viewer_->setSketchConstraintHighlight(*sketch, selectedConstraintId_.toStdString());
    } else {
        viewer_->clearSketchConstraintHighlight();
    }
}

void MainWindow::selectSketchConstraint(const QString& constraintId)
{
    selectedConstraintId_ = constraintId;
    featureEditorPanel_->setSketchConstraintSelected(constraintId);
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    if (sketch) {
        viewer_->setSketchConstraintMarkers(*sketch, constraintId.toStdString());
        viewer_->setSketchConstraintHighlight(*sketch, constraintId.toStdString());
    }
}

void MainWindow::editSketchConstraint(const QString& constraintId)
{
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    if (!sketch) return;
    for (const auto& constraint : sketch->constraints()) {
        if (std::visit([&](const auto& value) { return value.id == constraintId.toStdString(); }, constraint)) {
            const auto* distance = std::get_if<cad::parametric::DistanceConstraint>(&constraint);
            const auto* radius = std::get_if<cad::parametric::RadiusConstraint>(&constraint);
            const auto* horizontalDistance = std::get_if<cad::parametric::HorizontalDistanceConstraint>(&constraint);
            const auto* verticalDistance = std::get_if<cad::parametric::VerticalDistanceConstraint>(&constraint);
            const auto* angle = std::get_if<cad::parametric::AngleConstraint>(&constraint);
            const auto* angleBetween = std::get_if<cad::parametric::AngleBetweenLinesConstraint>(&constraint);
            if (!distance && !radius && !horizontalDistance && !verticalDistance && !angle && !angleBetween) return;
            bool ok = false;
            const double current = distance ? distance->value : radius ? radius->value
                : horizontalDistance ? horizontalDistance->value : verticalDistance ? verticalDistance->value
                : (angle ? angle->radians : angleBetween->angleRadians) * 180.0 / 3.14159265358979323846;
            const double value = QInputDialog::getDouble(this,
                distance || horizontalDistance || verticalDistance ? "Distance constraint"
                    : angle || angleBetween ? "Angle constraint" : "Radius constraint",
                "Value:", current,
                angle || angleBetween ? -360.0 : horizontalDistance || verticalDistance ? -1.0e6 : 0.001,
                1.0e6, 3, &ok);
            if (!ok) return;
            cad::application::ModelingResult result;
            if (distance) result = modeling_.updateSketchDistance(activeSketchId_, constraintId.toStdString(), value);
            else if (radius) result = modeling_.updateSketchRadius(activeSketchId_, constraintId.toStdString(), value);
            else if (horizontalDistance) result = modeling_.updateSketchHorizontalDistance(activeSketchId_, constraintId.toStdString(), value);
            else if (verticalDistance) result = modeling_.updateSketchVerticalDistance(activeSketchId_, constraintId.toStdString(), value);
            else if (angle) result = modeling_.updateSketchAngle(activeSketchId_, constraintId.toStdString(), value * 3.14159265358979323846 / 180.0);
            else result = modeling_.updateSketchAngleBetweenLines(activeSketchId_, constraintId.toStdString(), value * 3.14159265358979323846 / 180.0);
            if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
            else { selectedConstraintId_ = constraintId; refreshModelView(false); }
            return;
        }
    }
}

void MainWindow::deleteSketchConstraint(const QString& constraintId)
{
    if (activeSketchId_.empty()) return;
    const auto result = modeling_.removeSketchConstraint(
        activeSketchId_, constraintId.toStdString());
    if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
    else {
        selectedConstraintId_.clear();
        refreshModelView(false);
    }
}

void MainWindow::applySelection(
    const QStringList& featureIds,
    const bool updateViewer
)
{
    cancelRevolveAxisPick();
    viewer_->cancelActiveOperation();
    operationSession_.cancel();
    // This is the single MainWindow projection point for both viewer -> tree
    // and tree -> viewer selection paths. Keep the viewer update optional to
    // preserve feedback-loop suppression for featureSelectionChanged.
    if (updateViewer) {
        viewer_->selectFeatures(featureIds);
        // Tree selection is always feature/object selection. Do not
        // reinterpret it through a stale viewport Face/Edge mode.
        currentSelection_.items.clear();
        currentSelection_.items.reserve(featureIds.size());
        for (const auto& featureId : featureIds) {
            currentSelection_.items.push_back({
                featureId.toStdString(),
                cad::application::SelectionKind::Object,
                std::nullopt});
        }
    }
    selectedObjectIds_ = featureIds;
    featureEditorPanel_->selectFeatures(featureIds);
    if (bimNavigationPanel_) bimNavigationPanel_->selectFeatures(featureIds);
    updateBimInspector(featureIds);
    updateActionState();
}

void MainWindow::applySelectionSnapshot(
    const cad::application::SelectionSnapshot& selection,
    const bool updateViewer
)
{
    cancelRevolveAxisPick();
    viewer_->cancelActiveOperation();
    operationSession_.cancel();
    currentSelection_ = selection;
    const auto featureIds = selection.featureIds();
    QStringList ids;
    for (const auto& id : featureIds) ids.append(QString::fromStdString(id));

    if (updateViewer) {
        viewer_->selectFeatures(ids);
        currentSelection_ = viewer_->selectionSnapshot();
    }
    selectedObjectIds_ = ids;
    featureEditorPanel_->selectFeatures(ids);
    if (bimNavigationPanel_) bimNavigationPanel_->selectFeatures(ids);
    updateBimInspector(ids);
    updateActionState();
}

std::vector<std::string> MainWindow::selectedIds() const
{
    return currentSelection_.selectedObjectIds();
}

void MainWindow::updateActionState()
{
    auto state = modeling_.actionState(currentSelection_);
    if (!activeSketchId_.empty()) {
        state.canExtrude = false;
        state.canRevolve = false;
    }
    deleteAction_->setEnabled(state.canDelete);
    if (duplicateAction_) duplicateAction_->setEnabled(selectedIds().size() == 1);
    faceAction_->setEnabled(state.canCreateFace);
    extrudeAction_->setEnabled(state.canExtrude);
    if (pocketAction_) pocketAction_->setEnabled(state.canPocket);
    if (editSketchAction_) editSketchAction_->setEnabled(
        state.canEditSketch && activeSketchId_.empty());
    if (sketchOnFaceAction_) sketchOnFaceAction_->setEnabled(state.canSketchOnFace);
    if (linearPatternAction_) linearPatternAction_->setEnabled(selectedIds().size() == 1);
    if (pathPatternAction_) pathPatternAction_->setEnabled(selectedIds().size() == 2);
    if (filletAction_) filletAction_->setEnabled(state.canFillet);
    if (chamferAction_) chamferAction_->setEnabled(state.canChamfer);
    if (shellAction_) shellAction_->setEnabled(state.canShell);
    if (revolveAction_) revolveAction_->setEnabled(state.canRevolve);
}

void MainWindow::reportResult(const cad::application::ModelingResult& result)
{
    if (!result.success) {
        QMessageBox::warning(this, "Modeling operation failed",
            QString::fromStdString(result.error));
        return;
    }
    refreshModelView();
    applySelection({QString::fromStdString(result.id)});
}

void MainWindow::createActions()
{
    auto* fileMenu = menuBar()->addMenu("&File");
    auto* newAction = fileMenu->addAction("&New");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newDocument);
    auto* openAction = fileMenu->addAction("&Open...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openDocument);
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    importIfcAction_ = fileMenu->addAction("Import IFC...");
    connect(importIfcAction_, &QAction::triggered, this, &MainWindow::importIfc);
#endif
    auto* saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this]() { saveDocument(); });
    auto* saveAsAction = fileMenu->addAction("Save &As...");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this]() { saveDocumentAs(); });

    auto* editMenu = menuBar()->addMenu("&Edit");
    connect(editMenu, &QMenu::aboutToShow, this, [this]() { featureEditorPanel_->commitPendingEdits(); });
    auto* undoAction = modeling_.undoStack().createUndoAction(this, "Undo");
    auto undoKeys = QKeySequence::keyBindings(QKeySequence::Undo);
    if (!undoKeys.contains(QKeySequence(Qt::CTRL | Qt::Key_Z))) undoKeys.append(QKeySequence(Qt::CTRL | Qt::Key_Z));
    undoAction->setShortcuts(undoKeys);
    editMenu->addAction(undoAction);
    auto* redoAction = modeling_.undoStack().createRedoAction(this, "Redo");
    auto redoKeys = QKeySequence::keyBindings(QKeySequence::Redo);
    if (!redoKeys.contains(QKeySequence(Qt::CTRL | Qt::Key_Y))) redoKeys.append(QKeySequence(Qt::CTRL | Qt::Key_Y));
    redoAction->setShortcuts(redoKeys);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    deleteAction_ = editMenu->addAction("Delete");
    deleteAction_->setShortcut(QKeySequence(Qt::Key_Delete));
    deleteAction_->setEnabled(false);
    connect(deleteAction_, &QAction::triggered, this, &MainWindow::deleteFeature);
    duplicateAction_ = editMenu->addAction("Duplicate");
    duplicateAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    duplicateAction_->setEnabled(false);
    connect(duplicateAction_, &QAction::triggered, this, [this]() {
        const auto ids = selectedIds();
        if (ids.size() != 1) return;
        reportResult(modeling_.duplicateFeature(
            ids.front()));
    });

    auto* modelingMenu = menuBar()->addMenu("&Modeling");
    auto* viewMenu = menuBar()->addMenu("&View");

    auto* spatialHiddenAction = viewMenu->addAction("Activate Spatial Visibility");
    connect(spatialHiddenAction, &QAction::triggered, this,
        [this]() { activateSpatialVisibility(cad::application::VisibilityMode::Hidden); });
    auto* spatialGhostedAction = viewMenu->addAction("Spatial Context (Ghost Outside)");
    connect(spatialGhostedAction, &QAction::triggered, this,
        [this]() { activateSpatialVisibility(cad::application::VisibilityMode::Ghosted); });
    auto* clearSpatialAction = viewMenu->addAction("Clear Spatial Visibility");
    connect(clearSpatialAction, &QAction::triggered, this, &MainWindow::clearSpatialVisibility);
    viewMenu->addSeparator();
    auto* sectionXAction = viewMenu->addAction("Section X");
    connect(sectionXAction, &QAction::triggered, this,
        [this]() { viewer_->activateSection(CadViewer::SectionAxis::X); });
    auto* sectionYAction = viewMenu->addAction("Section Y");
    connect(sectionYAction, &QAction::triggered, this,
        [this]() { viewer_->activateSection(CadViewer::SectionAxis::Y); });
    auto* sectionZAction = viewMenu->addAction("Section Z");
    connect(sectionZAction, &QAction::triggered, this,
        [this]() { viewer_->activateSection(CadViewer::SectionAxis::Z); });
    auto* flipSectionAction = viewMenu->addAction("Flip Section");
    connect(flipSectionAction, &QAction::triggered, this,
        [this]() { viewer_->flipSection(); });
    auto* clearSectionAction = viewMenu->addAction("Clear Section");
    connect(clearSectionAction, &QAction::triggered, this,
        [this]() { viewer_->clearSection(); });
    viewMenu->addSeparator();
    auto* displayModeMenu = viewMenu->addMenu("Display Mode");
    auto* displayModes = new QActionGroup(this);
    displayModes->setExclusive(true);
    const auto addDisplayMode = [this, displayModeMenu, displayModes](
        const QString& label, const CadViewer::DisplayMode mode) {
        auto* action = displayModeMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(viewer_->displayMode() == mode);
        displayModes->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode]() {
            viewer_->setDisplayMode(mode);
        });
    };
    addDisplayMode("Shaded", CadViewer::DisplayMode::Shaded);
    addDisplayMode("Shaded with Edges", CadViewer::DisplayMode::ShadedWithEdges);
    addDisplayMode("Wireframe", CadViewer::DisplayMode::Wireframe);
    viewMenu->addSeparator();
    auto* saveViewAction = viewMenu->addAction("Save Current View");
    connect(saveViewAction, &QAction::triggered, this, &MainWindow::saveCurrentView);
    auto* restoreViewAction = viewMenu->addAction("Restore View");
    connect(restoreViewAction, &QAction::triggered, this, &MainWindow::restoreSavedView);
    auto* renameViewAction = viewMenu->addAction("Rename View");
    connect(renameViewAction, &QAction::triggered, this, &MainWindow::renameSavedView);
    auto* deleteViewAction = viewMenu->addAction("Delete View");
    connect(deleteViewAction, &QAction::triggered, this, &MainWindow::deleteSavedView);

    auto* toolBar = addToolBar("Modeling");
    toolBar->addAction(deleteAction_);

    auto* boxAction = new QAction("Box", this);
    connect(boxAction, &QAction::triggered, this, &MainWindow::createBox);
    modelingMenu->addAction(boxAction);
    toolBar->addAction(boxAction);

    auto* cylinderAction = new QAction("Cylinder", this);
    connect(cylinderAction, &QAction::triggered, this, &MainWindow::createCylinder);
    modelingMenu->addAction(cylinderAction);
    toolBar->addAction(cylinderAction);

    modelingMenu->addSeparator();
    auto* sketchAction = modelingMenu->addAction("Add Rectangle Sketch");
    connect(sketchAction, &QAction::triggered, this, &MainWindow::createRectangleSketch);
    sketchOnFaceAction_ = modelingMenu->addAction("Sketch on Face");
    sketchOnFaceAction_->setEnabled(false);
    connect(sketchOnFaceAction_, &QAction::triggered, this, &MainWindow::createSketchOnFace);
    editSketchAction_ = modelingMenu->addAction("Edit Sketch");
    editSketchAction_->setEnabled(false);
    connect(editSketchAction_, &QAction::triggered, this, &MainWindow::editSelectedSketch);
    sketchLineAction_ = modelingMenu->addAction("Sketch Line");
    sketchLineAction_->setEnabled(false);
    connect(sketchLineAction_, &QAction::triggered, this, &MainWindow::selectSketchLineTool);
    sketchCircleAction_ = modelingMenu->addAction("Sketch Circle");
    sketchCircleAction_->setEnabled(false);
    connect(sketchCircleAction_, &QAction::triggered, this, &MainWindow::selectSketchCircleTool);
    sketchArcAction_ = modelingMenu->addAction("Sketch Arc");
    sketchArcAction_->setEnabled(false);
    connect(sketchArcAction_, &QAction::triggered, this, &MainWindow::selectSketchArcTool);
    sketchRectangleAction_ = modelingMenu->addAction("Sketch Rectangle");
    sketchRectangleAction_->setEnabled(false);
    connect(sketchRectangleAction_, &QAction::triggered, this, &MainWindow::selectSketchRectangleTool);
    sketchTrimAction_ = modelingMenu->addAction("Trim");
    sketchTrimAction_->setEnabled(false);
    connect(sketchTrimAction_, &QAction::triggered, this, &MainWindow::selectSketchTrimTool);
    sketchExtendAction_ = modelingMenu->addAction("Extend");
    sketchExtendAction_->setEnabled(false);
    connect(sketchExtendAction_, &QAction::triggered, this, &MainWindow::selectSketchExtendTool);
    sketchCoincidentAction_ = modelingMenu->addAction("Coincident");
    sketchCoincidentAction_->setEnabled(false);
    connect(sketchCoincidentAction_, &QAction::triggered, this, &MainWindow::selectSketchCoincidentTool);
    sketchHorizontalAction_ = modelingMenu->addAction("Horizontal");
    sketchHorizontalAction_->setEnabled(false);
    connect(sketchHorizontalAction_, &QAction::triggered, this, &MainWindow::selectSketchHorizontalTool);
    sketchVerticalAction_ = modelingMenu->addAction("Vertical");
    sketchVerticalAction_->setEnabled(false);
    connect(sketchVerticalAction_, &QAction::triggered, this, &MainWindow::selectSketchVerticalTool);
    sketchDistanceAction_ = modelingMenu->addAction("Distance");
    sketchDistanceAction_->setEnabled(false);
    connect(sketchDistanceAction_, &QAction::triggered, this, &MainWindow::selectSketchDistanceTool);
    sketchRadiusAction_ = modelingMenu->addAction("Radius");
    sketchRadiusAction_->setEnabled(false);
    connect(sketchRadiusAction_, &QAction::triggered, this, &MainWindow::selectSketchRadiusTool);
    sketchHorizontalDistanceAction_ = modelingMenu->addAction("Horizontal Distance");
    sketchHorizontalDistanceAction_->setEnabled(false);
    connect(sketchHorizontalDistanceAction_, &QAction::triggered, this, &MainWindow::selectSketchHorizontalDistanceTool);
    sketchVerticalDistanceAction_ = modelingMenu->addAction("Vertical Distance");
    sketchVerticalDistanceAction_->setEnabled(false);
    connect(sketchVerticalDistanceAction_, &QAction::triggered, this, &MainWindow::selectSketchVerticalDistanceTool);
    sketchAngleAction_ = modelingMenu->addAction("Angle");
    sketchAngleAction_->setEnabled(false);
    connect(sketchAngleAction_, &QAction::triggered, this, &MainWindow::selectSketchAngleTool);
    sketchParallelAction_ = modelingMenu->addAction("Parallel");
    sketchParallelAction_->setEnabled(false);
    connect(sketchParallelAction_, &QAction::triggered, this, &MainWindow::selectSketchParallelTool);
    sketchPerpendicularAction_ = modelingMenu->addAction("Perpendicular");
    sketchPerpendicularAction_->setEnabled(false);
    connect(sketchPerpendicularAction_, &QAction::triggered, this, &MainWindow::selectSketchPerpendicularTool);
    sketchAngleBetweenLinesAction_ = modelingMenu->addAction("Angle Between Lines");
    sketchAngleBetweenLinesAction_->setEnabled(false);
    connect(sketchAngleBetweenLinesAction_, &QAction::triggered, this, &MainWindow::selectSketchAngleBetweenLinesTool);
    sketchTangentAction_ = modelingMenu->addAction("Tangent");
    sketchTangentAction_->setEnabled(false);
    connect(sketchTangentAction_, &QAction::triggered, this, &MainWindow::selectSketchTangentTool);
    sketchEqualAction_ = modelingMenu->addAction("Equal");
    sketchEqualAction_->setEnabled(false);
    connect(sketchEqualAction_, &QAction::triggered, this, &MainWindow::selectSketchEqualTool);
    finishSketchAction_ = modelingMenu->addAction("Finish Sketch");
    finishSketchAction_->setEnabled(false);
    connect(finishSketchAction_, &QAction::triggered, this, &MainWindow::finishSketch);
    faceAction_ = modelingMenu->addAction("Create Face");
    faceAction_->setEnabled(false);
    connect(faceAction_, &QAction::triggered, this, &MainWindow::createFace);
    extrudeAction_ = modelingMenu->addAction("Extrude");
    extrudeAction_->setEnabled(false);
    connect(extrudeAction_, &QAction::triggered, this, &MainWindow::createExtrude);
    pocketAction_ = modelingMenu->addAction("Pocket");
    pocketAction_->setEnabled(false);
    connect(pocketAction_, &QAction::triggered, this, &MainWindow::createPocket);
    revolveAction_ = modelingMenu->addAction("Revolve");
    revolveAction_->setEnabled(false);
    revolveAction_->setToolTip("Revolve a Sketch profile");
    connect(revolveAction_, &QAction::triggered, this, &MainWindow::createRevolve);
    toolBar->addAction(revolveAction_);
    linearPatternAction_ = modelingMenu->addAction("Linear Pattern");
    linearPatternAction_->setEnabled(false);
    linearPatternAction_->setToolTip("Create a linear pattern from the selected object");
    connect(linearPatternAction_, &QAction::triggered, this, &MainWindow::createLinearPattern);
    toolBar->addAction(linearPatternAction_);
    pathPatternAction_ = modelingMenu->addAction("Path Pattern");
    pathPatternAction_->setEnabled(false);
    pathPatternAction_->setToolTip("Create a pattern along the selected path");
    connect(pathPatternAction_, &QAction::triggered, this, &MainWindow::createPathPattern);
    toolBar->addAction(pathPatternAction_);
    filletAction_ = modelingMenu->addAction("Fillet");
    filletAction_->setEnabled(false);
    filletAction_->setToolTip("Fillet selected edges");
    connect(filletAction_, &QAction::triggered, this, &MainWindow::createFillet);
    toolBar->addAction(filletAction_);
    chamferAction_ = modelingMenu->addAction("Chamfer");
    chamferAction_->setEnabled(false);
    chamferAction_->setToolTip("Chamfer selected edges");
    connect(chamferAction_, &QAction::triggered, this, &MainWindow::createChamfer);
    toolBar->addAction(chamferAction_);
    shellAction_ = modelingMenu->addAction("Shell");
    shellAction_->setEnabled(false);
    shellAction_->setToolTip("Shell selected solid and opening Faces");
    connect(shellAction_, &QAction::triggered, this, &MainWindow::createShell);
    toolBar->addAction(shellAction_);
    modelingMenu->addSeparator();

    auto* clearAction = new QAction("Clear", this);
    connect(clearAction, &QAction::triggered, this, &MainWindow::clearDocument);
    modelingMenu->addAction(clearAction);

    auto* fitAllAction = new QAction("Fit All", this);
    connect(fitAllAction, &QAction::triggered, viewer_, &CadViewer::fitAll);
    viewMenu->addAction(fitAllAction);
    toolBar->addAction(fitAllAction);
}

void MainWindow::createBox()
{
    reportResult(modeling_.createBox());
}

void MainWindow::createCylinder()
{
    reportResult(modeling_.createCylinder());
}

bool MainWindow::beginOperation(
    const cad::application::InteractiveOperationKind kind,
    const std::vector<std::string>& sourceFeatureIds)
{
    cancelRevolveAxisPick();
    viewer_->cancelActiveOperation();
    operationSession_.cancel();
    return operationSession_.beginCreate(kind, sourceFeatureIds);
}

void MainWindow::cancelOperation()
{
    cancelRevolveAxisPick();
    operationSession_.cancel();
}

bool MainWindow::commitOperation()
{
    return operationSession_.commit().has_value();
}

void MainWindow::createRectangleSketch()
{
    const auto result = modeling_.createSketch();
    reportResult(result);
    if (result.success) enterSketchEditing(result.id);
}

void MainWindow::createSketchOnFace()
{
    const auto result = modeling_.createSketchOnFace(currentSelection_);
    reportResult(result);
    if (result.success) enterSketchEditing(result.id);
}

void MainWindow::editSelectedSketch()
{
    if (!modeling_.canEditSketch(currentSelection_)) return;
    editSketchById(QString::fromStdString(currentSelection_.items.front().featureId));
}

void MainWindow::editSketchById(const QString& sketchId)
{
    if (sketchId.isEmpty() || !activeSketchId_.empty()) return;
    const cad::application::SelectionSnapshot selection{{
        {sketchId.toStdString(), cad::application::SelectionKind::Object, std::nullopt}}};
    if (!modeling_.canEditSketch(selection)) {
        statusBar()->showMessage("Selected feature is not a Sketch", 3000);
        return;
    }
    applySelection({sketchId});
    enterSketchEditing(sketchId.toStdString());
}

void MainWindow::enterSketchEditing(const std::string& sketchId)
{
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(sketchId));
    if (!sketch) return;
    try {
        const auto frame = sketch->currentFrame();
        activeSketchId_ = sketchId;
        sketchTool_ = SketchTool::Line;
        sketchFirstPoint_.reset();
        sketchSecondPoint_.reset();
        constraintFirstPoint_.reset();
        selectedConstraintId_.clear();
        viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Line);
        viewer_->enterSketchMode(frame.origin, frame.xDirection,
            frame.yDirection, frame.normal);
        viewer_->setSketchConstraintMarkers(*sketch);
        sketchLineAction_->setEnabled(true);
        sketchCircleAction_->setEnabled(true);
        sketchArcAction_->setEnabled(true);
        sketchRectangleAction_->setEnabled(true);
        sketchTrimAction_->setEnabled(true);
        sketchExtendAction_->setEnabled(true);
        sketchCoincidentAction_->setEnabled(true);
        sketchHorizontalAction_->setEnabled(true);
        sketchVerticalAction_->setEnabled(true);
        sketchDistanceAction_->setEnabled(true);
        sketchRadiusAction_->setEnabled(true);
        sketchHorizontalDistanceAction_->setEnabled(true);
        sketchVerticalDistanceAction_->setEnabled(true);
        sketchAngleAction_->setEnabled(true);
        sketchParallelAction_->setEnabled(true);
        sketchPerpendicularAction_->setEnabled(true);
        sketchAngleBetweenLinesAction_->setEnabled(true);
        sketchTangentAction_->setEnabled(true);
        sketchEqualAction_->setEnabled(true);
        finishSketchAction_->setEnabled(true);
        refreshConstraintManager();
        updateActionState();
        statusBar()->showMessage("Sketch mode: select a drawing tool");
    } catch (const std::exception& error) {
        QMessageBox::warning(this, "Sketch editing failed", error.what());
    }
}

void MainWindow::finishSketch()
{
    const QString finishedSketchId = QString::fromStdString(activeSketchId_);
    viewer_->exitSketchMode();
    activeSketchId_.clear();
    sketchTool_ = SketchTool::None;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    constraintFirstPoint_.reset();
    selectedConstraintId_.clear();
    sketchLineAction_->setEnabled(false);
    sketchCircleAction_->setEnabled(false);
    sketchArcAction_->setEnabled(false);
    sketchRectangleAction_->setEnabled(false);
    sketchTrimAction_->setEnabled(false);
    sketchExtendAction_->setEnabled(false);
    sketchCoincidentAction_->setEnabled(false);
    sketchHorizontalAction_->setEnabled(false);
    sketchVerticalAction_->setEnabled(false);
    sketchDistanceAction_->setEnabled(false);
    sketchRadiusAction_->setEnabled(false);
    sketchHorizontalDistanceAction_->setEnabled(false);
    sketchVerticalDistanceAction_->setEnabled(false);
    sketchAngleAction_->setEnabled(false);
    sketchParallelAction_->setEnabled(false);
    sketchPerpendicularAction_->setEnabled(false);
    sketchAngleBetweenLinesAction_->setEnabled(false);
    sketchTangentAction_->setEnabled(false);
    sketchEqualAction_->setEnabled(false);
    finishSketchAction_->setEnabled(false);
    updateActionState();
    statusBar()->showMessage("Ready");
    refreshModelView(false);
    if (!finishedSketchId.isEmpty()) {
        // exitSketchMode() restores the previous viewport selection mode and
        // clears AIS selection. Re-project the finished Sketch as an object
        // selection so tree selection and feature actions do not depend on
        // the stale pre-edit Face selection.
        applySelection({finishedSketchId});
    }
}

void MainWindow::selectSketchLineTool()
{
    sketchTool_ = SketchTool::Line;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Line);
}

void MainWindow::selectSketchCircleTool()
{
    sketchTool_ = SketchTool::Circle;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Circle);
}

void MainWindow::selectSketchArcTool()
{
    sketchTool_ = SketchTool::Arc;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    refreshConstraintManager();
    statusBar()->showMessage("Arc: select center, start, then end");
}

void MainWindow::selectSketchRectangleTool()
{
    sketchTool_ = SketchTool::Rectangle;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Rectangle);
}

void MainWindow::selectSketchTrimTool()
{
    sketchTool_ = SketchTool::Trim;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Trim);
    statusBar()->showMessage("Trim: click a Line, Arc, or Circle segment");
}

void MainWindow::selectSketchExtendTool()
{
    sketchTool_ = SketchTool::Extend;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Extend);
    statusBar()->showMessage("Extend: hover and click a Line or Arc endpoint");
}

void MainWindow::selectSketchCoincidentTool()
{
    sketchTool_ = SketchTool::Coincident;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Coincident: click two Line or Arc endpoints");
}

void MainWindow::selectSketchHorizontalTool()
{
    sketchTool_ = SketchTool::Horizontal;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Horizontal: click a Line");
}

void MainWindow::selectSketchVerticalTool()
{
    sketchTool_ = SketchTool::Vertical;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Vertical: click a Line");
}

void MainWindow::selectSketchDistanceTool()
{
    sketchTool_ = SketchTool::Distance;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Distance: click a Line");
}

void MainWindow::selectSketchRadiusTool()
{
    sketchTool_ = SketchTool::Radius;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Radius: click a Circle or Arc");
}

void MainWindow::selectSketchHorizontalDistanceTool()
{
    sketchTool_ = SketchTool::HorizontalDistance;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Horizontal Distance: click two points");
}

void MainWindow::selectSketchVerticalDistanceTool()
{
    sketchTool_ = SketchTool::VerticalDistance;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Vertical Distance: click two points");
}

void MainWindow::selectSketchAngleTool()
{
    sketchTool_ = SketchTool::Angle;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Angle: click a Line");
}

void MainWindow::selectSketchParallelTool()
{
    sketchTool_ = SketchTool::Parallel;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Parallel: click two Lines");
}

void MainWindow::selectSketchPerpendicularTool()
{
    sketchTool_ = SketchTool::Perpendicular;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Perpendicular: click two Lines");
}

void MainWindow::selectSketchAngleBetweenLinesTool()
{
    sketchTool_ = SketchTool::Tangent;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Angle Between Lines: click reference and dependent Lines");
}

void MainWindow::selectSketchTangentTool()
{
    sketchTool_ = SketchTool::Equal;
    constraintFirstPoint_.reset();
    statusBar()->showMessage("Tangent: click Line, then Circle or Arc");
}

void MainWindow::selectSketchEqualTool()
{
    sketchTool_ = SketchTool::AngleBetweenLines;
    constraintFirstPoint_.reset();
    statusBar()->showMessage("Equal: click two compatible entities");
}

void MainWindow::handleSketchPoint(const gp_Pnt2d& point, const double hitTolerance)
{
    if (activeSketchId_.empty() || sketchTool_ == SketchTool::None) return;
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    if (!sketch) return;
    if (sketchTool_ == SketchTool::Horizontal || sketchTool_ == SketchTool::Vertical) {
        const auto lineId = cad::operations::SketchConstraintSolver::lineAt(
            sketch->entities(), point, hitTolerance);
        if (!lineId) { statusBar()->showMessage("Select a Line", 2000); return; }
        const auto result = sketchTool_ == SketchTool::Horizontal
            ? modeling_.addSketchHorizontal(activeSketchId_, *lineId)
            : modeling_.addSketchVertical(activeSketchId_, *lineId);
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::Coincident) {
        const auto pointRef = cad::operations::SketchConstraintSolver::pointAt(
            sketch->entities(), point, hitTolerance);
        if (!pointRef) { statusBar()->showMessage("Select a sketch endpoint", 2000); return; }
        if (!constraintFirstPoint_) {
            constraintFirstPoint_ = pointRef;
            statusBar()->showMessage("Coincident: select second endpoint");
            return;
        }
        const auto result = modeling_.addSketchCoincident(
            activeSketchId_, *constraintFirstPoint_, *pointRef);
        constraintFirstPoint_.reset();
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::HorizontalDistance
        || sketchTool_ == SketchTool::VerticalDistance) {
        const auto pointRef = cad::operations::SketchConstraintSolver::pointAt(
            sketch->entities(), point, hitTolerance);
        if (!pointRef) { statusBar()->showMessage("Select a sketch point", 2000); return; }
        if (!constraintFirstPoint_) {
            constraintFirstPoint_ = pointRef;
            statusBar()->showMessage("Select second point");
            return;
        }
        bool ok = false;
        const double value = QInputDialog::getDouble(this,
            sketchTool_ == SketchTool::HorizontalDistance ? "Horizontal distance" : "Vertical distance",
            "Value:", 10.0, -1.0e6, 1.0e6, 3, &ok);
        if (!ok) return;
        const auto result = sketchTool_ == SketchTool::HorizontalDistance
            ? modeling_.addSketchHorizontalDistance(activeSketchId_, *constraintFirstPoint_, *pointRef, value)
            : modeling_.addSketchVerticalDistance(activeSketchId_, *constraintFirstPoint_, *pointRef, value);
        constraintFirstPoint_.reset();
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::Angle) {
        const auto lineId = cad::operations::SketchConstraintSolver::lineAt(
            sketch->entities(), point, hitTolerance);
        if (!lineId) { statusBar()->showMessage("Select a Line", 2000); return; }
        bool ok = false;
        const double degrees = QInputDialog::getDouble(this, "Angle constraint",
            "Degrees:", 45.0, -360.0, 360.0, 3, &ok);
        if (!ok) return;
        const auto result = modeling_.addSketchAngle(activeSketchId_, *lineId,
            degrees * 3.14159265358979323846 / 180.0);
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::Parallel || sketchTool_ == SketchTool::Perpendicular) {
        const auto lineId = cad::operations::SketchConstraintSolver::lineAt(
            sketch->entities(), point, hitTolerance);
        if (!lineId) { statusBar()->showMessage("Select a Line", 2000); return; }
        if (!constraintFirstPoint_) {
            constraintFirstPoint_ = cad::parametric::SketchPointRef{
                *lineId, cad::parametric::SketchPointRole::LineStart};
            statusBar()->showMessage("Select second Line");
            return;
        }
        const auto result = sketchTool_ == SketchTool::Parallel
            ? modeling_.addSketchParallel(activeSketchId_, constraintFirstPoint_->entityId, *lineId)
            : modeling_.addSketchPerpendicular(activeSketchId_, constraintFirstPoint_->entityId, *lineId);
        constraintFirstPoint_.reset();
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::AngleBetweenLines) {
        const auto lineId = cad::operations::SketchConstraintSolver::lineAt(
            sketch->entities(), point, hitTolerance);
        if (!lineId) { statusBar()->showMessage("Select a Line", 2000); return; }
        if (!constraintFirstPoint_) {
            constraintFirstPoint_ = cad::parametric::SketchPointRef{
                *lineId, cad::parametric::SketchPointRole::LineStart};
            statusBar()->showMessage("Angle Between Lines: select dependent Line");
            return;
        }
        bool ok = false;
        const double degrees = QInputDialog::getDouble(this, "Angle Between Lines",
            "Degrees:", 30.0, -360.0, 360.0, 3, &ok);
        if (!ok) return;
        const auto result = modeling_.addSketchAngleBetweenLines(activeSketchId_,
            constraintFirstPoint_->entityId, *lineId,
            degrees * 3.14159265358979323846 / 180.0);
        constraintFirstPoint_.reset();
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::Tangent || sketchTool_ == SketchTool::Equal) {
        std::optional<std::string> target;
        if (sketchTool_ == SketchTool::Tangent && !constraintFirstPoint_)
            target = cad::operations::SketchConstraintSolver::lineAt(sketch->entities(), point, hitTolerance);
        else if (sketchTool_ == SketchTool::Tangent)
            target = cad::operations::SketchConstraintSolver::circleOrArcAt(sketch->entities(), point, hitTolerance);
        else {
            target = cad::operations::SketchConstraintSolver::lineAt(sketch->entities(), point, hitTolerance);
            if (!target) target = cad::operations::SketchConstraintSolver::circleOrArcAt(
                sketch->entities(), point, hitTolerance);
        }
        if (!target) { statusBar()->showMessage("Select a compatible sketch entity", 2000); return; }
        if (!constraintFirstPoint_) {
            constraintFirstPoint_ = cad::parametric::SketchPointRef{*target, cad::parametric::SketchPointRole::LineStart};
            statusBar()->showMessage("Select second compatible entity");
            return;
        }
        const auto result = sketchTool_ == SketchTool::Tangent
            ? modeling_.addSketchTangent(activeSketchId_, constraintFirstPoint_->entityId, *target)
            : modeling_.addSketchEqual(activeSketchId_, constraintFirstPoint_->entityId, *target);
        constraintFirstPoint_.reset();
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (sketchTool_ == SketchTool::Distance || sketchTool_ == SketchTool::Radius) {
        const auto target = sketchTool_ == SketchTool::Distance
            ? cad::operations::SketchConstraintSolver::lineAt(sketch->entities(), point, hitTolerance)
            : cad::operations::SketchConstraintSolver::circleOrArcAt(sketch->entities(), point, hitTolerance);
        if (!target) {
            statusBar()->showMessage(sketchTool_ == SketchTool::Distance
                ? "Select a Line" : "Select a Circle or Arc", 2000);
            return;
        }
        bool ok = false;
        const double value = QInputDialog::getDouble(this,
            sketchTool_ == SketchTool::Distance ? "Distance constraint" : "Radius constraint",
            "Value:", 10.0, 0.001, 1.0e6, 3, &ok);
        if (!ok) return;
        std::optional<std::string> existingConstraint;
        for (const auto& constraint : sketch->constraints()) {
            if (sketchTool_ == SketchTool::Distance) {
                if (const auto* item = std::get_if<cad::parametric::DistanceConstraint>(&constraint);
                    item && item->entityId == *target) existingConstraint = item->id;
            } else if (const auto* item = std::get_if<cad::parametric::RadiusConstraint>(&constraint);
                       item && item->entityId == *target) existingConstraint = item->id;
        }
        const auto result = existingConstraint
            ? (sketchTool_ == SketchTool::Distance
                ? modeling_.updateSketchDistance(activeSketchId_, *existingConstraint, value)
                : modeling_.updateSketchRadius(activeSketchId_, *existingConstraint, value))
            : (sketchTool_ == SketchTool::Distance
                ? modeling_.addSketchDistance(activeSketchId_, *target, value)
                : modeling_.addSketchRadius(activeSketchId_, *target, value));
        if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
        else refreshModelView(false);
        return;
    }
    if (!sketchFirstPoint_) {
        sketchFirstPoint_ = point;
        return;
    }
    if (sketchTool_ == SketchTool::Arc && !sketchSecondPoint_) {
        sketchSecondPoint_ = point;
        return;
    }
    const auto first = *sketchFirstPoint_;
    sketchFirstPoint_.reset();
    const auto second = sketchSecondPoint_;
    sketchSecondPoint_.reset();
    cad::application::ModelingResult result;
    if (sketchTool_ == SketchTool::Line) {
        result = modeling_.addSketchLine(activeSketchId_, first, point);
    } else if (sketchTool_ == SketchTool::Circle) {
        result = modeling_.addSketchCircle(activeSketchId_, first, first.Distance(point));
    } else if (sketchTool_ == SketchTool::Arc) {
        result = modeling_.addSketchArc(activeSketchId_, first, *second, point);
    } else {
        result = modeling_.addSketchLine(activeSketchId_, first,
            {point.X(), first.Y()});
        if (result.success) result = modeling_.addSketchLine(activeSketchId_,
            {point.X(), first.Y()}, point);
        if (result.success) result = modeling_.addSketchLine(activeSketchId_,
            point, {first.X(), point.Y()});
        if (result.success) result = modeling_.addSketchLine(activeSketchId_,
            {first.X(), point.Y()}, first);
    }
    if (!result.success) {
        QMessageBox::warning(this, "Sketch entity failed",
            QString::fromStdString(result.error));
    } else {
        refreshModelView(false);
    }
}

void MainWindow::createFace()
{
    reportResult(modeling_.createFace(selectedIds()));
}

void MainWindow::createExtrude()
{
    const auto ids = selectedIds();
    if (ids.size() != 1 || !beginOperation(
            cad::application::InteractiveOperationKind::Extrude, ids)) return;
    if (ids.size() == 1) {
        const auto feature = modeling_.body().findFeature(ids.front());
        if (feature && feature->role() == cad::parametric::FeatureRole::Sketch) {
            bool accepted = false;
            const double distance = QInputDialog::getDouble(
                this, "Extrude", "Distance:", 20.0, 0.001, 1'000'000.0,
                3, &accepted);
            if (!accepted) { cancelOperation(); return; }
            operationSession_.updatePreview("distance", distance);
            const auto reverse = QMessageBox::question(
                this, "Extrude", "Reverse direction?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                == QMessageBox::Yes;
            operationSession_.updatePreview("reverse", reverse ? 1.0 : 0.0);
            const auto result = modeling_.createExtrudeFromSketch(
                currentSelection_, distance, reverse);
            if (!result.success) cancelOperation(); else commitOperation();
            reportResult(result);
            return;
        }
    }
    const auto result = modeling_.createExtrude(ids);
    if (!result.success) cancelOperation(); else commitOperation();
    reportResult(result);
}

void MainWindow::createPocket()
{
    const auto ids = selectedIds();
    if (ids.size() != 1 || !beginOperation(
            cad::application::InteractiveOperationKind::Pocket, ids)) return;
    bool accepted = false;
    const double depth = QInputDialog::getDouble(
        this, "Pocket", "Depth:", 10.0, 0.001, 1'000'000.0,
        3, &accepted);
    if (!accepted) { cancelOperation(); return; }
    operationSession_.updatePreview("depth", depth);
    const auto result = modeling_.createPocketFromSketch(currentSelection_, depth);
    if (!result.success) cancelOperation(); else commitOperation();
    reportResult(result);
}

void MainWindow::createLinearPattern()
{
    reportResult(modeling_.createLinearPattern(selectedIds()));
}

void MainWindow::createPathPattern()
{
    reportResult(modeling_.createPathPattern(selectedIds()));
}

void MainWindow::createFillet()
{
    const auto ids = currentSelection_.featureIds();
    if (ids.empty() || !beginOperation(
            cad::application::InteractiveOperationKind::Fillet, ids)) return;
    bool accepted = false;
    const double radius = QInputDialog::getDouble(
        this, "Fillet", "Radius:", 3.0, 0.001, 1'000'000.0, 3, &accepted);
    if (!accepted) { cancelOperation(); return; }
    operationSession_.updatePreview("radius", radius);
    const auto result = modeling_.createFillet(currentSelection_, radius);
    if (!result.success) cancelOperation(); else commitOperation();
    reportResult(result);
}

void MainWindow::createChamfer()
{
    const auto ids = currentSelection_.featureIds();
    if (ids.empty() || !beginOperation(
            cad::application::InteractiveOperationKind::Chamfer, ids)) return;
    bool accepted = false;
    const double distance = QInputDialog::getDouble(
        this, "Chamfer", "Distance:", 3.0, 0.001, 1'000'000.0, 3, &accepted);
    if (!accepted) { cancelOperation(); return; }
    operationSession_.updatePreview("distance", distance);
    const auto result = modeling_.createChamfer(currentSelection_, distance);
    if (!result.success) cancelOperation(); else commitOperation();
    reportResult(result);
}

void MainWindow::createShell()
{
    const auto ids = currentSelection_.featureIds();
    if (ids.empty() || !beginOperation(
            cad::application::InteractiveOperationKind::Shell, ids)) return;
    bool accepted = false;
    const double thickness = QInputDialog::getDouble(
        this, "Shell", "Thickness:", 1.0, 0.001, 1'000'000.0, 3, &accepted);
    if (!accepted) { cancelOperation(); return; }
    operationSession_.updatePreview("thickness", thickness);
    const auto result = modeling_.createShell(currentSelection_, thickness);
    if (!result.success) cancelOperation(); else commitOperation();
    reportResult(result);
}

void MainWindow::createRevolve()
{
    const auto ids = selectedIds();
    if (ids.size() != 1 || !beginOperation(
            cad::application::InteractiveOperationKind::Revolve, ids)) return;

    bool accepted = false;
    const QString axisName = QInputDialog::getItem(
        this, "Revolve", "Axis:", {"X", "Y", "Z", "Sketch Line", "Model Edge"},
        1, false, &accepted);
    if (!accepted) { cancelOperation(); return; }
    const double angle = QInputDialog::getDouble(
        this, "Revolve", "Angle (degrees):", 360.0, -360.0, 360.0,
        3, &accepted);
    if (!accepted || std::abs(angle) <= 1.0e-9) { cancelOperation(); return; }

    cad::parametric::RevolveAxisDefinition axis;
    if (axisName == "X") axis.type = cad::parametric::RevolveAxisType::GlobalX;
    else if (axisName == "Z") axis.type = cad::parametric::RevolveAxisType::GlobalZ;
    else if (axisName == "Sketch Line") axis.type = cad::parametric::RevolveAxisType::SketchLine;
    else if (axisName == "Model Edge") axis.type = cad::parametric::RevolveAxisType::ModelEdge;
    else axis.type = cad::parametric::RevolveAxisType::GlobalY;
    operationSession_.updatePreview("angleDegrees", angle);
    if (axis.type == cad::parametric::RevolveAxisType::SketchLine
        || axis.type == cad::parametric::RevolveAxisType::ModelEdge) {
        revolveProfileFeatureId_ = QString::fromStdString(ids.front());
        revolvePendingAngleDegrees_ = angle;
        revolvePendingAxisType_ = axis.type;
        beginRevolveAxisPick(revolveProfileFeatureId_,
            axis.type == cad::parametric::RevolveAxisType::SketchLine
                ? "SketchLine" : "ModelEdge");
        return;
    }
    const auto result = modeling_.createRevolve(currentSelection_, axis, angle);
    if (!result.success) cancelOperation(); else commitOperation();
    reportResult(result);
}

void MainWindow::beginRevolveAxisPick(
    const QString& featureId, const QString& axisType, const bool editingExisting)
{
    cad::parametric::RevolveAxisType type;
    if (axisType == "SketchLine") type = cad::parametric::RevolveAxisType::SketchLine;
    else if (axisType == "ModelEdge") type = cad::parametric::RevolveAxisType::ModelEdge;
    else {
        const auto global = axisType == "GlobalX"
            ? cad::parametric::RevolveAxisType::GlobalX
            : axisType == "GlobalZ"
                ? cad::parametric::RevolveAxisType::GlobalZ
                : cad::parametric::RevolveAxisType::GlobalY;
        const auto result = modeling_.setFeatureProperty(
            featureId.toStdString(), "axisType", std::string(
                global == cad::parametric::RevolveAxisType::GlobalX ? "GlobalX"
                    : global == cad::parametric::RevolveAxisType::GlobalZ ? "GlobalZ" : "GlobalY"));
        reportResult(result);
        return;
    }
    if (editingExisting) {
        const auto revolve = std::dynamic_pointer_cast<cad::parametric::RevolveFeature>(
            modeling_.body().findFeature(featureId.toStdString()));
        if (!revolve) return;
        revolveProfileFeatureId_ = QString::fromStdString(revolve->profile()->id());
        if (!operationSession_.active()) {
            operationSession_.beginEdit(
                cad::application::InteractiveOperationKind::FeatureEdit,
                featureId.toStdString(), {});
        }
    }
    revolveAxisPicking_ = true;
    revolveAxisPickEditing_ = editingExisting;
    revolveAxisFeatureId_ = editingExisting ? featureId : QString{};
    revolvePendingAxisType_ = type;
    revolvePreviousSelectionMode_ = static_cast<int>(viewer_->selectionMode());
    viewer_->setAxisPickCancelHandler([this]() { cancelRevolveAxisPick(); });
    viewer_->setSelectionMode(CadViewer::SelectionMode::Edge);
    statusBar()->showMessage(
        type == cad::parametric::RevolveAxisType::SketchLine
            ? "Pick a line from the source Sketch"
            : "Pick a linear model Edge for the Revolve axis");
}

void MainWindow::cancelRevolveAxisPick()
{
    if (!revolveAxisPicking_) return;
    revolveRestoringSelectionMode_ = true;
    viewer_->setAxisPickCancelHandler({});
    viewer_->setSelectionMode(static_cast<CadViewer::SelectionMode>(
        revolvePreviousSelectionMode_));
    revolveAxisPicking_ = false;
    revolveRestoringSelectionMode_ = false;
    statusBar()->showMessage("Revolve axis pick cancelled", 2000);
}

bool MainWindow::handleRevolveAxisSelection(
    const cad::application::SelectionSnapshot& selection)
{
    if (!revolveAxisPicking_) return false;
    if (selection.items.empty()) return true;

    cad::parametric::RevolveAxisDefinition axis;
    const auto resolved = modeling_.resolveRevolveAxis(
        revolveProfileFeatureId_.toStdString(), selection, axis);
    if (!resolved.success) {
        statusBar()->showMessage(QString::fromStdString(resolved.error), 3000);
        return true;
    }

    cad::application::ModelingResult result;
    if (revolveAxisPickEditing_) {
        result = modeling_.updateRevolveAxis(
            revolveAxisFeatureId_.toStdString(), std::move(axis));
        if (result.success) operationSession_.commit();
        else operationSession_.cancel();
    } else {
        const cad::application::SelectionSnapshot profileSelection{{
            {revolveProfileFeatureId_.toStdString(),
                cad::application::SelectionKind::Object, std::nullopt}}};
        result = modeling_.createRevolve(
            profileSelection, std::move(axis), revolvePendingAngleDegrees_);
        if (!result.success) cancelOperation(); else commitOperation();
    }
    reportResult(result);
    return true;
}

void MainWindow::deleteFeature()
{
    featureEditorPanel_->commitPendingEdits();
    const auto ids = selectedIds();
    if (ids.size() != 1) return;
    const auto id = ids.front();
    const auto names = modeling_.dependentNames(id);
    if (!names.isEmpty() && QMessageBox::question(this, "Delete Feature",
        QString("Delete %1?\nDependent features will also be deleted:\n%2")
            .arg(QString::fromStdString(id), names.join("\n")),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
        != QMessageBox::Yes) return;
    reportResult(modeling_.deleteFeature(id));
}

void MainWindow::clearDocument()
{
    cancelRevolveAxisPick();
    viewer_->cancelActiveOperation();
    operationSession_.cancel();
    modeling_.clearProject();
    applySelection({});
    featureEditorPanel_->refresh();
    refreshBimNavigation();
}

void MainWindow::updateTitle()
{
    setWindowTitle((currentFile_.isEmpty() ? QStringLiteral("Untitled") : currentFile_)
                   + QStringLiteral("[*] — ParametricCAD"));
    setWindowModified(!modeling_.undoStack().isClean());
}

bool MainWindow::saveTo(const QString& path)
{
    featureEditorPanel_->commitPendingEdits();
    QString error;
    if (!project_.save(path, error)) {
        QMessageBox::critical(this, "Save failed", path + "\n" + error);
        return false;
    }
    currentFile_ = path;
    updateTitle();
    statusBar()->showMessage("Saved: " + path, 3000);
    return true;
}

bool MainWindow::saveDocument()
{
    if (!currentFile_.isEmpty()) return saveTo(currentFile_);
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        QMessageBox::critical(this, "Save failed", "Cannot create application data directory: " + directory);
        return false;
    }
    return saveTo(QDir(directory).filePath("autosave.pcad"));
}

bool MainWindow::saveDocumentAs()
{
    QFileDialog dialog(this, "Save project", currentFile_, "ParametricCAD (*.pcad)");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix("pcad");
    if (dialog.exec() != QDialog::Accepted) return false;
    return saveTo(dialog.selectedFiles().first());
}

void MainWindow::newDocument()
{
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    if (projectLoading_ || ifcImporting_) return;
#else
    if (projectLoading_) return;
#endif
    if (!confirmReplacement()) return;
    cancelRevolveAxisPick();
    viewer_->cancelActiveOperation();
    operationSession_.cancel();
    project_.newProject();
    presenter_->clear();
    applySelection({});
    featureEditorPanel_->setFeatures(modeling_.features());
    featureEditorPanel_->setVisibilityGroups(visibilityManager_.groups());
    featureEditorPanel_->setVisibilityFilters(visibilityManager_.filters());
    featureEditorPanel_->setVisibilityPresets(visibilityManager_.presets());
    refreshBimNavigation();
    featureEditorPanel_->setActionState(modeling_.actionState(selectedIds()));
    currentFile_.clear();
    updateTitle();
}

void MainWindow::openDocument()
{
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    if (projectLoading_ || ifcImporting_) return;
#else
    if (projectLoading_) return;
#endif
    const auto path = QFileDialog::getOpenFileName(this, "Open project", currentFile_, "ParametricCAD (*.pcad)");
    if (path.isEmpty()) return;
    if (!confirmReplacement()) return;
    startProjectLoad(path);
}

#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
void MainWindow::importIfc()
{
    if (projectLoading_ || ifcImporting_) return;
    const auto path = QFileDialog::getOpenFileName(
        this, "Import IFC", currentFile_, "IFC files (*.ifc);;All files (*)");
    if (path.isEmpty()) return;

    ifcImporting_ = true;
    pendingIfcPath_ = path;
    ifcImportProcessed_ = std::make_shared<std::atomic<int>>(0);
    ifcImportTotal_ = std::make_shared<std::atomic<int>>(0);
    ifcImportStage_ = std::make_shared<std::atomic<int>>(
        static_cast<int>(cad::import::IfcImportStage::Opening));
    ifcImportCancelRequested_ = std::make_shared<std::atomic_bool>(false);
    ifcImportDialog_ = new QProgressDialog("Importing IFC...", "Cancel", 0, 0, this);
    ifcImportDialog_->setWindowTitle("Import IFC");
    ifcImportDialog_->setWindowModality(Qt::NonModal);
    ifcImportDialog_->setAutoClose(false);
    ifcImportDialog_->setAutoReset(false);
    ifcImportDialog_->setMinimumDuration(0);
    connect(ifcImportDialog_, &QProgressDialog::canceled, this, [this]() {
        if (ifcImportCancelRequested_) {
            ifcImportCancelRequested_->store(true, std::memory_order_relaxed);
            ifcImportDialog_->setLabelText("Cancelling IFC import...");
            ifcImportDialog_->setCancelButton(nullptr);
        }
    });
    ifcImportDialog_->show();
    ifcImportProgressTimer_.start(100);
    statusBar()->showMessage("Importing IFC...");

    const auto processed = ifcImportProcessed_;
    const auto total = ifcImportTotal_;
    const auto stage = ifcImportStage_;
    const auto cancelled = ifcImportCancelRequested_;
    ifcImportWatcher_.setFuture(QtConcurrent::run(
        [path, processed, total, stage, cancelled]() {
            auto result = std::make_shared<cad::import::IfcImportResult>();
            cad::application::IfcImporter importer;
            *result = importer.prepare(path,
                [processed, total, stage](const cad::import::IfcImportProgress& progress) {
                    processed->store(static_cast<int>(progress.processed), std::memory_order_relaxed);
                    total->store(static_cast<int>(progress.total), std::memory_order_relaxed);
                    stage->store(static_cast<int>(progress.stage), std::memory_order_relaxed);
                },
                [cancelled]() { return cancelled->load(std::memory_order_relaxed); });
            return result;
        }));
}

void MainWindow::updateIfcImportProgress()
{
    if (!ifcImporting_ || !ifcImportDialog_) return;
    const auto stage = static_cast<cad::import::IfcImportStage>(
        ifcImportStage_->load(std::memory_order_relaxed));
    QString label;
    switch (stage) {
    case cad::import::IfcImportStage::Opening: label = "Opening IFC..."; break;
    case cad::import::IfcImportStage::Parsing: label = "Parsing IFC..."; break;
    case cad::import::IfcImportStage::Geometry: label = "Preparing geometry..."; break;
    case cad::import::IfcImportStage::Finalizing: label = "Finalizing import..."; break;
    }
    const int processed = ifcImportProcessed_->load(std::memory_order_relaxed);
    const int total = ifcImportTotal_->load(std::memory_order_relaxed);
    if (total > 0) {
        ifcImportDialog_->setRange(0, total);
        ifcImportDialog_->setValue(std::min(processed, total));
        label += QString(" %1 / %2").arg(processed).arg(total);
    } else if (processed > 0) {
        label += QString(" %1 products").arg(processed);
    }
    ifcImportDialog_->setLabelText(label);
}

void MainWindow::finishIfcImport()
{
    ifcImportProgressTimer_.stop();
    updateIfcImportProgress();
    const auto result = ifcImportWatcher_.result();
    const bool cancelled = result->cancelled
        || (ifcImportCancelRequested_ && ifcImportCancelRequested_->load(std::memory_order_relaxed));
    const QString path = pendingIfcPath_;
    if (ifcImportDialog_) {
        ifcImportDialog_->close();
        ifcImportDialog_->deleteLater();
        ifcImportDialog_ = nullptr;
    }
    ifcImporting_ = false;
    pendingIfcPath_.clear();
    if (cancelled) {
        statusBar()->showMessage("IFC import cancelled", 3000);
        ifcImportProcessed_.reset();
        ifcImportTotal_.reset();
        ifcImportStage_.reset();
        ifcImportCancelRequested_.reset();
        return;
    }
    if (result->fatal || result->products.empty()) {
        abortIfcImport(result->diagnostics.empty()
            ? QStringLiteral("No importable IFC products were found")
            : result->diagnostics.front().message);
        return;
    }

    try {
        cad::application::IfcImporter importer;
        auto features = importer.makeFeatures(*result, path, modeling_.body());
        const auto modelingResult = modeling_.importFeatures(std::move(features));
        if (!modelingResult.success) {
            abortIfcImport(QString::fromStdString(modelingResult.error));
            return;
        }
        // The undo-stack callback performs the single model/presentation refresh.
        viewer_->fitAll();
        const auto& stats = result->statistics;
        const auto seconds = static_cast<double>(stats.totalMilliseconds) / 1000.0;
        statusBar()->showMessage(QString("Imported IFC: %1 objects, %2 skipped, %3 failed (%4 s)")
            .arg(stats.importedCount).arg(stats.skippedCount).arg(stats.failedCount)
            .arg(seconds, 0, 'f', 1), 6000);
        if (stats.skippedCount > 0 || stats.failedCount > 0) {
            QString details = QString("Imported: %1\nSkipped: %2\nFailed: %3")
                .arg(stats.importedCount).arg(stats.skippedCount).arg(stats.failedCount);
            QHash<QString, int> groupedDiagnostics;
            for (const auto& diagnostic : result->diagnostics) {
                const auto key = diagnostic.entityType + ": " + diagnostic.message;
                ++groupedDiagnostics[key];
            }
            int shown = 0;
            for (auto iterator = groupedDiagnostics.cbegin();
                 iterator != groupedDiagnostics.cend() && shown < 20; ++iterator, ++shown) {
                details += "\n" + iterator.key();
                if (iterator.value() > 1) details += QString(" (%1)").arg(iterator.value());
            }
            if (groupedDiagnostics.size() > shown)
                details += QString("\n... and %1 more groups")
                    .arg(groupedDiagnostics.size() - shown);
            QMessageBox::warning(this, "IFC import completed with warnings", details);
        }
    } catch (const std::exception& error) {
        abortIfcImport(QString::fromUtf8(error.what()));
        return;
    }
    ifcImportProcessed_.reset();
    ifcImportTotal_.reset();
    ifcImportStage_.reset();
    ifcImportCancelRequested_.reset();
}

void MainWindow::abortIfcImport(const QString& error)
{
    ifcImportProgressTimer_.stop();
    if (ifcImportDialog_) {
        ifcImportDialog_->close();
        ifcImportDialog_->deleteLater();
        ifcImportDialog_ = nullptr;
    }
    ifcImporting_ = false;
    const auto path = pendingIfcPath_;
    pendingIfcPath_.clear();
    ifcImportProcessed_.reset();
    ifcImportTotal_.reset();
    ifcImportStage_.reset();
    ifcImportCancelRequested_.reset();
    QMessageBox::critical(this, "IFC import failed", path + "\n" + error);
}
#endif

void MainWindow::startProjectLoad(const QString& path)
{
    cancelRevolveAxisPick();
    viewer_->cancelActiveOperation();
    operationSession_.cancel();
    projectLoading_ = true;
    pendingProjectPath_ = path;
    projectLoadLoaded_ = std::make_shared<std::atomic<int>>(0);
    projectLoadTotal_ = std::make_shared<std::atomic<int>>(0);

    projectLoadDialog_ = new QProgressDialog("Loading project...", nullptr, 0, 0, this);
    projectLoadDialog_->setWindowTitle("Open project");
    projectLoadDialog_->setWindowModality(Qt::NonModal);
    projectLoadDialog_->setAutoClose(false);
    projectLoadDialog_->setAutoReset(false);
    projectLoadDialog_->setMinimumDuration(0);
    projectLoadDialog_->show();
    projectLoadProgressTimer_.start(100);
    projectLoadEventLoopClock_.start();
    projectLoadTotalTimer_.start();
    projectLoadMaxGuiStallMilliseconds_ = 0;
    projectLoadYieldCount_ = 0;
    projectLoadHeartbeatTimer_.start(10);
    statusBar()->showMessage("Loading project...");

    const auto loaded = projectLoadLoaded_;
    const auto total = projectLoadTotal_;
    projectLoadWatcher_.setFuture(QtConcurrent::run(
        [path, loaded, total]() {
            auto result = std::make_shared<cad::application::ProjectLoadResult>(
                cad::application::ProjectController::loadProject(
                path,
                [loaded, total](const int current, const int count) {
                    total->store(count, std::memory_order_relaxed);
                    loaded->store(current, std::memory_order_relaxed);
                },
                false));
            return result;
        }));
}

void MainWindow::updateProjectLoadProgress()
{
    if (!projectLoading_ || !projectLoadDialog_) return;
    const int total = projectLoadTotal_->load(std::memory_order_relaxed);
    const int loaded = projectLoadLoaded_->load(std::memory_order_relaxed);
    if (total > 0) {
        projectLoadDialog_->setRange(0, total);
        projectLoadDialog_->setValue(loaded);
        projectLoadDialog_->setLabelText(
            QString("Loading project... %1 / %2").arg(loaded).arg(total));
    }
}

void MainWindow::finishProjectLoad()
{
    projectLoadProgressTimer_.stop();
    updateProjectLoadProgress();

    projectLoadResult_ = projectLoadWatcher_.result();
    if (!projectLoadResult_->success()) {
        abortProjectLoad(projectLoadResult_->error);
        return;
    }

    projectLoadResult_->body.beginIncrementalRecompute();
    projectLoadRecomputeIndex_ = 0;
    projectLoadRecomputeTimer_.start();
    QTimer::singleShot(0, this, &MainWindow::processProjectLoadRecomputeChunk);
}

void MainWindow::processProjectLoadRecomputeChunk()
{
    if (!projectLoadResult_) return;
    const auto& body = projectLoadResult_->body;
    const auto startIndex = projectLoadRecomputeIndex_;
    QElapsedTimer chunkTimer;
    chunkTimer.start();
    while (projectLoadRecomputeIndex_ < body.features().size()
        && (projectLoadRecomputeIndex_ == startIndex
            || chunkTimer.elapsed() < ProjectLoadTimeBudgetMs)) {
        if (!projectLoadResult_->body.recomputeFeature(projectLoadRecomputeIndex_)) {
            abortProjectLoad(QString::fromStdString(projectLoadResult_->body.lastError()));
            return;
        }
        ++projectLoadRecomputeIndex_;
    }
    const auto chunkFeatures = projectLoadRecomputeIndex_ - startIndex;
    qInfo().noquote() << QString("Project load chunk: features: %1, recompute: %2 ms, "
        "total: %3 ms")
        .arg(chunkFeatures).arg(chunkTimer.elapsed()).arg(chunkTimer.elapsed());
    projectLoadLoaded_->store(static_cast<int>(projectLoadRecomputeIndex_), std::memory_order_relaxed);
    ++projectLoadYieldCount_;

    if (projectLoadRecomputeIndex_ < body.features().size()) {
        QTimer::singleShot(0, this, &MainWindow::processProjectLoadRecomputeChunk);
        return;
    }
    if (!projectLoadResult_->body.finishIncrementalRecompute()) {
        abortProjectLoad(QString::fromStdString(projectLoadResult_->body.lastError()));
        return;
    }

    projectLoadResult_->metrics.recomputeMilliseconds = projectLoadRecomputeTimer_.elapsed();
    projectLoadMetrics_ = projectLoadResult_->metrics;
    QElapsedTimer cleanupTimer;
    cleanupTimer.start();
    modeling_.replaceProject(std::move(projectLoadResult_->document),
        std::move(projectLoadResult_->body));
    currentFile_ = pendingProjectPath_;
    presenter_->clear();
    visibilityManager_ = std::move(projectLoadResult_->visibility);
    applySelection({});
    projectLoadMetrics_.cleanupMilliseconds = cleanupTimer.elapsed();
    projectLoadPresentationIndex_ = 0;
    projectLoadPresentationTimer_.start();
    QTimer::singleShot(0, this, &MainWindow::processProjectLoadPresentationChunk);
}

void MainWindow::processProjectLoadPresentationChunk()
{
    auto& body = modeling_.body();
    const auto startIndex = projectLoadPresentationIndex_;
    QElapsedTimer chunkTimer;
    chunkTimer.start();
    viewer_->beginBulkUpdate();
    while (projectLoadPresentationIndex_ < body.features().size()
        && (projectLoadPresentationIndex_ == startIndex
            || chunkTimer.elapsed() < ProjectLoadTimeBudgetMs)) {
        const auto& feature = body.features()[projectLoadPresentationIndex_];
        if (feature->state() == cad::parametric::FeatureState::UpToDate
            && !feature->shape().IsNull()) {
            viewer_->updateFeature(feature->shape(),
                QString::fromStdString(feature->id()));
        }
        ++projectLoadPresentationIndex_;
    }
    viewer_->endBulkUpdate();
    const auto presentationTime = chunkTimer.elapsed();
    qInfo().noquote() << QString("Project load chunk: features: %1, presentation: %2 ms, "
        "AIS update: %3 ms, selection activation: %4 ms, total: %5 ms")
        .arg(projectLoadPresentationIndex_ - startIndex)
        .arg(presentationTime)
        .arg(viewer_->lastViewerUpdateMilliseconds())
        .arg(viewer_->lastSelectionActivationMilliseconds())
        .arg(presentationTime);
    ++projectLoadYieldCount_;
    projectLoadLoaded_->store(static_cast<int>(projectLoadPresentationIndex_), std::memory_order_relaxed);

    if (projectLoadPresentationIndex_ < body.features().size()) {
        QTimer::singleShot(0, this, &MainWindow::processProjectLoadPresentationChunk);
        return;
    }

    QStringList presentedIds;
    for (const auto& feature : body.features()) {
        if (feature->state() == cad::parametric::FeatureState::UpToDate
            && !feature->shape().IsNull()) {
            presentedIds.append(QString::fromStdString(feature->id()));
        }
    }
    QElapsedTimer finalSyncTimer;
    finalSyncTimer.start();
    viewer_->beginBulkUpdate();
    viewer_->retainFeatures(presentedIds);
    viewer_->restoreSelection(body, {}, {});
    viewer_->endBulkUpdate();
    visibilityManager_.updateBoundingBoxes(body);
    presenter_->refreshVisibility();
    featureEditorPanel_->setFeatures(modeling_.features());
    featureEditorPanel_->setVisibilityGroups(visibilityManager_.groups());
    featureEditorPanel_->setVisibilityFilters(visibilityManager_.filters());
    featureEditorPanel_->setVisibilityPresets(visibilityManager_.presets());
    featureEditorPanel_->setActionState(modeling_.actionState(selectedIds()));
    refreshBimNavigation();
    const auto finalSync = finalSyncTimer.elapsed();
    QElapsedTimer fitTimer;
    fitTimer.start();
    viewer_->fitAll();
    const auto fitTime = fitTimer.elapsed();
    projectLoadResult_.reset();
    updateTitle();
    qInfo().noquote() << QString(
        "Project load: parse: %1 ms, deserialize: %2 ms, recompute: %3 ms, "
        "cleanup/replacement: %4 ms, presentation: %5 ms, final synchronization: %6 ms, "
        "selection activation: %7 ms, FitAll: %8 ms, max GUI stall: %9 ms, "
        "yields: %10, total: %11 ms, features: %12")
        .arg(projectLoadMetrics_.parseMilliseconds)
        .arg(projectLoadMetrics_.deserializeMilliseconds)
        .arg(projectLoadMetrics_.recomputeMilliseconds)
        .arg(projectLoadMetrics_.cleanupMilliseconds)
        .arg(projectLoadPresentationTimer_.elapsed())
        .arg(finalSync).arg(viewer_->lastSelectionActivationMilliseconds())
        .arg(fitTime).arg(projectLoadMaxGuiStallMilliseconds_)
        .arg(projectLoadYieldCount_)
        .arg(projectLoadTotalTimer_.elapsed()).arg(body.features().size());
    statusBar()->showMessage("Opened: " + currentFile_, 3000);

    if (projectLoadDialog_) {
        projectLoadDialog_->close();
        projectLoadDialog_->deleteLater();
        projectLoadDialog_ = nullptr;
    }
    projectLoadLoaded_.reset();
    projectLoadTotal_.reset();
    pendingProjectPath_.clear();
    projectLoadHeartbeatTimer_.stop();
    projectLoading_ = false;
}

void MainWindow::sampleProjectLoadEventLoop()
{
    if (!projectLoading_) return;
    const auto interval = projectLoadEventLoopClock_.restart();
    projectLoadMaxGuiStallMilliseconds_ = std::max(
        projectLoadMaxGuiStallMilliseconds_, static_cast<std::int64_t>(interval));
}

void MainWindow::abortProjectLoad(const QString& error)
{
    QMessageBox::critical(this, "Open failed", pendingProjectPath_ + "\n" + error);
    projectLoadProgressTimer_.stop();
    projectLoadHeartbeatTimer_.stop();
    if (projectLoadDialog_) {
        projectLoadDialog_->close();
        projectLoadDialog_->deleteLater();
        projectLoadDialog_ = nullptr;
    }
    projectLoadResult_.reset();
    projectLoadLoaded_.reset();
    projectLoadTotal_.reset();
    pendingProjectPath_.clear();
    projectLoading_ = false;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (projectLoading_) {
        statusBar()->showMessage("Please wait until project loading finishes.", 3000);
        event->ignore();
        return;
    }
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    if (ifcImporting_) {
        if (ifcImportCancelRequested_)
            ifcImportCancelRequested_->store(true, std::memory_order_relaxed);
        ifcImportWatcher_.future().waitForFinished();
        event->accept();
        return;
    }
#endif
    if (saveDocument()) event->accept();
    else event->ignore();
}

bool MainWindow::confirmReplacement()
{
    featureEditorPanel_->commitPendingEdits();
    if (!project_.isDirty()) return true;
    const auto choice = QMessageBox::question(this, "Current project",
        "Save the current project before replacing it?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return choice == QMessageBox::Discard || (choice == QMessageBox::Save && saveDocument());
}
