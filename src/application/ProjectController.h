#pragma once

#include "application/ModelingController.h"
#include "model/ProjectFile.h"

#include <QString>

namespace cad::application {

struct ProjectLoadResult
{
    Document document;
    cad::parametric::Body body;
    QString error;
    ProjectLoadMetrics metrics;

    bool success() const noexcept { return error.isEmpty(); }
};

class ProjectController final
{
public:
    explicit ProjectController(ModelingController& modeling);

    static ProjectLoadResult loadProject(const QString& path,
                                         ProjectLoadProgress progress = {},
                                         bool recompute = true);
    bool save(const QString& path, QString& error);
    bool open(const QString& path, QString& error);
    void newProject();
    bool isDirty() const noexcept;

private:
    ModelingController& modeling_;
};

} // namespace cad::application
