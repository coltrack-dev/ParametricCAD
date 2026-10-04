#include "application/ProjectController.h"

#include "model/ProjectFile.h"

namespace cad::application {

ProjectController::ProjectController(ModelingController& modeling)
    : modeling_(modeling)
{
}

ProjectLoadResult ProjectController::loadProject(
    const QString& path, ProjectLoadProgress progress, bool recompute)
{
    ProjectLoadResult result;
    if (!ProjectFile::load(path, result.document, result.body, result.error,
                           std::move(progress), &result.metrics, recompute)) {
        result.document = {};
        result.body = {};
    }
    return result;
}

bool ProjectController::save(const QString& path, QString& error)
{
    if (!modeling_.body().recompute()) {
        error = QString::fromStdString(modeling_.body().lastError());
        return false;
    }
    if (!ProjectFile::save(path, modeling_.document(), modeling_.body(), error)) {
        return false;
    }
    modeling_.undoStack().setClean();
    return true;
}

bool ProjectController::open(const QString& path, QString& error)
{
    auto result = loadProject(path);
    if (!result.success()) {
        error = result.error;
        return false;
    }
    modeling_.replaceProject(std::move(result.document), std::move(result.body));
    return true;
}

void ProjectController::newProject()
{
    modeling_.replaceProject({}, {});
}

bool ProjectController::isDirty() const noexcept
{
    return !modeling_.undoStack().isClean();
}

} // namespace cad::application
