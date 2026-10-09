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
#include "operations/SketchFilletService.h"
#include "operations/SketchConstraintSolver.h"
#include "operations/ImportedFeature.h"
#include "operations/SketchProfileBuilder.h"
#include "operations/SketchPathBuilder.h"
#include "application/SelectionResolver.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDialog>
#include <QEvent>
#include <QDir>
#include <QFileDialog>
#include <QKeySequence>
#include <QMessageBox>
#include <QInputDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
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
#include <QSettings>
#include <QStyle>
#include <QIcon>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cstdio>
#include <type_traits>
#include <tuple>

namespace
{
constexpr int ModelPanelWidth = 320;
constexpr qint64 ProjectLoadTimeBudgetMs = 30;

void traceActionState(const char* event, const QString& details)
{
    if (!qEnvironmentVariableIsSet("PARAMETRICCAD_TRACE_ACTIONS")) return;
    static unsigned long long sequence = 0;
    const auto text = details.toUtf8();
    std::fprintf(stderr, "[ACTION %llu] %s %s\n", ++sequence, event, text.constData());
    std::fflush(stderr);
}

void traceSketchEntities(const char* event, const cad::parametric::SketchFeature& sketch)
{
    if (!qEnvironmentVariableIsSet("PARAMETRICCAD_TRACE_ACTIONS")) return;
    QString details = QString("sketch=%1 entityCount=%2")
        .arg(QString::fromStdString(sketch.id()))
        .arg(static_cast<int>(sketch.entities().size()));
    for (const auto& entity : sketch.entities()) {
        std::visit([&details](const auto& value) {
            using Entity = std::decay_t<decltype(value)>;
            const char* type = "Unknown";
            if constexpr (std::is_same_v<Entity, cad::parametric::SketchLine>) type = "Line";
            else if constexpr (std::is_same_v<Entity, cad::parametric::SketchCircle>) type = "Circle";
            else if constexpr (std::is_same_v<Entity, cad::parametric::SketchArc>) type = "Arc";
            details += QString(" [%1 id=%2 construction=%3]")
                .arg(type)
                .arg(QString::fromStdString(value.id))
                .arg(value.construction);
        }, entity);
    }
    traceActionState(event, details);
}

QString shapeBounds(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return "<null>";
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    if (bounds.IsVoid()) return "<void>";
    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    return QString("[%1,%2,%3]..[%4,%5,%6]")
        .arg(xmin).arg(ymin).arg(zmin).arg(xmax).arg(ymax).arg(zmax);
}

QString dialogAddress(const QDialog* dialog)
{
    return dialog ? QString("0x%1").arg(
        reinterpret_cast<quintptr>(dialog), 0, 16) : QString("<null>");
}

std::optional<gp_Pnt2d> sketchCornerPoint(
    const std::vector<cad::parametric::SketchEntity>& entities,
    const std::pair<cad::parametric::SketchEntityId,
                    cad::parametric::SketchEntityId>& corner)
{
    const auto findLine = [&entities](const auto& id) -> const cad::parametric::SketchLine* {
        for (const auto& entity : entities) {
            if (const auto* line = std::get_if<cad::parametric::SketchLine>(&entity);
                line && line->id == id) return line;
        }
        return nullptr;
    };
    const auto* first = findLine(corner.first);
    const auto* second = findLine(corner.second);
    if (!first || !second) return std::nullopt;
    for (const auto& candidate : {first->start, first->end}) {
        if (candidate.Distance(second->start) <= 1.0e-7) return candidate;
        if (candidate.Distance(second->end) <= 1.0e-7) return candidate;
    }
    return std::nullopt;
}

const char* sketchSupportName(const cad::parametric::SketchSupportType support)
{
    switch (support) {
    case cad::parametric::SketchSupportType::XY: return "XY";
    case cad::parametric::SketchSupportType::XZ: return "XZ";
    case cad::parametric::SketchSupportType::YZ: return "YZ";
    case cad::parametric::SketchSupportType::Face: return "Face";
    }
    return "Unknown";
}

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

    const auto setupRectangleOverlay = [this](QWidget* widget) {
        widget->setParent(viewer_);
        widget->setVisible(false);
        widget->installEventFilter(this);
    };
    rectangleWidthTitle_ = new QLabel("Width", viewer_);
    rectangleHeightTitle_ = new QLabel("Height", viewer_);
    rectangleCursorLabel_ = new QLabel(viewer_);
    rectangleWidthEdit_ = new QLineEdit(viewer_);
    rectangleHeightEdit_ = new QLineEdit(viewer_);
    for (auto* widget : {static_cast<QWidget*>(rectangleWidthTitle_),
                         static_cast<QWidget*>(rectangleHeightTitle_),
                         static_cast<QWidget*>(rectangleCursorLabel_),
                         static_cast<QWidget*>(rectangleWidthEdit_),
                         static_cast<QWidget*>(rectangleHeightEdit_)}) {
        setupRectangleOverlay(widget);
        widget->setStyleSheet(
            "background: rgba(255,255,255,235); color: #202733; "
            "border: 1px solid #d89b38; border-radius: 3px; padding: 2px;");
        widget->setAttribute(Qt::WA_TransparentForMouseEvents,
            widget != rectangleWidthEdit_ && widget != rectangleHeightEdit_);
    }
    // The fields receive keyboard focus programmatically after the first
    // corner. They must not become a second hit-test surface over the
    // viewport, otherwise the second corner can be delivered to QLineEdit
    // instead of CadViewer and the rectangle never reaches commit.
    rectangleWidthEdit_->setAttribute(Qt::WA_TransparentForMouseEvents);
    rectangleHeightEdit_->setAttribute(Qt::WA_TransparentForMouseEvents);
    rectangleWidthEdit_->setPlaceholderText("Width");
    rectangleHeightEdit_->setPlaceholderText("Height");
    rectangleWidthEdit_->setFixedSize(78, 24);
    rectangleHeightEdit_->setFixedSize(78, 24);
    rectangleWidthTitle_->setFixedHeight(20);
    rectangleHeightTitle_->setFixedHeight(20);
    rectangleCursorLabel_->setFixedHeight(24);
    rectangleWidthEdit_->setTabOrder(rectangleWidthEdit_, rectangleHeightEdit_);
    connect(rectangleWidthEdit_, &QLineEdit::textEdited, this, [this](const QString& text) {
        rectangleWidthLocked_ = false;
        if (rectangleState_ == RectangleState::Drawing)
            rectangleState_ = RectangleState::NumericInput;
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (ok && value > 1.0e-9) {
            rectangleWidth_ = value;
            rectangleWidthLocked_ = true;
            updateRectangleInput(rectangleCursorPoint_.value_or(
                sketchFirstPoint_.value_or(gp_Pnt2d())));
        }
    });
    connect(rectangleHeightEdit_, &QLineEdit::textEdited, this, [this](const QString& text) {
        rectangleHeightLocked_ = false;
        if (rectangleState_ == RectangleState::Drawing)
            rectangleState_ = RectangleState::NumericInput;
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (ok && value > 1.0e-9) {
            rectangleHeight_ = value;
            rectangleHeightLocked_ = true;
            updateRectangleInput(rectangleCursorPoint_.value_or(
                sketchFirstPoint_.value_or(gp_Pnt2d())));
        }
    });
    connect(rectangleWidthEdit_, &QLineEdit::returnPressed,
        this, &MainWindow::commitRectangleFromInput);
    connect(rectangleHeightEdit_, &QLineEdit::returnPressed,
        this, &MainWindow::commitRectangleFromInput);
    rectangleOverlayTimer_.setInterval(30);
    connect(&rectangleOverlayTimer_, &QTimer::timeout,
        this, &MainWindow::updateRectangleOverlay);

    createActions();
    createParametricPanel();
    createBimPanel();
    setupToolbars();
    restoreWindowLayout();
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
        [this](const gp_Pnt2d& point, const double tolerance,
               const Qt::KeyboardModifiers modifiers) {
            if (sketchTool_ == SketchTool::Trim || sketchTool_ == SketchTool::Extend)
                viewer_->clearSketchTrimPreview();
            if (sketchTool_ == SketchTool::Fillet) {
                const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
                    modeling_.body().findFeature(activeSketchId_));
                if (!sketch) return;
                const auto corner = cad::operations::SketchFilletService::cornerAt(
                    sketch->entities(), point, tolerance);
                traceActionState("sketch.fillet.cornerClick", QString("point=(%1,%2) candidate=%3 tolerance=%4")
                    .arg(point.X()).arg(point.Y()).arg(corner.has_value()).arg(tolerance));
                if (corner && filletFirstLineId_.empty()) {
                    const auto selected = std::find(
                        filletCorners_.begin(), filletCorners_.end(), *corner);
                    if (selected == filletCorners_.end()) {
                        if (filletCorners_.empty()) filletRadiusAnchor_ = point;
                        filletCorners_.push_back(*corner);
                        filletState_ = FilletState::RadiusAdjustment;
                    } else if (modifiers.testFlag(Qt::ControlModifier)) {
                        filletCorners_.erase(selected);
                        if (filletCorners_.empty()) {
                            filletState_ = FilletState::Selecting;
                            filletRadiusAnchor_.reset();
                        }
                    }
                    const bool valid = updateSketchFilletPreview(filletPreviewRadius_);
                    if (valid) statusBar()->showMessage(
                        QString("Sketch Fillet: %1 corner(s); click more or press Enter")
                            .arg(filletCorners_.size()), 0);
                    return;
                }
                auto firstId = filletFirstLineId_;
                auto secondId = std::string{};
                if (corner && firstId.empty()) {
                    firstId = corner->first;
                    secondId = corner->second;
                } else {
                    const auto lineId = cad::operations::SketchConstraintSolver::lineAt(
                        sketch->entities(), point, tolerance);
                    if (!lineId) {
                        statusBar()->showMessage("Sketch Fillet: select a Line or corner", 2000);
                        return;
                    }
                    if (firstId.empty()) {
                        filletFirstLineId_ = *lineId;
                        statusBar()->showMessage("Sketch Fillet: select connected second Line");
                        return;
                    }
                    secondId = *lineId;
                }
                if (firstId.empty() || secondId.empty()) {
                    statusBar()->showMessage("Sketch Fillet: select a Line", 2000);
                    return;
                }
                if (firstId == secondId) {
                    statusBar()->showMessage("Sketch Fillet requires two different Lines", 2000);
                    return;
                }
                QInputDialog dialog(this);
                dialog.setInputMode(QInputDialog::DoubleInput);
                dialog.setWindowTitle("Sketch Fillet");
                dialog.setLabelText("Radius:");
                dialog.setDoubleRange(1.0e-6, 1.0e9);
                dialog.setDoubleDecimals(3);
                dialog.setDoubleValue(2.0);
                const auto updatePreview = [this, sketch, firstId, secondId](const double radius) {
                    const auto plan = cad::operations::SketchFilletService::analyze(
                        *sketch, firstId, secondId, radius);
                    if (plan.changed) {
                        viewer_->setSketchTrimPreview(plan.entities);
                        statusBar()->showMessage(
                            QString("Sketch Fillet: radius %1 mm; press Enter to apply")
                                .arg(radius, 0, 'f', 3), 0);
                    } else {
                        viewer_->clearSketchTrimPreview();
                        statusBar()->showMessage(QString::fromStdString(plan.error), 2000);
                    }
                };
                connect(&dialog, &QInputDialog::doubleValueChanged,
                    this, updatePreview);
                updatePreview(dialog.doubleValue());
                const bool accepted = dialog.exec() == QDialog::Accepted;
                viewer_->clearSketchTrimPreview();
                if (accepted) {
                    const auto result = modeling_.filletSketchLines(
                        activeSketchId_, firstId, secondId, dialog.doubleValue());
                    if (!result.success) {
                        statusBar()->showMessage(QString::fromStdString(result.error), 3000);
                        QMessageBox::warning(this, "Sketch Fillet",
                            QString::fromStdString(result.error));
                    } else {
                        refreshModelView(false);
                        statusBar()->showMessage("Sketch Fillet applied", 2000);
                    }
                }
                filletFirstLineId_.clear();
                statusBar()->showMessage(accepted ? "Sketch Fillet: select first Line"
                                                   : "Sketch Fillet cancelled", 2000);
                return;
            }
            if (sketchTool_ == SketchTool::Chamfer) {
                const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
                    modeling_.body().findFeature(activeSketchId_));
                if (!sketch) return;
                const auto corner = cad::operations::SketchChamferService::cornerAt(
                    sketch->entities(), point, tolerance);
                traceActionState("sketch.chamfer.cornerClick", QString("point=(%1,%2) candidate=%3 tolerance=%4")
                    .arg(point.X()).arg(point.Y()).arg(corner.has_value()).arg(tolerance));
                if (!corner) {
                    hoveredChamferCorner_.reset();
                    updateSketchChamferPreview();
                    statusBar()->showMessage("Sketch Chamfer: select a connected Line corner", 2000);
                    return;
                }
                hoveredChamferCorner_ = *corner;
                const auto before = chamferCorners_.size();
                const auto selected = std::find(chamferCorners_.begin(), chamferCorners_.end(), *corner);
                QString change;
                if (selected == chamferCorners_.end()) {
                    chamferCorners_.push_back(*corner);
                    change = "add";
                } else if (modifiers.testFlag(Qt::ControlModifier)) {
                    chamferCorners_.erase(selected);
                    change = "remove(Ctrl+Click)";
                } else {
                    change = "keep(already selected)";
                }
                traceActionState("sketch.chamfer.selection", QString(
                    "change=%1 before=%2 after=%3 ctrl=%4")
                    .arg(change).arg(before).arg(chamferCorners_.size())
                    .arg(modifiers.testFlag(Qt::ControlModifier)));
                updateSketchChamferPreview();
                if (chamferCorners_.empty()) {
                    statusBar()->showMessage("Sketch Chamfer: select a corner", 2000);
                } else {
                    const auto count = chamferCorners_.size();
                    statusBar()->showMessage(QString("Chamfer: %1 %2 selected; press Enter to apply")
                        .arg(count)
                        .arg(count == 1 ? "corner" : "corners"), 0);
                }
                return;
            }
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
            if (sketchTool_ == SketchTool::Fillet) {
                if (filletState_ == FilletState::RadiusAdjustment
                    && !filletCorners_.empty() && filletRadiusAnchor_) {
                    const double radius = std::max(1.0e-6,
                        filletRadiusAnchor_->Distance(point));
                    updateSketchFilletPreview(radius);
                    return;
                }
                const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
                    modeling_.body().findFeature(activeSketchId_));
                const auto corner = sketch
                    ? cad::operations::SketchFilletService::cornerAt(
                        sketch->entities(), point, tolerance)
                    : std::nullopt;
                traceActionState("sketch.fillet.hover", QString("candidate=%1 tolerance=%2 selected=%3")
                    .arg(corner.has_value()).arg(tolerance).arg(filletCorners_.size()));
                if (corner && filletCorners_.empty()) {
                    const auto plan = cad::operations::SketchFilletService::analyze(
                        *sketch, corner->first, corner->second, filletPreviewRadius_);
                    if (plan.changed) viewer_->setSketchTrimPreview(plan.entities);
                    else {
                        viewer_->setSketchTrimPreview(sketch->entities(), true);
                        statusBar()->showMessage(QString::fromStdString(plan.error), 2000);
                    }
                } else if (filletCorners_.empty()) {
                    viewer_->clearSketchTrimPreview();
                }
                return;
            }
            if (sketchTool_ == SketchTool::Chamfer) {
                const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
                    modeling_.body().findFeature(activeSketchId_));
                const auto corner = sketch
                    ? cad::operations::SketchChamferService::cornerAt(sketch->entities(), point, tolerance)
                    : std::nullopt;
                if (corner != hoveredChamferCorner_) {
                    traceActionState("sketch.chamfer.hover", QString(
                        "point=(%1,%2) candidate=%3 tolerance=%4 selected=%5")
                        .arg(point.X()).arg(point.Y()).arg(corner.has_value())
                        .arg(tolerance).arg(chamferCorners_.size()));
                }
                hoveredChamferCorner_ = corner;
                updateSketchChamferPreview();
                return;
            }
            if (sketchTool_ == SketchTool::Rectangle && !activeSketchId_.empty()) {
                updateRectangleInput(point);
                return;
            }
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
            const bool rectangleActive = sketchTool_ == SketchTool::Rectangle;
            const bool rectangleWasDrawing = rectangleState_ == RectangleState::Drawing
                || rectangleState_ == RectangleState::NumericInput;
            clearRectangleInput();
            sketchFirstPoint_.reset(); sketchSecondPoint_.reset();
            filletFirstLineId_.clear();
            filletCorners_.clear();
            filletState_ = FilletState::Selecting;
            filletRadiusAnchor_.reset();
            if (rectangleActive) {
                if (rectangleWasDrawing) {
                    statusBar()->showMessage("Rectangle: pick first corner", 2000);
                } else {
                    sketchTool_ = SketchTool::None;
                    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
                    statusBar()->showMessage("Sketch mode: select a drawing tool", 2000);
                }
                return;
            }
            if (sketchTool_ == SketchTool::Trim || sketchTool_ == SketchTool::Extend
                || sketchTool_ == SketchTool::Fillet
                || sketchTool_ == SketchTool::Chamfer
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
        // QUndoStack::indexChanged can be emitted from a mouse callback while
        // OCCT is still processing selection. Defer presenter/AIS changes
        // until the event returns to avoid re-entering the viewer context.
        historyVisibilityRefreshPending_ |= visibilityChanged;
        if (historyRefreshScheduled_) return;
        historyRefreshScheduled_ = true;
        QTimer::singleShot(0, this, [this]() {
            historyRefreshScheduled_ = false;
            const bool refreshVisibility = historyVisibilityRefreshPending_;
            historyVisibilityRefreshPending_ = false;
            if (refreshVisibility) refreshVisibilityView();
            else refreshModelView();
            featureEditorPanel_->scheduleRefresh();
        });
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
            if (sweepPathPicking_ && featureIds.size() == 1) {
                const cad::application::SelectionSnapshot selection{{
                    {featureIds.front().toStdString(),
                        cad::application::SelectionKind::Object, std::nullopt}}};
                if (handleSweepPathSelection(selection)) return;
            }
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
            if (handleSweepPathSelection(selection)) return;
            applySelectionSnapshot(selection);
    });

    dockWidget->setWidget(featureEditorPanel_);

    addDockWidget(
        Qt::LeftDockWidgetArea,
        dockWidget
    );
    modelDockAction_ = dockWidget->toggleViewAction();
    modelDockAction_->setText("Constraint Manager / Model");
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
            bimNavigatorAction_ = dockWidget->toggleViewAction();
            bimNavigatorAction_->setText("BIM Navigator");
            menu->addAction(bimNavigatorAction_);
            bimInspectorAction_ = inspectorDock->toggleViewAction();
            bimInspectorAction_->setText("BIM Inspector");
            menu->addAction(bimInspectorAction_);
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
        } else if (const auto* value = std::get_if<cad::parametric::ChamferConstraint>(&constraint)) {
            const auto mode = value->mode == cad::parametric::SketchChamferMode::TwoDistances
                ? "D1/D2" : value->mode == cad::parametric::SketchChamferMode::DistanceAngle
                ? "D/Angle" : "Equal";
            item.label = QString("Chamfer (%1) %2 / %3")
                .arg(mode, QString::fromStdString(value->firstLineId),
                     QString::fromStdString(value->secondLineId));
            item.editable = true;
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
            const auto* chamfer = std::get_if<cad::parametric::ChamferConstraint>(&constraint);
            if (!distance && !radius && !horizontalDistance && !verticalDistance && !angle
                && !angleBetween && !chamfer) return;
            if (chamfer) {
                bool ok = false;
                const double first = QInputDialog::getDouble(this, "Sketch Chamfer",
                    "First distance:", chamfer->firstDistance, 0.001, 1.0e9, 3, &ok);
                if (!ok) return;
                double second = chamfer->secondDistance;
                double angleValue = chamfer->angleRadians;
                if (chamfer->mode == cad::parametric::SketchChamferMode::TwoDistances) {
                    second = QInputDialog::getDouble(this, "Sketch Chamfer",
                        "Second distance:", second, 0.001, 1.0e9, 3, &ok);
                    if (!ok) return;
                } else if (chamfer->mode == cad::parametric::SketchChamferMode::DistanceAngle) {
                    angleValue = QInputDialog::getDouble(this, "Sketch Chamfer",
                        "Angle (degrees):", angleValue * 180.0 / 3.14159265358979323846,
                        0.001, 179.999, 3, &ok) * 3.14159265358979323846 / 180.0;
                    if (!ok) return;
                }
                const auto result = modeling_.updateSketchChamfer(
                    activeSketchId_, constraintId.toStdString(), first, second, angleValue);
                if (!result.success) statusBar()->showMessage(QString::fromStdString(result.error), 3000);
                else { selectedConstraintId_ = constraintId; refreshModelView(false); }
                return;
            }
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
    traceActionState("applySelection", QString("ids=%1 updateViewer=%2 activeSketch=%3")
        .arg(featureIds.join(','))
        .arg(updateViewer)
        .arg(QString::fromStdString(activeSketchId_)));
    cancelRevolveAxisPick();
    viewer_->cancelActiveOperation();
    cancelInteractiveSession("external feature selection");
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
    traceActionState("applySelectionSnapshot", QString("items=%1 updateViewer=%2 activeSketch=%3")
        .arg(static_cast<int>(selection.items.size()))
        .arg(updateViewer)
        .arg(QString::fromStdString(activeSketchId_)));
    cancelRevolveAxisPick();
    viewer_->cancelActiveOperation();
    const bool preserveSweepSession = operationSession_.active()
        && operationSession_.context().kind
            == cad::application::InteractiveOperationKind::Sweep
        && (sweepPathPicking_ || sweepRestoringSelectionMode_);
    if (preserveSweepSession) {
        traceActionState("sweep.selectionUpdate.preserveSession", QString(
            "items=%1 pathPicking=%2 pathSelected=%3")
            .arg(static_cast<int>(selection.items.size()))
            .arg(sweepPathPicking_)
            .arg(sweepPathSelection_.has_value()));
    } else {
        cancelInteractiveSession("selection change not owned by Sweep path picker");
    }
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
    if (booleanFuseAction_) booleanFuseAction_->setEnabled(state.canBoolean);
    if (booleanCutAction_) booleanCutAction_->setEnabled(state.canBoolean);
    if (booleanCommonAction_) booleanCommonAction_->setEnabled(state.canBoolean);
    if (editSketchAction_) editSketchAction_->setEnabled(
        state.canEditSketch && activeSketchId_.empty());
    if (createSketchAction_) createSketchAction_->setEnabled(activeSketchId_.empty());
    bool selectedSketchEdge = currentSelection_.items.size() == 1
        && currentSelection_.items.front().kind == cad::application::SelectionKind::Edge
        && currentSelection_.items.front().subshapeIndex
        && modeling_.body().findFeature(currentSelection_.items.front().featureId)
        && modeling_.body().findFeature(currentSelection_.items.front().featureId)->role()
            == cad::parametric::FeatureRole::Sketch;
    if (toggleSketchConstructionAction_) {
        toggleSketchConstructionAction_->setEnabled(selectedSketchEdge && activeSketchId_.empty());
    }
    if (sketchConstructionAction_) {
        sketchConstructionAction_->setEnabled(
            !activeSketchId_.empty() || (selectedSketchEdge && activeSketchId_.empty()));
        if (selectedSketchEdge && activeSketchId_.empty()) {
            cad::parametric::RevolveAxisDefinition axis;
            const auto resolved = modeling_.resolveRevolveAxis(
                currentSelection_.items.front().featureId, currentSelection_, axis);
            if (resolved.success) {
                const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
                    modeling_.body().findFeature(currentSelection_.items.front().featureId));
                if (sketch) {
                    for (const auto& entity : sketch->entities()) {
                        if (!std::visit([&axis](const auto& value) {
                            return value.id == axis.sketchLineId;
                        }, entity)) continue;
                        sketchConstructionAction_->setChecked(std::visit(
                            [](const auto& value) { return value.construction; }, entity));
                        break;
                    }
                }
            }
        }
    }
    if (linearPatternAction_) linearPatternAction_->setEnabled(selectedIds().size() == 1);
    if (pathPatternAction_) pathPatternAction_->setEnabled(selectedIds().size() == 2);
    if (filletAction_) filletAction_->setEnabled(state.canFillet);
    if (chamferAction_) chamferAction_->setEnabled(state.canChamfer);
    if (shellAction_) shellAction_->setEnabled(state.canShell);
    if (revolveAction_) {
        QString profileDetails = "none";
        if (currentSelection_.items.size() == 1
            && !currentSelection_.items.front().featureId.empty()) {
            const auto feature = modeling_.body().findFeature(
                currentSelection_.items.front().featureId);
            const auto sketch = feature
                ? std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature)
                : nullptr;
            if (sketch) {
                std::size_t constructionCount = 0;
                for (const auto& entity : sketch->entities()) {
                    constructionCount += std::visit(
                        [](const auto& value) { return value.construction ? 1U : 0U; }, entity);
                }
                std::size_t profileCount = 0;
                try {
                    profileCount = cad::operations::SketchProfileBuilder::build(*sketch).faces.size();
                } catch (...) {
                }
                profileDetails = QString("entities=%1 construction=%2 nonConstruction=%3 profiles=%4")
                    .arg(static_cast<int>(sketch->entities().size()))
                    .arg(static_cast<int>(constructionCount))
                    .arg(static_cast<int>(sketch->entities().size() - constructionCount))
                    .arg(static_cast<int>(profileCount));
                traceSketchEntities("revolveAction.profile", *sketch);
            }
        }
        QString selectedIdsText;
        int selectedFeatureRole = -1;
        if (currentSelection_.items.size() == 1) {
            const auto selectedFeature = modeling_.body().findFeature(
                currentSelection_.items.front().featureId);
            if (selectedFeature) selectedFeatureRole = static_cast<int>(selectedFeature->role());
        }
        for (const auto& id : selectedIds()) {
            if (!selectedIdsText.isEmpty()) selectedIdsText += ',';
            selectedIdsText += QString::fromStdString(id);
        }
        traceActionState("revolveAction.setEnabled", QString(
            "enabled=%1 selectedIds=%2 primary=%3 featureRole=%4 items=%5 mode=%6 activeSketch=%7 actionState=SelectionSnapshot canRevolve=%8 profile=%9")
            .arg(state.canRevolve)
            .arg(selectedIdsText)
            .arg(currentSelection_.items.empty()
                ? QString{}
                : QString::fromStdString(currentSelection_.items.front().featureId))
            .arg(selectedFeatureRole)
            .arg(static_cast<int>(currentSelection_.items.size()))
            .arg(static_cast<int>(viewer_->selectionMode()))
            .arg(QString::fromStdString(activeSketchId_))
            .arg(state.canRevolve)
            .arg(profileDetails));
        revolveAction_->setEnabled(state.canRevolve);
        QString associated;
        for (auto* object : revolveAction_->associatedObjects()) {
            const auto widget = qobject_cast<QWidget*>(object);
            if (!widget) continue;
            if (!associated.isEmpty()) associated += ',';
            associated += QString("%1:%2")
                .arg(widget->objectName().isEmpty() ? widget->metaObject()->className()
                                                     : widget->objectName())
                .arg(widget->isEnabled());
        }
        traceActionState("revolveAction.afterSetEnabled", QString(
            "isEnabled=%1 associated=%2")
            .arg(revolveAction_->isEnabled())
            .arg(associated));
    }
    if (sweepAction_) sweepAction_->setEnabled(state.canSweep && activeSketchId_.empty());
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
    newAction_ = fileMenu->addAction("&New");
    newAction_->setShortcut(QKeySequence::New);
    connect(newAction_, &QAction::triggered, this, &MainWindow::newDocument);
    openAction_ = fileMenu->addAction("&Open...");
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, &MainWindow::openDocument);
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    importIfcAction_ = fileMenu->addAction("Import IFC...");
    connect(importIfcAction_, &QAction::triggered, this, &MainWindow::importIfc);
#endif
    saveAction_ = fileMenu->addAction("&Save");
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, [this]() { saveDocument(); });
    auto* saveAsAction = fileMenu->addAction("Save &As...");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this]() { saveDocumentAs(); });

    auto* editMenu = menuBar()->addMenu("&Edit");
    connect(editMenu, &QMenu::aboutToShow, this, [this]() { featureEditorPanel_->commitPendingEdits(); });
    undoAction_ = modeling_.undoStack().createUndoAction(this, "Undo");
    auto undoKeys = QKeySequence::keyBindings(QKeySequence::Undo);
    if (!undoKeys.contains(QKeySequence(Qt::CTRL | Qt::Key_Z))) undoKeys.append(QKeySequence(Qt::CTRL | Qt::Key_Z));
    undoAction_->setShortcuts(undoKeys);
    editMenu->addAction(undoAction_);
    redoAction_ = modeling_.undoStack().createRedoAction(this, "Redo");
    auto redoKeys = QKeySequence::keyBindings(QKeySequence::Redo);
    if (!redoKeys.contains(QKeySequence(Qt::CTRL | Qt::Key_Y))) redoKeys.append(QKeySequence(Qt::CTRL | Qt::Key_Y));
    redoAction_->setShortcuts(redoKeys);
    editMenu->addAction(redoAction_);
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
    sectionXAction_ = viewMenu->addAction("Section X");
    connect(sectionXAction_, &QAction::triggered, this,
        [this]() { viewer_->activateSection(CadViewer::SectionAxis::X); });
    sectionYAction_ = viewMenu->addAction("Section Y");
    connect(sectionYAction_, &QAction::triggered, this,
        [this]() { viewer_->activateSection(CadViewer::SectionAxis::Y); });
    sectionZAction_ = viewMenu->addAction("Section Z");
    connect(sectionZAction_, &QAction::triggered, this,
        [this]() { viewer_->activateSection(CadViewer::SectionAxis::Z); });
    flipSectionAction_ = viewMenu->addAction("Flip Section");
    connect(flipSectionAction_, &QAction::triggered, this,
        [this]() { viewer_->flipSection(); });
    clearSectionAction_ = viewMenu->addAction("Clear Section");
    connect(clearSectionAction_, &QAction::triggered, this,
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
        displayModeActions_.push_back(action);
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

    boxAction_ = modelingMenu->addAction("Box");
    connect(boxAction_, &QAction::triggered, this, &MainWindow::createBox);

    cylinderAction_ = modelingMenu->addAction("Cylinder");
    connect(cylinderAction_, &QAction::triggered, this, &MainWindow::createCylinder);

    modelingMenu->addSeparator();
    createSketchAction_ = modelingMenu->addAction("Create Sketch");
    connect(createSketchAction_, &QAction::triggered, this, &MainWindow::createSketch);
    editSketchAction_ = modelingMenu->addAction("Edit Sketch");
    editSketchAction_->setEnabled(false);
    connect(editSketchAction_, &QAction::triggered, this, &MainWindow::editSelectedSketch);
    sketchLineAction_ = modelingMenu->addAction("Sketch Line");
    sketchLineAction_->setEnabled(false);
    connect(sketchLineAction_, &QAction::triggered, this, &MainWindow::selectSketchLineTool);
    sketchConstructionAction_ = modelingMenu->addAction("Construction Line");
    sketchConstructionAction_->setCheckable(true);
    sketchConstructionAction_->setEnabled(false);
    sketchConstructionAction_->setToolTip(
        "Construction mode for new lines, or Construction property for a selected Sketch line");
    connect(sketchConstructionAction_, &QAction::triggered, this, [this]() {
        if (activeSketchId_.empty()
            && currentSelection_.items.size() == 1
            && currentSelection_.items.front().kind == cad::application::SelectionKind::Edge) {
            toggleSelectedSketchConstruction();
        }
    });
    toggleSketchConstructionAction_ = modelingMenu->addAction("Toggle Construction");
    toggleSketchConstructionAction_->setEnabled(false);
    connect(toggleSketchConstructionAction_, &QAction::triggered, this,
        &MainWindow::toggleSelectedSketchConstruction);
    sketchCircleAction_ = modelingMenu->addAction("Sketch Circle");
    sketchCircleAction_->setEnabled(false);
    connect(sketchCircleAction_, &QAction::triggered, this, &MainWindow::selectSketchCircleTool);
    sketchArcAction_ = modelingMenu->addAction("Arc");
    sketchArcAction_->setEnabled(false);
    connect(sketchArcAction_, &QAction::triggered, this, &MainWindow::selectSketchArcTool);
    sketchCenterArcAction_ = modelingMenu->addAction("Center Arc");
    sketchCenterArcAction_->setEnabled(false);
    connect(sketchCenterArcAction_, &QAction::triggered, this,
        &MainWindow::selectSketchCenterArcTool);
        sketchRectangleAction_ = modelingMenu->addAction("Sketch Rectangle");
        sketchRectangleAction_->setEnabled(false);
        connect(sketchRectangleAction_, &QAction::triggered, this, &MainWindow::selectSketchRectangleTool);
    sketchFilletAction_ = modelingMenu->addAction("Sketch Fillet");
    sketchFilletAction_->setShortcut(Qt::Key_F);
    sketchFilletAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    sketchFilletAction_->setEnabled(false);
    sketchFilletAction_->setToolTip("Fillet two connected Sketch Lines");
    connect(sketchFilletAction_, &QAction::triggered, this, &MainWindow::selectSketchFilletTool);
    sketchChamferAction_ = modelingMenu->addAction("Sketch Chamfer");
    sketchChamferAction_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F));
    sketchChamferAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    sketchChamferAction_->setEnabled(false);
    sketchChamferAction_->setToolTip("Chamfer two connected Sketch Lines");
    connect(sketchChamferAction_, &QAction::triggered, this, &MainWindow::selectSketchChamferTool);
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
    traceActionState("revolveAction.created", "initial enabled=false");
    revolveAction_->setEnabled(false);
    revolveAction_->setToolTip("Revolve a Sketch profile");
    connect(revolveAction_, &QAction::triggered, this, &MainWindow::createRevolve);
    sweepAction_ = modelingMenu->addAction("Sweep");
    sweepAction_->setEnabled(false);
    sweepAction_->setToolTip("Sweep a Sketch profile along an Edge or Sketch path");
    connect(sweepAction_, &QAction::triggered, this, &MainWindow::createSweep);
    linearPatternAction_ = modelingMenu->addAction("Linear Pattern");
    linearPatternAction_->setEnabled(false);
    linearPatternAction_->setToolTip("Create a linear pattern from the selected object");
    connect(linearPatternAction_, &QAction::triggered, this, &MainWindow::createLinearPattern);
    pathPatternAction_ = modelingMenu->addAction("Path Pattern");
    pathPatternAction_->setEnabled(false);
    pathPatternAction_->setToolTip("Create a pattern along the selected path");
    connect(pathPatternAction_, &QAction::triggered, this, &MainWindow::createPathPattern);
    filletAction_ = modelingMenu->addAction("Fillet");
    filletAction_->setEnabled(false);
    filletAction_->setToolTip("Fillet selected edges");
    connect(filletAction_, &QAction::triggered, this, &MainWindow::createFillet);
    chamferAction_ = modelingMenu->addAction("Chamfer");
    chamferAction_->setEnabled(false);
    chamferAction_->setToolTip("Chamfer selected edges");
    connect(chamferAction_, &QAction::triggered, this, &MainWindow::createChamfer);
    shellAction_ = modelingMenu->addAction("Shell");
    shellAction_->setEnabled(false);
    shellAction_->setToolTip("Shell selected solid and opening Faces");
    connect(shellAction_, &QAction::triggered, this, &MainWindow::createShell);
    booleanFuseAction_ = modelingMenu->addAction("Boolean Fuse");
    booleanFuseAction_->setEnabled(false);
    connect(booleanFuseAction_, &QAction::triggered, this, [this]() {
        reportResult(modeling_.createBoolean(
            cad::application::BooleanKind::Fuse, selectedIds()));
    });
    booleanCutAction_ = modelingMenu->addAction("Boolean Cut");
    booleanCutAction_->setEnabled(false);
    connect(booleanCutAction_, &QAction::triggered, this, [this]() {
        reportResult(modeling_.createBoolean(
            cad::application::BooleanKind::Cut, selectedIds()));
    });
    booleanCommonAction_ = modelingMenu->addAction("Boolean Common");
    booleanCommonAction_->setEnabled(false);
    connect(booleanCommonAction_, &QAction::triggered, this, [this]() {
        reportResult(modeling_.createBoolean(
            cad::application::BooleanKind::Common, selectedIds()));
    });
    modelingMenu->addSeparator();

    auto* clearAction = modelingMenu->addAction("Clear");
    connect(clearAction, &QAction::triggered, this, &MainWindow::clearDocument);

    fitAllAction_ = viewMenu->addAction("Fit All");
    connect(fitAllAction_, &QAction::triggered, viewer_, &CadViewer::fitAll);
}

void MainWindow::setupToolbars()
{
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks
        | QMainWindow::AnimatedDocks);

    const auto addToolbar = [this](const char* name) {
        auto* toolbar = addToolBar(QString::fromLatin1(name));
        toolbar->setObjectName(QString::fromLatin1(name) + "ToolBar");
        toolbar->setMovable(true);
        toolbar->setFloatable(true);
        toolbar->setIconSize(QSize(24, 24));
        toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        return toolbar;
    };
    const auto prepare = [this](QAction* action, const QStyle::StandardPixmap icon) {
        if (!action) return;
        const auto label = action->text().remove('&');
        if (!label.isEmpty()) {
            action->setToolTip(label);
            action->setStatusTip(action->toolTip());
        }
        const auto lower = label.toLower();
        QString resource = "file-new";
        if (lower == "new") resource = "file-new";
        else if (lower.startsWith("open")) resource = "file-open";
        else if (lower.startsWith("save")) resource = "file-save";
        else if (lower.contains("import ifc")) resource = "import-ifc";
        else if (lower == "undo") resource = "undo";
        else if (lower == "redo") resource = "redo";
        else if (lower == "create sketch") resource = "create-sketch";
        else if (lower == "edit sketch") resource = "edit-sketch";
        else if (lower == "finish sketch") resource = "finish-sketch";
        else if (lower == "line") resource = "line";
        else if (lower == "rectangle") resource = "rectangle";
        else if (lower == "circle") resource = "circle";
        else if (lower == "arc") resource = "arc";
        else if (lower == "center arc") resource = "center-arc";
        else if (lower == "trim") resource = "trim";
        else if (lower == "extend") resource = "extend";
        else if (lower == "coincident") resource = "coincident";
        else if (lower == "horizontal") resource = "horizontal";
        else if (lower == "vertical") resource = "vertical";
        else if (lower == "distance") resource = "distance";
        else if (lower == "radius") resource = "radius";
        else if (lower == "tangent") resource = "tangent";
        else if (lower == "equal") resource = "equal";
        else if (lower.contains("constraint manager")) resource = "constraint-manager";
        else if (lower == "box") resource = "box";
        else if (lower == "cylinder") resource = "cylinder";
        else if (lower == "extrude") resource = "extrude";
        else if (lower == "pocket") resource = "pocket";
        else if (lower == "revolve") resource = "revolve";
        else if (lower == "sweep") resource = "sweep";
        else if (lower == "shell") resource = "shell";
        else if (lower == "fillet") resource = "fillet";
        else if (lower == "chamfer") resource = "chamfer";
        else if (lower == "boolean fuse") resource = "boolean-fuse";
        else if (lower == "boolean cut") resource = "boolean-cut";
        else if (lower == "boolean common") resource = "boolean-common";
        else if (lower == "linear pattern") resource = "linear-pattern";
        else if (lower == "path pattern") resource = "path-pattern";
        else if (lower.contains("push/pull")) resource = "push-pull";
        else if (lower.contains("move/rotate")) resource = "move";
        else if (lower.contains("x-ray")) resource = "xray";
        else if (lower.startsWith("object")) resource = "select-object";
        else if (lower.startsWith("face")) resource = "select-face";
        else if (lower.startsWith("edge")) resource = "select-edge";
        else if (lower.startsWith("vertex")) resource = "select-vertex";
        else if (lower == "fit all") resource = "fit-all";
        else if (lower == "section x") resource = "section-x";
        else if (lower == "section y") resource = "section-y";
        else if (lower == "section z") resource = "section-z";
        else if (lower.contains("flip section")) resource = "flip-section";
        else if (lower.contains("clear section")) resource = "clear-section";
        else if (lower == "shaded") resource = "shaded";
        else if (lower.contains("shaded with edges")) resource = "shaded-edges";
        else if (lower == "wireframe") resource = "wireframe";
        else if (lower == "show") resource = "show";
        else if (lower == "hide") resource = "hide";
        else if (lower == "isolate") resource = "isolate";
        else if (lower.contains("ghost")) resource = "ghost-others";
        else if (lower.contains("bim navigator")) resource = "bim-navigator";
        else if (lower.contains("bim inspector")) resource = "bim-inspector";
        const auto bundled = QIcon(":/toolbar/" + resource + ".svg");
        action->setIcon(bundled.isNull() ? style()->standardIcon(icon) : bundled);
    };
    const auto add = [&prepare](QToolBar* toolbar, QAction* action,
                                const QStyle::StandardPixmap icon) {
        if (!action) return;
        prepare(action, icon);
        toolbar->addAction(action);
    };

    auto* file = addToolbar("File and History");
    add(file, newAction_, QStyle::SP_FileIcon);
    add(file, openAction_, QStyle::SP_DirOpenIcon);
    add(file, saveAction_, QStyle::SP_DialogSaveButton);
    file->addSeparator();
    add(file, undoAction_, QStyle::SP_ArrowBack);
    add(file, redoAction_, QStyle::SP_ArrowForward);
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    add(file, importIfcAction_, QStyle::SP_DriveHDIcon);
#endif

    auto* sketch = addToolbar("Sketch");
    add(sketch, createSketchAction_, QStyle::SP_FileIcon);
    add(sketch, editSketchAction_, QStyle::SP_FileDialogDetailedView);
    sketch->addSeparator();
    add(sketch, sketchLineAction_, QStyle::SP_ArrowRight);
    add(sketch, sketchRectangleAction_, QStyle::SP_FileDialogDetailedView);
    add(sketch, sketchCircleAction_, QStyle::SP_BrowserReload);
    add(sketch, sketchArcAction_, QStyle::SP_DialogApplyButton);
    add(sketch, sketchCenterArcAction_, QStyle::SP_DialogApplyButton);
    add(sketch, sketchTrimAction_, QStyle::SP_DialogCancelButton);
    add(sketch, sketchExtendAction_, QStyle::SP_ArrowUp);
    add(sketch, sketchFilletAction_, QStyle::SP_DialogApplyButton);
    add(sketch, sketchChamferAction_, QStyle::SP_DialogApplyButton);
    sketch->addSeparator();
    add(sketch, finishSketchAction_, QStyle::SP_DialogOkButton);

    auto* constraints = addToolbar("Constraints");
    add(constraints, sketchCoincidentAction_, QStyle::SP_DialogApplyButton);
    add(constraints, sketchHorizontalAction_, QStyle::SP_ArrowRight);
    add(constraints, sketchVerticalAction_, QStyle::SP_ArrowUp);
    add(constraints, sketchDistanceAction_, QStyle::SP_DialogOpenButton);
    add(constraints, sketchRadiusAction_, QStyle::SP_BrowserReload);
    add(constraints, sketchTangentAction_, QStyle::SP_DialogApplyButton);
    add(constraints, sketchEqualAction_, QStyle::SP_DialogApplyButton);
    add(constraints, modelDockAction_, QStyle::SP_FileDialogDetailedView);

    auto* solid = addToolbar("Solid Modeling");
    add(solid, boxAction_, QStyle::SP_ComputerIcon);
    add(solid, cylinderAction_, QStyle::SP_DriveHDIcon);
    add(solid, extrudeAction_, QStyle::SP_ArrowUp);
    add(solid, pocketAction_, QStyle::SP_ArrowDown);
    add(solid, revolveAction_, QStyle::SP_BrowserReload);
    add(solid, sweepAction_, QStyle::SP_ArrowRight);
    add(solid, shellAction_, QStyle::SP_DialogSaveButton);

    auto* modify = addToolbar("Modify");
    add(modify, filletAction_, QStyle::SP_DialogApplyButton);
    add(modify, chamferAction_, QStyle::SP_DialogApplyButton);
    add(modify, booleanFuseAction_, QStyle::SP_DialogApplyButton);
    add(modify, booleanCutAction_, QStyle::SP_DialogCancelButton);
    add(modify, booleanCommonAction_, QStyle::SP_DialogOpenButton);
    add(modify, linearPatternAction_, QStyle::SP_FileDialogDetailedView);
    add(modify, pathPatternAction_, QStyle::SP_FileDialogDetailedView);

    auto* transform = addToolbar("Transform");
    add(transform, viewer_->pushPullAction(), QStyle::SP_ArrowUp);
    add(transform, viewer_->transformAction(), QStyle::SP_ArrowRight);
    add(transform, viewer_->xRayAction(), QStyle::SP_DialogYesButton);

    auto* selection = addToolbar("Selection");
    add(selection, viewer_->objectSelectionAction(), QStyle::SP_ComputerIcon);
    add(selection, viewer_->faceSelectionAction(), QStyle::SP_DialogApplyButton);
    add(selection, viewer_->edgeSelectionAction(), QStyle::SP_ArrowRight);
    add(selection, viewer_->vertexSelectionAction(), QStyle::SP_ArrowUp);

    auto* view = addToolbar("View");
    add(view, fitAllAction_, QStyle::SP_BrowserReload);
    add(view, sectionXAction_, QStyle::SP_ArrowRight);
    add(view, sectionYAction_, QStyle::SP_ArrowUp);
    add(view, sectionZAction_, QStyle::SP_ArrowDown);
    add(view, flipSectionAction_, QStyle::SP_DialogResetButton);
    add(view, clearSectionAction_, QStyle::SP_DialogCancelButton);
    for (auto* action : displayModeActions_) add(view, action, QStyle::SP_FileDialogDetailedView);

    auto* visibility = addToolbar("Visibility");
    auto* show = new QAction("Show", this);
    connect(show, &QAction::triggered, this, [this]() {
        reportResult(modeling_.setFeatureVisibility(selectedIds(), true));
    });
    auto* hide = new QAction("Hide", this);
    connect(hide, &QAction::triggered, this, [this]() {
        reportResult(modeling_.setFeatureVisibility(selectedIds(), false));
    });
    auto* isolate = new QAction("Isolate", this);
    connect(isolate, &QAction::triggered, this, [this]() {
        visibilityManager_.setIsolatedFeatures(selectedIds());
        refreshVisibilityView();
    });
    auto* ghost = new QAction("Ghost Others", this);
    connect(ghost, &QAction::triggered, this, [this]() {
        visibilityManager_.ghostOthers(selectedIds());
        refreshVisibilityView();
    });
    add(visibility, show, QStyle::SP_DialogYesButton);
    add(visibility, hide, QStyle::SP_DialogCancelButton);
    add(visibility, isolate, QStyle::SP_DialogApplyButton);
    add(visibility, ghost, QStyle::SP_DialogResetButton);

    auto* bim = addToolbar("BIM");
#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
    add(bim, importIfcAction_, QStyle::SP_DriveHDIcon);
#endif
    add(bim, bimNavigatorAction_, QStyle::SP_ComputerIcon);
    add(bim, bimInspectorAction_, QStyle::SP_FileDialogDetailedView);
}

void MainWindow::restoreWindowLayout()
{
    QSettings settings("ParametricCAD", "ParametricCAD");
    restoreGeometry(settings.value("mainWindow/geometry").toByteArray());
    restoreState(settings.value("mainWindow/state").toByteArray(), 1);
}

void MainWindow::saveWindowLayout()
{
    QSettings settings("ParametricCAD", "ParametricCAD");
    settings.setValue("mainWindow/geometry", saveGeometry());
    settings.setValue("mainWindow/state", saveState(1));
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
    cancelInteractiveSession("beginOperation replaces previous session");
    return operationSession_.beginCreate(kind, sourceFeatureIds);
}

void MainWindow::cancelInteractiveSession(const char* reason)
{
    const auto operation = operationSession_.active()
        ? static_cast<int>(operationSession_.context().kind) : -1;
    traceActionState("interactiveSession.cancel", QString(
        "reason=%1 active=%2 operation=%3 dialog=%4 selectionMode=%5 "
        "selectionItems=%6 sweepPathPicking=%7 pathSelected=%8")
        .arg(reason)
        .arg(operationSession_.active())
        .arg(operation)
        .arg(dialogAddress(sweepDialog_))
        .arg(static_cast<int>(viewer_->selectionMode()))
        .arg(static_cast<int>(currentSelection_.items.size()))
        .arg(sweepPathPicking_)
        .arg(sweepPathSelection_.has_value()));
    operationSession_.cancel();
    traceActionState("interactiveSession.cancelled", QString(
        "reason=%1 active=%2 operation=-1 dialog=%3")
        .arg(reason).arg(operationSession_.active()).arg(dialogAddress(sweepDialog_)));
}

void MainWindow::cancelOperation()
{
    traceActionState("sweep.cancelOperation", QString("dialog=%1 sessionActive=%2 pathSelected=%3")
        .arg(dialogAddress(sweepDialog_))
        .arg(operationSession_.active())
        .arg(sweepPathSelection_.has_value()));
    cancelRevolveAxisPick();
    if (sweepPathPicking_) {
        sweepPathPicking_ = false;
        viewer_->setSelectionMode(static_cast<CadViewer::SelectionMode>(
            sweepPreviousSelectionMode_));
        sweepProfileSelection_ = {};
        statusBar()->showMessage("Sweep path pick cancelled", 2000);
    }
    sweepRestoringSelectionMode_ = false;
    sweepPathSelection_.reset();
    sweepPathReference_.reset();
    if (sweepDialog_) {
        auto* dialog = sweepDialog_;
        sweepDialog_ = nullptr;
        sweepPathLabel_ = nullptr;
        sweepCommitButton_ = nullptr;
        dialog->close();
        dialog->deleteLater();
    }
    cancelInteractiveSession("explicit operation cancel");
}

bool MainWindow::commitOperation()
{
    traceActionState("interactiveSession.commit", QString(
        "active=%1 operation=%2 dialog=%3")
        .arg(operationSession_.active())
        .arg(operationSession_.active()
            ? static_cast<int>(operationSession_.context().kind) : -1)
        .arg(dialogAddress(sweepDialog_)));
    const bool committed = operationSession_.commit().has_value();
    traceActionState("interactiveSession.committed", QString(
        "success=%1 active=%2 dialog=%3")
        .arg(committed).arg(operationSession_.active()).arg(dialogAddress(sweepDialog_)));
    return committed;
}

void MainWindow::createSketch()
{
    const auto state = modeling_.actionState(currentSelection_);
    QStringList placements{"XY Plane", "XZ Plane", "YZ Plane"};
    if (state.canSketchOnFace) placements.prepend("Selected Planar Face");

    bool accepted = false;
    const auto placement = QInputDialog::getItem(
        this, "Create Sketch", "Placement:", placements, 0, false, &accepted);
    if (!accepted) return;

    cad::application::ModelingResult result;
    if (placement == "Selected Planar Face") {
        result = modeling_.createSketchOnFace(currentSelection_);
    } else {
        const auto support = placement == "XZ Plane"
            ? cad::parametric::SketchSupportType::XZ
            : placement == "YZ Plane"
                ? cad::parametric::SketchSupportType::YZ
                : cad::parametric::SketchSupportType::XY;
        result = modeling_.createSketch(support);
    }
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
        sketchModeState_ = SketchModeState::Editing;
        sketchTool_ = SketchTool::None;
        rectangleState_ = RectangleState::Ready;
        sketchFirstPoint_.reset();
        sketchSecondPoint_.reset();
        filletFirstLineId_.clear();
        filletCorners_.clear();
        chamferCorners_.clear();
        hoveredFilletCorner_.reset();
        hoveredChamferCorner_.reset();
        lastChamferPreviewTrace_.clear();
        lastChamferPreviewError_.clear();
        chamferState_ = ChamferState::CornerSelection;
        filletState_ = FilletState::Selecting;
        filletRadiusAnchor_.reset();
        clearRectangleInput();
        constraintFirstPoint_.reset();
        selectedConstraintId_.clear();
        viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
        viewer_->enterSketchMode(frame.origin, frame.xDirection,
            frame.yDirection, frame.normal, QString::fromStdString(sketchId));
        viewer_->setSketchConstraintMarkers(*sketch);
        sketchLineAction_->setEnabled(true);
        sketchConstructionAction_->setEnabled(true);
        sketchCircleAction_->setEnabled(true);
        sketchArcAction_->setEnabled(true);
        sketchCenterArcAction_->setEnabled(true);
        sketchRectangleAction_->setEnabled(true);
        sketchFilletAction_->setEnabled(true);
        sketchChamferAction_->setEnabled(true);
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
    traceActionState("finishSketch.begin", QString("activeSketch=%1 mode=%2")
        .arg(QString::fromStdString(activeSketchId_))
        .arg(static_cast<int>(viewer_->selectionMode())));
    const QString finishedSketchId = QString::fromStdString(activeSketchId_);
    if (rectangleWidthEdit_) rectangleWidthEdit_->clearFocus();
    if (rectangleHeightEdit_) rectangleHeightEdit_->clearFocus();
    viewer_->setFocus(Qt::OtherFocusReason);
    viewer_->removeEventFilter(this);
    viewer_->exitSketchMode();
    activeSketchId_.clear();
    sketchModeState_ = SketchModeState::Inactive;
    sketchTool_ = SketchTool::None;
    rectangleState_ = RectangleState::Ready;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    filletFirstLineId_.clear();
    filletCorners_.clear();
    chamferCorners_.clear();
    hoveredFilletCorner_.reset();
    hoveredChamferCorner_.reset();
    lastChamferPreviewTrace_.clear();
    lastChamferPreviewError_.clear();
    chamferState_ = ChamferState::CornerSelection;
    filletState_ = FilletState::Selecting;
    filletRadiusAnchor_.reset();
    clearRectangleInput();
    constraintFirstPoint_.reset();
    selectedConstraintId_.clear();
    sketchLineAction_->setEnabled(false);
    sketchConstructionAction_->setEnabled(false);
    sketchConstructionAction_->setChecked(false);
    sketchCircleAction_->setEnabled(false);
    sketchArcAction_->setEnabled(false);
    sketchCenterArcAction_->setEnabled(false);
    sketchRectangleAction_->setEnabled(false);
    sketchFilletAction_->setEnabled(false);
    sketchChamferAction_->setEnabled(false);
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
    traceActionState("finishSketch.end", QString("finishedSketch=%1 activeSketch=%2 mode=%3 revolveEnabled=%4")
        .arg(finishedSketchId)
        .arg(QString::fromStdString(activeSketchId_))
        .arg(static_cast<int>(viewer_->selectionMode()))
        .arg(revolveAction_ && revolveAction_->isEnabled()));
}

void MainWindow::selectSketchLineTool()
{
    clearRectangleInput();
    if (sketchModeState_ != SketchModeState::Editing) return;
    sketchTool_ = SketchTool::Line;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Line);
}

void MainWindow::selectSketchCircleTool()
{
    clearRectangleInput();
    if (sketchModeState_ != SketchModeState::Editing) return;
    sketchTool_ = SketchTool::Circle;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Circle);
}

void MainWindow::selectSketchArcTool()
{
    clearRectangleInput();
    if (sketchModeState_ != SketchModeState::Editing) return;
    sketchTool_ = SketchTool::Arc;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Arc);
    refreshConstraintManager();
    statusBar()->showMessage("Arc: pick start point");
}

void MainWindow::selectSketchCenterArcTool()
{
    clearRectangleInput();
    if (sketchModeState_ != SketchModeState::Editing) return;
    sketchTool_ = SketchTool::CenterArc;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::CenterArc);
    refreshConstraintManager();
    statusBar()->showMessage("Center Arc: pick center");
}

void MainWindow::selectSketchRectangleTool()
{
    if (sketchModeState_ != SketchModeState::Editing) return;
    sketchTool_ = SketchTool::Rectangle;
    rectangleState_ = RectangleState::Ready;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    clearRectangleInput();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Rectangle);
    statusBar()->showMessage("Rectangle: pick first corner");
}

void MainWindow::selectSketchFilletTool()
{
    clearRectangleInput();
    if (sketchModeState_ != SketchModeState::Editing) return;
    sketchTool_ = SketchTool::Fillet;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    filletFirstLineId_.clear();
    filletCorners_.clear();
    hoveredFilletCorner_.reset();
    filletState_ = FilletState::Selecting;
    filletPreviewRadius_ = 2.0;
    filletRadiusAnchor_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Fillet);
    viewer_->installEventFilter(this);
    viewer_->clearSketchTrimPreview();
    statusBar()->showMessage("Sketch Fillet: select first Line");
}

void MainWindow::selectSketchChamferTool()
{
    if (sketchModeState_ != SketchModeState::Editing || activeSketchId_.empty()) return;
    sketchTool_ = SketchTool::Chamfer;
    chamferCorners_.clear();
    hoveredChamferCorner_.reset();
    lastChamferPreviewTrace_.clear();
    lastChamferPreviewError_.clear();
    chamferState_ = ChamferState::CornerSelection;
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Chamfer);
    viewer_->installEventFilter(this);
    viewer_->setFocus(Qt::OtherFocusReason);
    statusBar()->showMessage("Sketch Chamfer: select corners; Enter applies");
}

void MainWindow::commitSketchChamferSelection()
{
    traceActionState("sketch.chamfer.commit.enter", QString("state=%1 selected=%2")
        .arg(static_cast<int>(chamferState_)).arg(chamferCorners_.size()));
    if (sketchTool_ != SketchTool::Chamfer || chamferCorners_.empty()) {
        traceActionState("sketch.chamfer.commit.return", "reason=no selected corners");
        return;
    }
    chamferState_ = ChamferState::ParameterAdjustment;
    hoveredChamferCorner_.reset();
    QDialog dialog(this);
    dialog.setWindowTitle("Sketch Chamfer");
    auto* form = new QFormLayout(&dialog);
    auto* modeBox = new QComboBox(&dialog);
    modeBox->addItems({"Equal Distance", "Two Distances", "Distance + Angle"});
    auto* firstSpin = new QDoubleSpinBox(&dialog);
    firstSpin->setRange(1.0e-6, 1.0e9);
    firstSpin->setDecimals(3);
    firstSpin->setValue(5.0);
    auto* secondSpin = new QDoubleSpinBox(&dialog);
    secondSpin->setRange(1.0e-6, 1.0e9);
    secondSpin->setDecimals(3);
    secondSpin->setValue(5.0);
    auto* validation = new QLabel(&dialog);
    validation->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
    auto* apply = buttons->button(QDialogButtonBox::Apply);
    form->addRow("Chamfer Type:", modeBox);
    form->addRow("Distance 1:", firstSpin);
    form->addRow("Distance 2 / Angle:", secondSpin);
    form->addRow("Selected Corners:", new QLabel(QString::number(chamferCorners_.size()), &dialog));
    form->addRow("Validation:", validation);
    form->addRow(buttons);

    const auto parameters = [&]() {
        const auto mode = modeBox->currentIndex() == 1
            ? cad::parametric::SketchChamferMode::TwoDistances
            : modeBox->currentIndex() == 2
            ? cad::parametric::SketchChamferMode::DistanceAngle
            : cad::parametric::SketchChamferMode::EqualDistance;
        const double second = mode == cad::parametric::SketchChamferMode::DistanceAngle
            ? secondSpin->value() * 3.14159265358979323846 / 180.0
            : secondSpin->value();
        return std::tuple{mode, firstSpin->value(), second,
            mode == cad::parametric::SketchChamferMode::DistanceAngle
                ? secondSpin->value() : 0.0};
    };
    const auto update = [&]() {
        const auto [mode, first, second, angle] = parameters();
        const bool valid = updateSketchChamferPreview(mode, first, second, angle);
        validation->setText(valid ? "Valid" : lastChamferPreviewError_);
        apply->setEnabled(valid);
        secondSpin->setSuffix(mode == cad::parametric::SketchChamferMode::DistanceAngle
            ? " deg" : " mm");
    };
    connect(modeBox, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, update);
    connect(firstSpin, &QDoubleSpinBox::valueChanged, &dialog, update);
    connect(secondSpin, &QDoubleSpinBox::valueChanged, &dialog, update);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (apply->isEnabled()) dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (accepted) {
        const auto [mode, firstDistance, secondDistance, angleRadians] = parameters();
        const auto result = modeling_.chamferSketchCorners(
            activeSketchId_, chamferCorners_, mode, firstDistance, secondDistance, angleRadians);
        if (!result.success) {
            chamferState_ = ChamferState::ValidationError;
            traceActionState("sketch.chamfer.commit.failed", QString::fromStdString(result.error));
            statusBar()->showMessage(QString::fromStdString(result.error), 3000);
            QMessageBox::warning(this, "Sketch Chamfer", QString::fromStdString(result.error));
            updateSketchChamferPreview(mode, firstDistance, secondDistance, angleRadians);
            return;
        }
        viewer_->clearSketchTrimPreview();
        refreshModelView(false);
        statusBar()->showMessage("Sketch Chamfer applied", 2000);
    }
    if (!accepted) viewer_->clearSketchTrimPreview();
    chamferCorners_.clear();
    hoveredChamferCorner_.reset();
    chamferState_ = ChamferState::CornerSelection;
    statusBar()->showMessage(accepted ? "Sketch Chamfer: select corner"
                                      : "Sketch Chamfer cancelled", 2000);
}

bool MainWindow::updateSketchFilletPreview(const double radius)
{
    if (filletCorners_.empty() || activeSketchId_.empty()) {
        viewer_->clearSketchTrimPreview();
        return false;
    }
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    if (!sketch) return false;
    auto working = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        sketch->clone("fillet-preview"));
    if (!working) return false;
    working->replaceEntities(0, working->entityCount(), sketch->entities());
    working->replaceConstraints(sketch->constraints());
    auto entities = sketch->entities();
    std::string error;
    for (const auto& [first, second] : filletCorners_) {
        const auto plan = cad::operations::SketchFilletService::analyze(
            *working, first, second, radius);
        if (!plan.changed) {
            viewer_->setSketchTrimPreview(sketch->entities(), true);
            statusBar()->showMessage(QString::fromStdString(plan.error), 2000);
            return false;
        }
        entities = plan.entities;
        working->replaceEntities(0, working->entityCount(), entities);
    }
    filletPreviewRadius_ = radius;
    viewer_->setSketchTrimPreview(entities);
    statusBar()->showMessage(
        QString("Sketch Fillet: %1 corner(s), R=%2 mm; Enter applies, Esc cancels")
            .arg(filletCorners_.size()).arg(radius, 0, 'f', 3), 0);
    return true;
}

bool MainWindow::updateSketchChamferPreview(
    const cad::parametric::SketchChamferMode mode,
    const double firstDistance,
    const double secondDistance,
    const double angleRadians)
{
    if (activeSketchId_.empty()) {
        viewer_->clearSketchTrimPreview();
        viewer_->clearSketchCornerMarkers();
        return false;
    }
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    if (!sketch) {
        viewer_->clearSketchTrimPreview();
        viewer_->clearSketchCornerMarkers();
        return false;
    }

    std::vector<cad::application::ModelingController::SketchChamferCorner> corners =
        chamferCorners_;
    if (hoveredChamferCorner_
        && std::find(corners.begin(), corners.end(), *hoveredChamferCorner_) == corners.end()) {
        corners.push_back(*hoveredChamferCorner_);
    }
    if (corners.empty()) {
        viewer_->clearSketchTrimPreview();
        viewer_->clearSketchCornerMarkers();
        return false;
    }

    auto working = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        sketch->clone("chamfer-preview"));
    if (!working) {
        viewer_->clearSketchTrimPreview();
        viewer_->clearSketchCornerMarkers();
        return false;
    }
    working->replaceEntities(0, working->entityCount(), sketch->entities());
    working->replaceConstraints(sketch->constraints());
    auto entities = sketch->entities();
    for (const auto& [first, second] : corners) {
        const auto plan = cad::operations::SketchChamferService::analyze(
            *working, first, second, mode, firstDistance, secondDistance, angleRadians);
    if (!plan.changed) {
            lastChamferPreviewError_ = QString::fromStdString(plan.error);
            viewer_->setSketchTrimPreview(entities, true);
            std::vector<gp_Pnt2d> selectedPoints;
            for (const auto& corner : chamferCorners_) {
                if (const auto point = sketchCornerPoint(sketch->entities(), corner))
                    selectedPoints.push_back(*point);
            }
            viewer_->setSketchCornerMarkers(selectedPoints);
            const auto trace = QString(
                "valid=0 selected=%1 previewEntities=%2 distance1=%3 distance2=%4 error=%5")
                .arg(chamferCorners_.size()).arg(entities.size())
                .arg(firstDistance).arg(secondDistance).arg(QString::fromStdString(plan.error));
            if (trace != lastChamferPreviewTrace_) {
                lastChamferPreviewTrace_ = trace;
                traceActionState("sketch.chamfer.preview", trace);
            }
            if (!plan.error.empty()) statusBar()->showMessage(
                QString::fromStdString(plan.error), 2000);
            return false;
        }
        entities = plan.entities;
        working->replaceEntities(0, working->entityCount(), entities);
    }

    lastChamferPreviewError_.clear();
    viewer_->setSketchTrimPreview(entities);
    std::vector<gp_Pnt2d> selectedPoints;
    for (const auto& corner : chamferCorners_) {
        if (const auto point = sketchCornerPoint(sketch->entities(), corner))
            selectedPoints.push_back(*point);
    }
    viewer_->setSketchCornerMarkers(selectedPoints);
    const auto trace = QString(
        "valid=1 selected=%1 hover=%2 previewEntities=%3 distance1=%4 distance2=%5")
        .arg(chamferCorners_.size()).arg(hoveredChamferCorner_.has_value())
        .arg(entities.size()).arg(firstDistance).arg(secondDistance);
    if (trace != lastChamferPreviewTrace_) {
        lastChamferPreviewTrace_ = trace;
        traceActionState("sketch.chamfer.preview", trace);
    }
    statusBar()->showMessage(
        QString("Sketch Chamfer: %1 corner(s), D=%2 mm; Enter applies, Esc cancels")
            .arg(chamferCorners_.size())
            .arg(firstDistance, 0, 'f', 3), 0);
    return true;
}

void MainWindow::commitSketchFilletSelection()
{
    if (filletCorners_.empty() || activeSketchId_.empty()) return;
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    if (!sketch) return;
    QInputDialog dialog(this);
    dialog.setInputMode(QInputDialog::DoubleInput);
    dialog.setWindowTitle("Sketch Fillet");
    dialog.setLabelText(QString("Radius for %1 corner(s):").arg(filletCorners_.size()));
    dialog.setDoubleRange(1.0e-6, 1.0e9);
    dialog.setDoubleDecimals(3);
    dialog.setDoubleValue(2.0);
    connect(&dialog, &QInputDialog::doubleValueChanged, this,
        [this](const double radius) { updateSketchFilletPreview(radius); });
    updateSketchFilletPreview(dialog.doubleValue());
    const bool accepted = dialog.exec() == QDialog::Accepted;
    viewer_->clearSketchTrimPreview();
    if (accepted) {
        const auto result = modeling_.filletSketchCorners(
            activeSketchId_, filletCorners_, dialog.doubleValue());
        if (!result.success) {
            statusBar()->showMessage(QString::fromStdString(result.error), 3000);
            QMessageBox::warning(this, "Sketch Fillet",
                QString::fromStdString(result.error));
        } else {
            refreshModelView(false);
            statusBar()->showMessage("Sketch Fillet applied", 2000);
        }
    }
    filletCorners_.clear();
    filletState_ = FilletState::Selecting;
    filletRadiusAnchor_.reset();
    statusBar()->showMessage(accepted ? "Sketch Fillet: select corner"
                                      : "Sketch Fillet cancelled", 2000);
}

void MainWindow::selectSketchTrimTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::Trim;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Trim);
    statusBar()->showMessage("Trim: click a Line, Arc, or Circle segment");
}

void MainWindow::selectSketchExtendTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::Extend;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::Extend);
    statusBar()->showMessage("Extend: hover and click a Line or Arc endpoint");
}

void MainWindow::selectSketchCoincidentTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::Coincident;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Coincident: click two Line or Arc endpoints");
}

void MainWindow::selectSketchHorizontalTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::Horizontal;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Horizontal: click a Line");
}

void MainWindow::selectSketchVerticalTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::Vertical;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Vertical: click a Line");
}

void MainWindow::selectSketchDistanceTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::Distance;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Distance: click a Line");
}

void MainWindow::selectSketchRadiusTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::Radius;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Radius: click a Circle or Arc");
}

void MainWindow::selectSketchHorizontalDistanceTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::HorizontalDistance;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Horizontal Distance: click two points");
}

void MainWindow::selectSketchVerticalDistanceTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::VerticalDistance;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Vertical Distance: click two points");
}

void MainWindow::selectSketchAngleTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::Angle;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Angle: click a Line");
}

void MainWindow::selectSketchParallelTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::Parallel;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Parallel: click two Lines");
}

void MainWindow::selectSketchPerpendicularTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::Perpendicular;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Perpendicular: click two Lines");
}

void MainWindow::selectSketchAngleBetweenLinesTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::Tangent;
    constraintFirstPoint_.reset();
    viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
    statusBar()->showMessage("Angle Between Lines: click reference and dependent Lines");
}

void MainWindow::selectSketchTangentTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::Equal;
    constraintFirstPoint_.reset();
    statusBar()->showMessage("Tangent: click Line, then Circle or Arc");
}

void MainWindow::selectSketchEqualTool()
{
    clearRectangleInput();
    sketchTool_ = SketchTool::AngleBetweenLines;
    constraintFirstPoint_.reset();
    statusBar()->showMessage("Equal: click two compatible entities");
}

void MainWindow::toggleSelectedSketchConstruction()
{
    if (currentSelection_.items.size() != 1
        || currentSelection_.items.front().kind != cad::application::SelectionKind::Edge) {
        return;
    }
    const auto selection = currentSelection_;
    const auto& item = selection.items.front();
    cad::parametric::RevolveAxisDefinition axis;
    const auto resolved = modeling_.resolveRevolveAxis(item.featureId, selection, axis);
    if (!resolved.success) {
        statusBar()->showMessage(QString::fromStdString(resolved.error), 3000);
        return;
    }
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(item.featureId));
    if (sketch) traceSketchEntities("toggleConstruction.before", *sketch);
    traceActionState("toggleConstruction.target", QString("sketch=%1 entityId=%2")
        .arg(QString::fromStdString(item.featureId))
        .arg(QString::fromStdString(axis.sketchLineId)));
    const auto result = modeling_.toggleSketchEntityConstruction(
        item.featureId, axis.sketchLineId);
    if (!result.success) {
        reportResult(result);
        return;
    }
    refreshModelView(false);
    applySelectionSnapshot(selection, false);
    if (sketch) traceSketchEntities("toggleConstruction.after", *sketch);
}

std::optional<gp_Pnt2d> MainWindow::rectanglePointForCursor(const gp_Pnt2d& cursor) const
{
    if (!sketchFirstPoint_) return std::nullopt;
    const double width = rectangleWidthLocked_ ? rectangleWidth_
        : std::abs(cursor.X() - sketchFirstPoint_->X());
    const double height = rectangleHeightLocked_ ? rectangleHeight_
        : std::abs(cursor.Y() - sketchFirstPoint_->Y());
    if (width <= 1.0e-9 || height <= 1.0e-9) return std::nullopt;
    return gp_Pnt2d(
        sketchFirstPoint_->X() + rectangleDirectionX_ * width,
        sketchFirstPoint_->Y() + rectangleDirectionY_ * height);
}

void MainWindow::updateRectangleInput(const gp_Pnt2d& cursor)
{
    if (sketchModeState_ != SketchModeState::Editing
        || sketchTool_ != SketchTool::Rectangle
        || (rectangleState_ != RectangleState::Drawing
            && rectangleState_ != RectangleState::NumericInput)
        || !sketchFirstPoint_) return;
    traceActionState("rectangle.mouseMove", QString(
        "sketch=%1 state=%2 first=(%3,%4) cursor=(%5,%6)")
        .arg(QString::fromStdString(activeSketchId_))
        .arg(static_cast<int>(rectangleState_))
        .arg(sketchFirstPoint_->X()).arg(sketchFirstPoint_->Y())
        .arg(cursor.X()).arg(cursor.Y()));
    rectangleCursorPoint_ = cursor;
    if (std::abs(cursor.X() - sketchFirstPoint_->X()) > 1.0e-9)
        rectangleDirectionX_ = cursor.X() >= sketchFirstPoint_->X() ? 1 : -1;
    if (std::abs(cursor.Y() - sketchFirstPoint_->Y()) > 1.0e-9)
        rectangleDirectionY_ = cursor.Y() >= sketchFirstPoint_->Y() ? 1 : -1;
    if (!rectangleWidthLocked_) rectangleWidth_ = std::abs(cursor.X() - sketchFirstPoint_->X());
    if (!rectangleHeightLocked_) rectangleHeight_ = std::abs(cursor.Y() - sketchFirstPoint_->Y());
    if (rectangleWidthEdit_ && !rectangleWidthLocked_)
        rectangleWidthEdit_->setText(QString::number(rectangleWidth_, 'f', 3));
    if (rectangleHeightEdit_ && !rectangleHeightLocked_)
        rectangleHeightEdit_->setText(QString::number(rectangleHeight_, 'f', 3));
    const auto end = rectanglePointForCursor(cursor);
    traceActionState("rectangle.geometry", QString(
        "sketch=%1 first=(%2,%3) end=%4 width=%5 height=%6 locked=(%7,%8)")
        .arg(QString::fromStdString(activeSketchId_))
        .arg(sketchFirstPoint_->X()).arg(sketchFirstPoint_->Y())
        .arg(end ? QString("(%1,%2)").arg(end->X()).arg(end->Y()) : QStringLiteral("<invalid>"))
        .arg(rectangleWidth_).arg(rectangleHeight_)
        .arg(rectangleWidthLocked_).arg(rectangleHeightLocked_));
    viewer_->setSketchPreviewPointOverride(end);
    viewer_->refreshSketchPreview();
    updateRectangleOverlay();
}

void MainWindow::updateRectangleOverlay()
{
    if (!sketchFirstPoint_ || sketchTool_ != SketchTool::Rectangle) {
        rectangleOverlayTimer_.stop();
        for (auto* widget : {static_cast<QWidget*>(rectangleWidthTitle_),
                             static_cast<QWidget*>(rectangleHeightTitle_),
                             static_cast<QWidget*>(rectangleCursorLabel_),
                             static_cast<QWidget*>(rectangleWidthEdit_),
                             static_cast<QWidget*>(rectangleHeightEdit_)}) {
            if (widget) widget->hide();
        }
        return;
    }
    rectangleOverlayTimer_.start();
    const gp_Pnt2d cursor = rectangleCursorPoint_.value_or(*sketchFirstPoint_);
    const auto end = rectanglePointForCursor(cursor).value_or(cursor);
    const auto firstScreen = viewer_->sketchPointToScreen(*sketchFirstPoint_);
    const auto endScreen = viewer_->sketchPointToScreen(end);
    const auto cursorScreen = viewer_->sketchPointToScreen(cursor);
    if (!firstScreen || !endScreen) return;
    const int left = std::min(firstScreen->x(), endScreen->x());
    const int right = std::max(firstScreen->x(), endScreen->x());
    const int top = std::min(firstScreen->y(), endScreen->y());
    const int bottom = std::max(firstScreen->y(), endScreen->y());
    const int widthX = (left + right) / 2 - rectangleWidthEdit_->width() / 2;
    const int heightY = (top + bottom) / 2 - rectangleHeightEdit_->height() / 2;
    rectangleWidthTitle_->move(widthX, std::max(2, top - 70));
    rectangleWidthEdit_->move(widthX, std::max(22, top - 47));
    rectangleHeightTitle_->move(std::max(2, left - 88), std::max(2, heightY - 25));
    rectangleHeightEdit_->move(std::max(2, left - 88), std::max(22, heightY));
    traceActionState("rectangle.overlay", QString(
        "sketch=%1 firstScreen=(%2,%3) endScreen=(%4,%5) widthRect=%6,%7,%8,%9 "
        "heightRect=%10,%11,%12,%13")
        .arg(QString::fromStdString(activeSketchId_))
        .arg(firstScreen->x()).arg(firstScreen->y())
        .arg(endScreen->x()).arg(endScreen->y())
        .arg(rectangleWidthEdit_->x()).arg(rectangleWidthEdit_->y())
        .arg(rectangleWidthEdit_->width()).arg(rectangleWidthEdit_->height())
        .arg(rectangleHeightEdit_->x()).arg(rectangleHeightEdit_->y())
        .arg(rectangleHeightEdit_->width()).arg(rectangleHeightEdit_->height()));
    if (cursorScreen) {
        rectangleCursorLabel_->setText(QString("X: %1  Y: %2")
            .arg(cursor.X(), 0, 'f', 3).arg(cursor.Y(), 0, 'f', 3));
        rectangleCursorLabel_->adjustSize();
        rectangleCursorLabel_->move(
            std::clamp(cursorScreen->x() + 12, 2, std::max(2, viewer_->width() - rectangleCursorLabel_->width() - 2)),
            std::clamp(cursorScreen->y() + 12, 2, std::max(2, viewer_->height() - rectangleCursorLabel_->height() - 2)));
    }
    for (auto* widget : {static_cast<QWidget*>(rectangleWidthTitle_),
                         static_cast<QWidget*>(rectangleHeightTitle_),
                         static_cast<QWidget*>(rectangleCursorLabel_),
                         static_cast<QWidget*>(rectangleWidthEdit_),
                         static_cast<QWidget*>(rectangleHeightEdit_)}) {
        if (widget) widget->show(), widget->raise();
    }
}

bool MainWindow::commitRectangle(const gp_Pnt2d& point)
{
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    const auto before = sketch ? sketch->entities().size() : 0U;
    traceActionState("rectangle.commit.enter", QString(
        "sketch=%1 state=%2 first=%3 point=(%4,%5) entitiesBefore=%6")
        .arg(QString::fromStdString(activeSketchId_))
        .arg(static_cast<int>(rectangleState_))
        .arg(sketchFirstPoint_.has_value())
        .arg(point.X()).arg(point.Y()).arg(static_cast<int>(before)));
    if (!sketchFirstPoint_) {
        traceActionState("rectangle.commit.return", "reason=missing first point");
        return false;
    }
    const auto first = *sketchFirstPoint_;
    if (first.Distance(point) <= 1.0e-9
        || std::abs(point.X() - first.X()) <= 1.0e-9
        || std::abs(point.Y() - first.Y()) <= 1.0e-9) {
        statusBar()->showMessage("Rectangle width and height must be positive", 2500);
        return false;
    }
    const gp_Pnt2d corners[] = {
        first, {point.X(), first.Y()}, point, {first.X(), point.Y()}};
    cad::application::ModelingResult result;
    for (int index = 0; index < 4; ++index) {
        result = modeling_.addSketchLine(activeSketchId_, corners[index],
            corners[(index + 1) % 4]);
        if (!result.success) break;
    }
    if (!result.success) {
        traceActionState("rectangle.commit.failed", QString("error=%1")
            .arg(QString::fromStdString(result.error)));
        QMessageBox::warning(this, "Sketch entity failed",
            QString::fromStdString(result.error));
        return false;
    }
    const auto afterSketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    traceActionState("rectangle.commit.done", QString(
        "sketch=%1 entitiesBefore=%2 entitiesAfter=%3 result=success")
        .arg(QString::fromStdString(activeSketchId_))
        .arg(static_cast<int>(before))
        .arg(afterSketch ? static_cast<int>(afterSketch->entities().size()) : -1));
    return true;
}

void MainWindow::commitRectangleFromInput()
{
    if (!sketchFirstPoint_ || sketchModeState_ != SketchModeState::Editing
        || sketchTool_ != SketchTool::Rectangle
        || (rectangleState_ != RectangleState::Drawing
            && rectangleState_ != RectangleState::NumericInput)) return;
    bool widthOk = false;
    bool heightOk = false;
    const double width = rectangleWidthEdit_->text().toDouble(&widthOk);
    const double height = rectangleHeightEdit_->text().toDouble(&heightOk);
    if (!widthOk || !heightOk || width <= 1.0e-9 || height <= 1.0e-9) {
        statusBar()->showMessage("Enter positive Width and Height", 2500);
        return;
    }
    rectangleWidth_ = width;
    rectangleHeight_ = height;
    rectangleState_ = RectangleState::NumericInput;
    rectangleWidthLocked_ = true;
    rectangleHeightLocked_ = true;
    const auto end = rectanglePointForCursor(rectangleCursorPoint_.value_or(*sketchFirstPoint_));
    if (!end || !commitRectangle(*end)) return;
    sketchFirstPoint_.reset();
    sketchSecondPoint_.reset();
    clearRectangleInput();
    rectangleState_ = RectangleState::Ready;
}

void MainWindow::clearRectangleInput()
{
    filletFirstLineId_.clear();
    rectangleOverlayTimer_.stop();
    rectangleState_ = RectangleState::Ready;
    rectangleCursorPoint_.reset();
    rectangleWidthLocked_ = false;
    rectangleHeightLocked_ = false;
    rectangleWidth_ = 0.0;
    rectangleHeight_ = 0.0;
    viewer_->clearSketchPreview();
    for (auto* widget : {static_cast<QWidget*>(rectangleWidthTitle_),
                         static_cast<QWidget*>(rectangleHeightTitle_),
                         static_cast<QWidget*>(rectangleCursorLabel_),
                         static_cast<QWidget*>(rectangleWidthEdit_),
                         static_cast<QWidget*>(rectangleHeightEdit_)}) {
        if (widget) widget->hide();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == viewer_ && event->type() == QEvent::KeyPress
        && sketchModeState_ == SketchModeState::Editing
        && (sketchTool_ == SketchTool::Fillet || sketchTool_ == SketchTool::Chamfer)) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (sketchTool_ == SketchTool::Fillet) commitSketchFilletSelection();
            else commitSketchChamferSelection();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            if ((sketchTool_ == SketchTool::Fillet
                    && filletCorners_.empty() && filletFirstLineId_.empty())
                || (sketchTool_ == SketchTool::Chamfer && chamferCorners_.empty())) {
                sketchTool_ = SketchTool::None;
                viewer_->removeEventFilter(this);
                viewer_->setSketchPreviewTool(CadViewer::SketchPreviewTool::None);
                viewer_->clearSketchTrimPreview();
                statusBar()->showMessage("Sketch mode: select a drawing tool", 2000);
                return true;
            }
            if (sketchTool_ == SketchTool::Fillet) {
                filletCorners_.clear();
                filletFirstLineId_.clear();
            } else {
                chamferCorners_.clear();
            }
            viewer_->clearSketchTrimPreview();
            viewer_->clearSketchCornerMarkers();
            lastChamferPreviewTrace_.clear();
            lastChamferPreviewError_.clear();
            statusBar()->showMessage(sketchTool_ == SketchTool::Fillet
                ? "Sketch Fillet selection cancelled" : "Sketch Chamfer selection cancelled", 2000);
            return true;
        }
    }
    if ((watched == rectangleWidthEdit_ || watched == rectangleHeightEdit_)
        && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Tab) {
            auto* next = watched == rectangleWidthEdit_
                ? rectangleHeightEdit_ : rectangleWidthEdit_;
            next->setFocus(Qt::TabFocusReason);
            next->selectAll();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            commitRectangleFromInput();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            sketchFirstPoint_.reset();
            sketchSecondPoint_.reset();
            clearRectangleInput();
            statusBar()->showMessage("Rectangle input cancelled", 2000);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::handleSketchPoint(const gp_Pnt2d& point, const double hitTolerance)
{
    if (activeSketchId_.empty()) {
        traceActionState("rectangle.commit.skipped", "reason=missing active sketch");
        return;
    }
    if (sketchTool_ == SketchTool::None) {
        traceActionState("rectangle.commit.skipped", "reason=no active sketch tool");
        return;
    }
    const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(activeSketchId_));
    if (!sketch) {
        traceActionState("rectangle.commit.skipped", "reason=active sketch feature missing");
        return;
    }
    if (sketchTool_ == SketchTool::Rectangle) {
        traceActionState("rectangle.mousePoint", QString(
            "sketch=%1 state=%2 first=%3 second=%4 point=(%5,%6) entities=%7")
            .arg(QString::fromStdString(activeSketchId_))
            .arg(static_cast<int>(rectangleState_))
            .arg(sketchFirstPoint_.has_value()).arg(sketchSecondPoint_.has_value())
            .arg(point.X()).arg(point.Y())
            .arg(static_cast<int>(sketch->entities().size())));
    }
    if (sketchTool_ == SketchTool::Arc) {
        const auto frame = sketch->currentFrame();
        gp_Pnt world = frame.origin;
        world.Translate(gp_Vec(frame.xDirection) * point.X()
            + gp_Vec(frame.yDirection) * point.Y());
        const auto clickNumber = sketchFirstPoint_
            ? (sketchSecondPoint_ ? 3 : 2) : 1;
        traceActionState("arc.click", QString(
            "click=%1 local=(%2,%3) world=(%4,%5,%6) support=%7 "
            "frameOrigin=(%8,%9,%10) frameX=(%11,%12,%13) frameY=(%14,%15,%16) "
            "frameNormal=(%17,%18,%19)")
            .arg(clickNumber)
            .arg(point.X()).arg(point.Y())
            .arg(world.X()).arg(world.Y()).arg(world.Z())
            .arg(sketchSupportName(sketch->supportType()))
            .arg(frame.origin.X()).arg(frame.origin.Y()).arg(frame.origin.Z())
            .arg(frame.xDirection.X()).arg(frame.xDirection.Y()).arg(frame.xDirection.Z())
            .arg(frame.yDirection.X()).arg(frame.yDirection.Y()).arg(frame.yDirection.Z())
            .arg(frame.normal.X()).arg(frame.normal.Y()).arg(frame.normal.Z()));
    }
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
        if (sketchTool_ == SketchTool::Rectangle) {
            traceActionState("rectangle.stateTransition", "old=Ready new=Drawing");
            rectangleState_ = RectangleState::Drawing;
            rectangleCursorPoint_ = point;
            rectangleDirectionX_ = 1;
            rectangleDirectionY_ = 1;
            rectangleWidth_ = 0.0;
            rectangleHeight_ = 0.0;
            rectangleWidthLocked_ = false;
            rectangleHeightLocked_ = false;
            rectangleWidthEdit_->setText("0.000");
            rectangleHeightEdit_->setText("0.000");
            rectangleWidthTitle_->setText("Width");
            rectangleHeightTitle_->setText("Height");
            rectangleWidthEdit_->show();
            rectangleHeightEdit_->show();
            rectangleWidthEdit_->setFocus(Qt::MouseFocusReason);
            rectangleWidthEdit_->selectAll();
            updateRectangleOverlay();
            statusBar()->showMessage("Rectangle: move cursor or enter Width/Height");
        }
        if (sketchTool_ == SketchTool::Arc) {
            statusBar()->showMessage("Arc: pick end point");
        } else if (sketchTool_ == SketchTool::CenterArc) {
            statusBar()->showMessage("Center Arc: pick start point");
        }
        return;
    }
    if ((sketchTool_ == SketchTool::Arc || sketchTool_ == SketchTool::CenterArc)
        && !sketchSecondPoint_) {
        sketchSecondPoint_ = point;
        statusBar()->showMessage(sketchTool_ == SketchTool::Arc
            ? "Arc: pick point on arc" : "Center Arc: pick end point");
        return;
    }
    if (sketchTool_ == SketchTool::Rectangle) {
        traceActionState("rectangle.secondPoint.accepted", QString(
            "sketch=%1 state=%2 first=(%3,%4) second=(%5,%6)")
            .arg(QString::fromStdString(activeSketchId_))
            .arg(static_cast<int>(rectangleState_))
            .arg(sketchFirstPoint_->X()).arg(sketchFirstPoint_->Y())
            .arg(point.X()).arg(point.Y()));
        if (rectangleState_ != RectangleState::Drawing
            && rectangleState_ != RectangleState::NumericInput) {
            traceActionState("rectangle.commit.skipped", QString("reason=invalid state %1")
                .arg(static_cast<int>(rectangleState_)));
            return;
        }
        updateRectangleInput(point);
        const auto end = rectanglePointForCursor(point);
        if (!end) {
            traceActionState("rectangle.commit.skipped", "reason=invalid width or height");
            statusBar()->showMessage("Rectangle width and height must be positive", 2500);
            return;
        }
        const bool committed = commitRectangle(*end);
        if (!committed) {
            traceActionState("rectangle.commit.failed", "reason=commitRectangle returned false");
            return;
        }
        sketchFirstPoint_.reset();
        sketchSecondPoint_.reset();
        clearRectangleInput();
        traceActionState("rectangle.stateTransition", "old=Drawing new=Ready");
        rectangleState_ = RectangleState::Ready;
        traceActionState("rectangle.commit.success", QString("entities=%1")
            .arg(static_cast<int>(sketch->entities().size())));
        return;
    }

    const auto first = *sketchFirstPoint_;
    sketchFirstPoint_.reset();
    const auto second = sketchSecondPoint_;
    sketchSecondPoint_.reset();
    cad::application::ModelingResult result;
    if (sketchTool_ == SketchTool::Line) {
        result = modeling_.addSketchLine(activeSketchId_, first, point,
            sketchConstructionAction_ && sketchConstructionAction_->isChecked());
    } else if (sketchTool_ == SketchTool::Circle) {
        result = modeling_.addSketchCircle(activeSketchId_, first, first.Distance(point));
    } else if (sketchTool_ == SketchTool::Arc) {
        result = modeling_.addSketchThreePointArc(activeSketchId_, first, *second, point);
    } else if (sketchTool_ == SketchTool::CenterArc) {
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
    } else if (sketchTool_ == SketchTool::Arc || sketchTool_ == SketchTool::CenterArc) {
        const auto updated = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            modeling_.body().findFeature(activeSketchId_));
        if (updated && !updated->entities().empty()) {
            if (const auto* arc = std::get_if<cad::parametric::SketchArc>(
                    &updated->entities().back())) {
                traceActionState("arc.stored", QString(
                    "center=(%1,%2) start=(%3,%4) end=(%5,%6) radius=%7")
                    .arg(arc->center.X()).arg(arc->center.Y())
                    .arg(arc->startPoint().X()).arg(arc->startPoint().Y())
                    .arg(arc->endPoint().X()).arg(arc->endPoint().Y())
                    .arg(arc->radius));
            }
        }
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

void MainWindow::createSweep()
{
    if (currentSelection_.items.size() != 1
        || currentSelection_.items.front().kind != cad::application::SelectionKind::Object) {
        return;
    }
    const auto profile = modeling_.body().findFeature(currentSelection_.items.front().featureId);
    if (!profile || profile->role() != cad::parametric::FeatureRole::Sketch) return;
    if (!beginOperation(cad::application::InteractiveOperationKind::Sweep,
            {profile->id()})) return;
    traceActionState("sweep.begin", QString("profile=%1 sessionActive=%2 pathSelected=%3 dialog=%4 operation=%5")
        .arg(QString::fromStdString(profile->id()))
        .arg(operationSession_.active())
        .arg(sweepPathSelection_.has_value())
        .arg(dialogAddress(sweepDialog_))
        .arg(static_cast<int>(operationSession_.context().kind)));
    sweepProfileSelection_ = currentSelection_;
    sweepPathSelection_.reset();
    sweepPathReference_.reset();

    auto* dialog = new QDialog(this);
    dialog->setWindowTitle("Sweep");
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(
        QString("Profile: %1").arg(QString::fromStdString(profile->name())), dialog));
    sweepPathLabel_ = new QLabel("Path: <not selected>", dialog);
    layout->addWidget(sweepPathLabel_);
    auto* buttons = new QHBoxLayout();
    auto* pickButton = new QPushButton("Pick Path", dialog);
    sweepCommitButton_ = new QPushButton("Commit", dialog);
    sweepCommitButton_->setEnabled(false);
    auto* cancelButton = new QPushButton("Cancel", dialog);
    buttons->addWidget(pickButton);
    buttons->addWidget(sweepCommitButton_);
    buttons->addWidget(cancelButton);
    layout->addLayout(buttons);
    sweepDialog_ = dialog;
    connect(pickButton, &QPushButton::clicked, this, &MainWindow::pickSweepPath);
    connect(sweepCommitButton_, &QPushButton::clicked, this, [this]() {
        traceActionState("sweep.commitButton.clicked", QString(
            "dialog=%1 sessionActive=%2 pathSelected=%3 profile=%4 pathOwner=%5 refValid=%6 enabled=%7 visible=%8")
            .arg(dialogAddress(sweepDialog_))
            .arg(operationSession_.active())
            .arg(sweepPathSelection_.has_value())
            .arg(sweepProfileSelection_.items.empty() ? QString("<none>")
                : QString::fromStdString(sweepProfileSelection_.items.front().featureId))
            .arg(sweepPathSelection_ && !sweepPathSelection_->items.empty()
                ? QString::fromStdString(sweepPathSelection_->items.front().featureId)
                : QString("<none>"))
            .arg(sweepPathReference_.has_value())
            .arg(sweepCommitButton_ && sweepCommitButton_->isEnabled())
            .arg(sweepDialog_ && sweepDialog_->isVisible()));
        commitSweep();
    });
    connect(cancelButton, &QPushButton::clicked, this, &MainWindow::cancelOperation);
    connect(dialog, &QDialog::rejected, this, [this]() {
        if (sweepDialog_) cancelOperation();
    });
    dialog->show();
    traceActionState("sweep.dialog.created", QString("dialog=%1 visible=%2 commitEnabled=%3")
        .arg(dialogAddress(dialog)).arg(dialog->isVisible())
        .arg(sweepCommitButton_->isEnabled()));
    statusBar()->showMessage("Sweep staged: click Pick Path");
}

void MainWindow::pickSweepPath()
{
    if (!operationSession_.active()) return;
    sweepPathSelection_.reset();
    sweepPathReference_.reset();
    if (sweepCommitButton_) sweepCommitButton_->setEnabled(false);
    if (sweepPathLabel_) sweepPathLabel_->setText("Path: <not selected>");
    sweepPreviousSelectionMode_ = static_cast<int>(viewer_->selectionMode());
    sweepPathPicking_ = true;
    viewer_->setAxisPickCancelHandler([this]() { cancelOperation(); });
    viewer_->setSelectionMode(CadViewer::SelectionMode::Edge);
    traceActionState("sweep.beginPickPath", QString("profile=%1 mode=%2 dialog=%3 visible=%4")
        .arg(QString::fromStdString(sweepProfileSelection_.items.front().featureId))
        .arg(static_cast<int>(viewer_->selectionMode()))
        .arg(dialogAddress(sweepDialog_))
        .arg(sweepDialog_ && sweepDialog_->isVisible()));
    statusBar()->showMessage("Pick Sweep path Edge or Sketch");
}

void MainWindow::commitSweep()
{
    traceActionState("sweep.commit.enter", QString(
        "dialog=%1 sessionActive=%2 operation=%3 pathSelected=%4 refValid=%5 profileItems=%6 pathItems=%7")
        .arg(dialogAddress(sweepDialog_))
        .arg(operationSession_.active())
        .arg(operationSession_.active()
            ? static_cast<int>(operationSession_.context().kind) : -1)
        .arg(sweepPathSelection_.has_value())
        .arg(sweepPathReference_.has_value())
        .arg(static_cast<int>(sweepProfileSelection_.items.size()))
        .arg(sweepPathSelection_ ? static_cast<int>(sweepPathSelection_->items.size()) : 0));
    if (!operationSession_.active()) {
        traceActionState("sweep.commit.return", "reason=session inactive");
        if (sweepCommitButton_) sweepCommitButton_->setEnabled(false);
        if (sweepDialog_) {
            traceActionState("sweep.invalidSession", QString(
                "dialog=%1 action=close").arg(dialogAddress(sweepDialog_)));
            cancelOperation();
        }
        statusBar()->showMessage("Sweep session is no longer active", 3000);
        return;
    }
    if (operationSession_.context().kind
        != cad::application::InteractiveOperationKind::Sweep) {
        traceActionState("sweep.commit.return", "reason=operation is not Sweep");
        statusBar()->showMessage("Active operation is not Sweep", 3000);
        return;
    }
    if (sweepProfileSelection_.items.size() != 1) {
        traceActionState("sweep.commit.return", "reason=profile missing");
        statusBar()->showMessage("Sweep profile is missing", 3000);
        return;
    }
    if (!sweepPathSelection_ || sweepPathSelection_->items.size() != 1) {
        traceActionState("sweep.commit.return", "reason=path missing");
        statusBar()->showMessage("Sweep requires a selected path Edge", 3000);
        return;
    }
    const bool sketchPath = sweepPathSelection_->items.front().kind
        == cad::application::SelectionKind::Object;
    if (!sketchPath && !sweepPathReference_) {
        traceActionState("sweep.commit.return", "reason=persistent path reference missing");
        statusBar()->showMessage("Sweep path reference is missing", 3000);
        return;
    }
    traceActionState("sweep.commit.begin", QString("profile=%1 pathItems=%2")
        .arg(QString::fromStdString(sweepProfileSelection_.items.front().featureId))
        .arg(static_cast<int>(sweepPathSelection_->items.size())));
    const auto profile = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
        modeling_.body().findFeature(sweepProfileSelection_.items.front().featureId));
    const auto pathItem = sweepPathSelection_->items.front();
    const auto pathOwner = modeling_.body().findFeature(pathItem.featureId);
    if (profile && pathOwner && sweepPathReference_) {
        const auto frame = profile->currentFrame();
        const auto resolved = cad::topology::TopologicalReferenceResolver::resolveAgainstShape(
            *sweepPathReference_, pathOwner->shape());
        QString geometryDetails = QString("support=%1 frameOrigin=(%2,%3,%4) "
            "frameNormal=(%5,%6,%7) profileBounds=%8 pathOwner=%9")
            .arg(sketchSupportName(profile->supportType()))
            .arg(frame.origin.X()).arg(frame.origin.Y()).arg(frame.origin.Z())
            .arg(frame.normal.X()).arg(frame.normal.Y()).arg(frame.normal.Z())
            .arg(shapeBounds(profile->shape()))
            .arg(QString::fromStdString(pathOwner->id()));
        if (resolved.status == cad::topology::ResolveStatus::Resolved
            && resolved.shape && resolved.shape->ShapeType() == TopAbs_EDGE) {
            const auto edge = TopoDS::Edge(*resolved.shape);
            BRepAdaptor_Curve curve(edge);
            const auto start = curve.Value(curve.FirstParameter());
            const auto end = curve.Value(curve.LastParameter());
            const gp_Vec startFromPlane(frame.origin, start);
            geometryDetails += QString(" pathStart=(%1,%2,%3) pathEnd=(%4,%5,%6) "
                "pathLength=%7 startPlaneDistance=%8 pathBounds=%9")
                .arg(start.X()).arg(start.Y()).arg(start.Z())
                .arg(end.X()).arg(end.Y()).arg(end.Z())
                .arg(start.Distance(end))
                .arg(std::abs(startFromPlane.Dot(gp_Vec(frame.normal))))
                .arg(shapeBounds(edge));
        } else {
            geometryDetails += QString(" pathResolveStatus=%1")
                .arg(static_cast<int>(resolved.status));
        }
        traceActionState("sweep.geometry", geometryDetails);
    }
    traceActionState("sweep.MakePipe.called", "backend Sweep build begins");
    traceActionState("sweep.commit.beforeCreate", QString("profile=%1 pathOwner=%2 refValid=%3")
        .arg(QString::fromStdString(sweepProfileSelection_.items.front().featureId))
        .arg(QString::fromStdString(sweepPathSelection_->items.front().featureId))
        .arg(sweepPathReference_.has_value()));
    const auto result = modeling_.createSweep(sweepProfileSelection_, *sweepPathSelection_);
    traceActionState("sweep.commit.afterCreate", QString("success=%1 featureId=%2 error=%3")
        .arg(result.success).arg(QString::fromStdString(result.id))
        .arg(QString::fromStdString(result.error)));
    if (!result.success) {
        traceActionState("sweep.commit.failed", QString::fromStdString(result.error));
        reportResult(result);
        return;
    }
    const auto sweep = modeling_.body().findFeature(result.id);
    if (sweep) {
        int solidCount = 0;
        for (TopExp_Explorer explorer(sweep->shape(), TopAbs_SOLID); explorer.More(); explorer.Next())
            ++solidCount;
        GProp_GProps volumeProperties;
        BRepGProp::VolumeProperties(sweep->shape(), volumeProperties);
        traceActionState("sweep.shape", QString("feature=%1 null=%2 shapeType=%3 solids=%4 volume=%5 state=%6 error=%7 bounds=%8")
            .arg(QString::fromStdString(result.id))
            .arg(sweep->shape().IsNull())
            .arg(static_cast<int>(sweep->shape().ShapeType()))
            .arg(solidCount)
            .arg(volumeProperties.Mass())
            .arg(static_cast<int>(sweep->state()))
            .arg(QString::fromStdString(sweep->error()))
            .arg(shapeBounds(sweep->shape())));
    }
    if (sweepDialog_) {
        auto* dialog = sweepDialog_;
        sweepDialog_ = nullptr;
        sweepPathLabel_ = nullptr;
        sweepCommitButton_ = nullptr;
        dialog->close();
        dialog->deleteLater();
    }
    viewer_->setAxisPickCancelHandler({});
    sweepPathPicking_ = false;
    traceActionState("sweep.commit.sessionBeforeFinish", QString(
        "active=%1 operation=%2 dialog=%3")
        .arg(operationSession_.active())
        .arg(operationSession_.active()
            ? static_cast<int>(operationSession_.context().kind) : -1)
        .arg(dialogAddress(sweepDialog_)));
    operationSession_.commit();
    traceActionState("sweep.commit.sessionFinished", QString(
        "active=%1 operation=-1").arg(operationSession_.active()));
    traceActionState("sweep.commit.done", QString("feature=%1 sessionActive=%2")
        .arg(QString::fromStdString(result.id)).arg(operationSession_.active()));
    sweepProfileSelection_ = {};
    sweepPathSelection_.reset();
    sweepPathReference_.reset();
    reportResult(result);
}

bool MainWindow::handleSweepPathSelection(
    const cad::application::SelectionSnapshot& selection)
{
    if (!sweepPathPicking_) return false;
    if (selection.items.size() != 1
        || selection.items.front().kind != cad::application::SelectionKind::Edge) {
        return true;
    }
    const auto& item = selection.items.front();
    const auto owner = modeling_.body().findFeature(item.featureId);
    if (!owner) return true;
    if (item.kind == cad::application::SelectionKind::Object) {
        const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(owner);
        if (!sketch || sketch->id() == sweepProfileSelection_.items.front().featureId) {
            statusBar()->showMessage("Sweep path must be a different Sketch", 3000);
            return true;
        }
        try {
            const auto path = cad::operations::SketchPathBuilder::build(*sketch);
            sweepPathSelection_ = selection;
            sweepPathReference_.reset();
            viewer_->setAxisPickCancelHandler({});
            sweepPathPicking_ = false;
            sweepRestoringSelectionMode_ = true;
            viewer_->setSelectionMode(static_cast<CadViewer::SelectionMode>(
                sweepPreviousSelectionMode_));
            sweepRestoringSelectionMode_ = false;
            if (sweepPathLabel_) sweepPathLabel_->setText(QString("Path: %1 | Entities: %2")
                .arg(QString::fromStdString(owner->name()))
                .arg(static_cast<int>(path.entityIds.size())));
            if (sweepCommitButton_) sweepCommitButton_->setEnabled(true);
            traceActionState("sweep.pathAccepted", QString(
                "type=SketchPath owner=%1 entities=%2 sessionActive=%3 operation=%4 "
                "buttonEnabled=%5 buttonVisible=%6 dialog=%7 dialogVisible=%8")
                .arg(QString::fromStdString(item.featureId))
                .arg(static_cast<int>(path.entityIds.size()))
                .arg(operationSession_.active())
                .arg(operationSession_.active()
                    ? static_cast<int>(operationSession_.context().kind) : -1)
                .arg(sweepCommitButton_ && sweepCommitButton_->isEnabled())
                .arg(sweepCommitButton_ && sweepCommitButton_->isVisible())
                .arg(dialogAddress(sweepDialog_))
                .arg(sweepDialog_ && sweepDialog_->isVisible()));
            statusBar()->showMessage("Sweep Sketch path selected; click Commit");
        } catch (const std::exception& error) {
            traceActionState("sweep.pathRejected", QString::fromStdString(error.what()));
            statusBar()->showMessage(QString::fromStdString(error.what()), 4000);
        }
        return true;
    }
    if (item.kind != cad::application::SelectionKind::Edge || !item.subshapeIndex)
        return true;
    try {
        cad::application::SelectionResolver resolver(modeling_.body());
        const auto edge = resolver.resolve(item);
        if (!edge || edge->ShapeType() != TopAbs_EDGE) return true;
        sweepPathReference_ = cad::topology::TopologicalSignatureBuilder::createReference(
            owner->id(), owner->shape(), *edge);
    } catch (const std::exception& error) {
        traceActionState("sweep.pathRejected", QString::fromStdString(error.what()));
        statusBar()->showMessage(QString("Invalid Sweep path: %1").arg(
            QString::fromStdString(error.what())), 4000);
        return true;
    }
    sweepPathSelection_ = selection;
    viewer_->setAxisPickCancelHandler({});
    sweepPathPicking_ = false;
    sweepRestoringSelectionMode_ = true;
    viewer_->setSelectionMode(static_cast<CadViewer::SelectionMode>(
        sweepPreviousSelectionMode_));
    sweepRestoringSelectionMode_ = false;
    if (sweepPathLabel_) {
        sweepPathLabel_->setText(QString("Path: %1 | Edge %2")
            .arg(owner ? QString::fromStdString(owner->name())
                       : QString::fromStdString(item.featureId))
            .arg(*item.subshapeIndex));
    }
    if (sweepCommitButton_) sweepCommitButton_->setEnabled(true);
    traceActionState("sweep.pathAccepted", QString("owner=%1 edge=%2 sessionActive=%3 operation=%4 buttonEnabled=%5 buttonVisible=%6 dialog=%7 dialogVisible=%8")
        .arg(QString::fromStdString(item.featureId))
        .arg(*item.subshapeIndex)
        .arg(operationSession_.active())
        .arg(operationSession_.active()
            ? static_cast<int>(operationSession_.context().kind) : -1)
        .arg(sweepCommitButton_ && sweepCommitButton_->isEnabled())
        .arg(sweepCommitButton_ && sweepCommitButton_->isVisible())
        .arg(dialogAddress(sweepDialog_))
        .arg(sweepDialog_ && sweepDialog_->isVisible()));
    statusBar()->showMessage("Sweep path selected; click Commit");
    return true;
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
    if (!editingExisting) revolveProfileFeatureId_ = featureId;
    revolveAxisPicking_ = true;
    revolveAxisPickEditing_ = editingExisting;
    revolveAxisFeatureId_ = editingExisting ? featureId : QString{};
    revolvePendingAxisType_ = type;
    revolvePreviousSelectionMode_ = static_cast<int>(viewer_->selectionMode());
    viewer_->setAxisPickCancelHandler([this]() { cancelRevolveAxisPick(); });
    if (type == cad::parametric::RevolveAxisType::ModelEdge) {
        viewer_->setSelectionMode(CadViewer::SelectionMode::Edge);
    }
    traceActionState("revolve.beginAxisPick", QString("source=%1 axisType=%2 mode=%3")
        .arg(featureId)
        .arg(axisType)
        .arg(static_cast<int>(viewer_->selectionMode())));
    if (type == cad::parametric::RevolveAxisType::SketchLine) {
        const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(
            modeling_.body().findFeature(revolveProfileFeatureId_.toStdString()));
        if (!sketch) {
            cancelRevolveAxisPick();
            statusBar()->showMessage("Revolve Sketch source is unavailable", 3000);
            return;
        }
        viewer_->setSketchLinePickTarget(
            sketch,
            [this](const cad::parametric::SketchEntityId& entityId) {
                handleSketchLineAxisPicked(entityId);
            },
            [this](const std::optional<cad::parametric::SketchEntityId>& entityId) {
                statusBar()->showMessage(entityId
                    ? QString("Axis: Sketch Line | Entity: %1").arg(
                        QString::fromStdString(*entityId))
                    : "Pick a line from the source Sketch");
            });
    }
    statusBar()->showMessage(
        type == cad::parametric::RevolveAxisType::SketchLine
            ? "Pick a line from the source Sketch"
            : "Pick a linear model Edge for the Revolve axis");
}

void MainWindow::cancelRevolveAxisPick()
{
    if (!revolveAxisPicking_) return;
    traceActionState("revolve.endAxisPick", QString("source=%1")
        .arg(revolveProfileFeatureId_));
    revolveRestoringSelectionMode_ = true;
    viewer_->setAxisPickCancelHandler({});
    viewer_->clearSketchLinePickTarget();
    viewer_->setSelectionMode(static_cast<CadViewer::SelectionMode>(
        revolvePreviousSelectionMode_));
    revolveAxisPicking_ = false;
    revolveRestoringSelectionMode_ = false;
    statusBar()->showMessage("Revolve axis pick cancelled", 2000);
}

void MainWindow::handleSketchLineAxisPicked(const cad::parametric::SketchEntityId& entityId)
{
    if (!revolveAxisPicking_) return;
    traceActionState("revolve.sketchLineAccepted", QString("source=%1 entity=%2")
        .arg(revolveProfileFeatureId_)
        .arg(QString::fromStdString(entityId)));
    cad::parametric::RevolveAxisDefinition axis;
    axis.type = cad::parametric::RevolveAxisType::SketchLine;
    axis.sketchFeatureId = revolveProfileFeatureId_.toStdString();
    axis.sketchLineId = entityId;

    cad::application::ModelingResult result;
    if (revolveAxisPickEditing_) {
        result = modeling_.updateRevolveAxis(
            revolveAxisFeatureId_.toStdString(), std::move(axis));
        if (result.success) operationSession_.commit();
        else cancelInteractiveSession("Revolve axis edit failed");
    } else {
        const cad::application::SelectionSnapshot profileSelection{{
            {revolveProfileFeatureId_.toStdString(),
                cad::application::SelectionKind::Object, std::nullopt}}};
        result = modeling_.createRevolve(
            profileSelection, std::move(axis), revolvePendingAngleDegrees_);
        if (!result.success) cancelOperation(); else commitOperation();
    }
    traceActionState("revolve.commitResult", QString("success=%1 id=%2 axisType=SketchLine entity=%3")
        .arg(result.success)
        .arg(QString::fromStdString(result.id))
        .arg(QString::fromStdString(entityId)));
    reportResult(result);
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
        else cancelInteractiveSession("Revolve axis edit failed");
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
    cancelInteractiveSession("clear document");
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
    cancelInteractiveSession("new project");
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
    cancelInteractiveSession("load project");
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
    if (saveDocument()) {
        saveWindowLayout();
        event->accept();
    }
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
