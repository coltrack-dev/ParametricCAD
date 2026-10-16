#include "viewer/ModelPresenter.h"

#include "viewer/CadViewer.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <cstdio>
#include <cstdlib>

namespace {
bool sweepTraceEnabled()
{
    return std::getenv("PARAMETRICCAD_TRACE_ACTIONS") != nullptr;
}

void traceSweepFeature(const char* event,
                       const cad::parametric::ParametricFeature& feature)
{
    if (!sweepTraceEnabled() || std::string(feature.typeId()) != "Sweep") return;
    Bnd_Box bounds;
    if (!feature.shape().IsNull()) BRepBndLib::Add(feature.shape(), bounds);
    std::fprintf(stderr,
        "[SWEEP] %s feature=%s state=%d error=%s shapeNull=%d shapeType=%d boundsVoid=%d\n",
        event, feature.id().c_str(), static_cast<int>(feature.state()),
        feature.error().c_str(), feature.shape().IsNull() ? 1 : 0,
        feature.shape().IsNull() ? -1 : static_cast<int>(feature.shape().ShapeType()),
        bounds.IsVoid() ? 1 : 0);
    std::fflush(stderr);
}
}

cad::viewer::ModelPresenter::ModelPresenter(
    cad::parametric::Body& body,
    cad::application::VisibilityManager& visibilityManager,
    CadViewer& viewer)
    : body_(body), viewer_(viewer), visibilityManager_(visibilityManager)
{
}

cad::viewer::PresentationResult cad::viewer::ModelPresenter::refreshModel()
{
    const auto selectedTopology = viewer_.captureTopologySelection(body_);
    const auto selection = viewer_.selectionSnapshot();
    std::vector<std::string> selectedObjects = selection.selectedObjectIds();
    std::vector<std::string> selectedComponents;
    for (const auto& item : selection.items) {
        if (item.componentId) selectedComponents.push_back(*item.componentId);
    }
    PresentationResult result;
    result.rebuilt = body_.recompute();
    if (!result.rebuilt) result.error = body_.lastError();
    viewer_.beginBulkUpdate();
    QStringList presentedComponents;
    for (const auto& feature : body_.features()) {
        if (feature->typeId() == std::string("Sweep")) {
            traceSweepFeature("refresh.inspect", *feature);
        }
        const auto sketch = std::dynamic_pointer_cast<cad::parametric::SketchFeature>(feature);
        if (feature->state() != cad::parametric::FeatureState::UpToDate
            || feature->shape().IsNull()
            || (sketch && sketch->entities().empty())) continue;
        const auto candidates = cad::parametric::ComponentResolver::enumerate(
            body_, feature->id());
        if (candidates.size() > 1) {
            // A compound is represented by its component AIS objects only;
            // never display the compound and its children simultaneously.
            for (const auto& candidate : candidates) {
                std::string error;
                const auto resolved = cad::parametric::ComponentResolver::resolve(
                    body_, candidate.id, error);
                if (!resolved) continue;
                const auto component = body_.findComponent(candidate.id);
                const bool visible = !component || component->visible;
                viewer_.updateComponent(resolved->shape,
                    QString::fromStdString(candidate.id),
                    QString::fromStdString(feature->id()));
                if (!visible) viewer_.setComponentVisibility(
                    QString::fromStdString(candidate.id),
                    cad::application::VisibilityMode::Hidden);
                presentedComponents.append(QString::fromStdString(candidate.id));
            }
            continue;
        }
        result.presentedIds.push_back(feature->id());
        traceSweepFeature("refresh.updateFeature", *feature);
        viewer_.updateFeature(feature->shape(), QString::fromStdString(feature->id()));
        if (sketch) {
            viewer_.updateSketchConstructionGeometry(*sketch,
                QString::fromStdString(feature->id()));
        }
    }
    viewer_.retainFeatures([&result]() {
        QStringList ids;
        for (const auto& id : result.presentedIds) ids.append(QString::fromStdString(id));
        return ids;
    }());
    viewer_.retainComponents(presentedComponents);
    visibilityManager_.updateBoundingBoxes(body_);
    applyVisibility();
    viewer_.restoreSelection(body_, selectedTopology, selectedObjects, selectedComponents);
    viewer_.endBulkUpdate();
    return result;
}

void cad::viewer::ModelPresenter::refreshVisibility()
{
    applyVisibility();
}

void cad::viewer::ModelPresenter::applyVisibility()
{
    const auto update = visibilityManager_.evaluate(body_);
    viewer_.applyVisibilityChanges(update.changes);
    if (sweepTraceEnabled()) {
        for (const auto& state : visibilityManager_.projection(body_)) {
            if (state.featureId.empty()) continue;
            const auto feature = body_.findFeature(state.featureId);
            if (feature && feature->typeId() == std::string("Sweep")) {
                std::fprintf(stderr, "[SWEEP] refresh.visibility feature=%s mode=%d changes=%zu\n",
                    state.featureId.c_str(), static_cast<int>(state.mode), update.changes.size());
                std::fflush(stderr);
            }
        }
    }
}

void cad::viewer::ModelPresenter::clear()
{
    viewer_.clear();
    visibilityManager_.clear();
}
