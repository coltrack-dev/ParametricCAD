#include "viewer/ModelPresenter.h"

#include "model/FeatureVisibility.h"
#include "viewer/CadViewer.h"

#include <map>

cad::viewer::ModelPresenter::ModelPresenter(cad::parametric::Body& body, CadViewer& viewer)
    : body_(body), viewer_(viewer)
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
    std::set<std::string> hiddenIds;
    for (const auto& id : cad::parametric::hiddenFeatureIds(body_, isolatedFeatureIds_)) {
        hiddenIds.insert(id);
    }
    std::map<QString, VisibilityMode> modes;
    for (const auto& feature : body_.features()) {
        const auto& id = feature->id();
        VisibilityMode mode = hiddenIds.contains(id)
            ? VisibilityMode::Hidden : VisibilityMode::Visible;
        if (mode == VisibilityMode::Visible
            && !ghostedSelectionIds_.empty()
            && !ghostedSelectionIds_.contains(id)) {
            mode = VisibilityMode::Ghosted;
        }
        modes.emplace(QString::fromStdString(id), mode);
    }
    viewer_.setFeatureVisibilityModes(modes);
}

void cad::viewer::ModelPresenter::clear()
{
    viewer_.clear();
    isolatedFeatureIds_.clear();
    ghostedSelectionIds_.clear();
}

void cad::viewer::ModelPresenter::setIsolatedFeatures(
    const std::vector<std::string>& featureIds)
{
    isolatedFeatureIds_.clear();
    isolatedFeatureIds_.insert(featureIds.begin(), featureIds.end());
}

void cad::viewer::ModelPresenter::clearIsolation()
{
    isolatedFeatureIds_.clear();
}

bool cad::viewer::ModelPresenter::isolationActive() const noexcept
{
    return !isolatedFeatureIds_.empty();
}

void cad::viewer::ModelPresenter::ghostOthers(
    const std::vector<std::string>& selectedIds)
{
    ghostedSelectionIds_.clear();
    ghostedSelectionIds_.insert(selectedIds.begin(), selectedIds.end());
}

void cad::viewer::ModelPresenter::clearGhosting()
{
    ghostedSelectionIds_.clear();
}
