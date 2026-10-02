#include "viewer/ModelPresenter.h"

#include "model/FeatureVisibility.h"
#include "viewer/CadViewer.h"

cad::viewer::ModelPresenter::ModelPresenter(cad::parametric::Body& body, CadViewer& viewer)
    : body_(body), viewer_(viewer)
{
}

cad::viewer::PresentationResult cad::viewer::ModelPresenter::refresh()
{
    PresentationResult result;
    result.rebuilt = body_.recompute();
    if (!result.rebuilt) result.error = body_.lastError();
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
    QStringList hidden;
    for (const auto& id : cad::parametric::hiddenFeatureIds(body_)) {
        hidden.append(QString::fromStdString(id));
    }
    viewer_.setHiddenFeatures(hidden);
    return result;
}

void cad::viewer::ModelPresenter::clear()
{
    viewer_.clear();
}
