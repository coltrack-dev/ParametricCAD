#include "application/ProjectController.h"

#include "model/ProjectFile.h"

namespace cad::application {

ProjectController::ProjectController(ModelingController& modeling)
    : modeling_(modeling)
{
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
    Document document;
    cad::parametric::Body body;
    if (!ProjectFile::load(path, document, body, error)) return false;
    modeling_.replaceProject(std::move(document), std::move(body));
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
