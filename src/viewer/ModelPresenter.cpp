#include "viewer/ModelPresenter.h"

#include "model/FeatureVisibility.h"
#include "viewer/CadViewer.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(pcadPresenterLog, "parametric.presenter")

cad::viewer::ModelPresenter::ModelPresenter(cad::parametric::Body& body, CadViewer& viewer)
    : body_(body), viewer_(viewer)
{
}

cad::viewer::PresentationResult cad::viewer::ModelPresenter::refresh()
{
    qCDebug(pcadPresenterLog) << "refresh begin features"
                              << static_cast<qulonglong>(body_.features().size());
    PresentationResult result;
    result.rebuilt = body_.recompute();
    if (!result.rebuilt) result.error = body_.lastError();
    for (const auto& feature : body_.features()) {
        qCDebug(pcadPresenterLog) << "present feature"
                                  << QString::fromStdString(feature->id())
                                  << "ptr" << static_cast<const void*>(feature.get())
                                  << "state" << static_cast<int>(feature->state())
                                  << "shapeNull" << feature->shape().IsNull();
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
    qCDebug(pcadPresenterLog) << "refresh complete rebuilt" << result.rebuilt
                              << "presented" << static_cast<qulonglong>(result.presentedIds.size());
    return result;
}

void cad::viewer::ModelPresenter::clear()
{
    viewer_.clear();
}
