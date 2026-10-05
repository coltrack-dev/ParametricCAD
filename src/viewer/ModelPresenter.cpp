#include "viewer/ModelPresenter.h"

#include "viewer/CadViewer.h"

#include <map>

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
    PresentationResult result;
    result.rebuilt = body_.recompute();
    if (!result.rebuilt) result.error = body_.lastError();
    viewer_.beginBulkUpdate();
    for (const auto& feature : body_.features()) {
        if (feature->state() != cad::parametric::FeatureState::UpToDate
            || feature->shape().IsNull()) continue;
        result.presentedIds.push_back(feature->id());
        viewer_.updateFeature(feature->shape(), QString::fromStdString(feature->id()));
    }
    viewer_.retainFeatures([&result]() {
        QStringList ids;
        for (const auto& id : result.presentedIds) ids.append(QString::fromStdString(id));
        return ids;
    }());
    applyVisibility();
    viewer_.restoreSelection(body_, selectedTopology, selectedObjects);
    viewer_.endBulkUpdate();
    return result;
}

void cad::viewer::ModelPresenter::refreshVisibility()
{
    applyVisibility();
}

void cad::viewer::ModelPresenter::applyVisibility()
{
    std::map<QString, cad::application::VisibilityMode> modes;
    for (const auto& state : visibilityManager_.projection(body_)) {
        modes.emplace(QString::fromStdString(state.featureId), state.mode);
    }
    viewer_.setFeatureVisibilityModes(modes);
}

void cad::viewer::ModelPresenter::clear()
{
    viewer_.clear();
    visibilityManager_.clear();
}
