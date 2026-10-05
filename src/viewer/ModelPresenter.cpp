#include "viewer/ModelPresenter.h"

#include "model/FeatureVisibility.h"
#include "viewer/CadViewer.h"

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
    QStringList hidden;
    for (const auto& id : cad::parametric::hiddenFeatureIds(body_, isolatedFeatureIds_)) {
        hidden.append(QString::fromStdString(id));
    }
    viewer_.setHiddenFeatures(hidden);
}

void cad::viewer::ModelPresenter::clear()
{
    viewer_.clear();
    isolatedFeatureIds_.clear();
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
