#pragma once

#include "application/ModelingController.h"

#include <QString>

namespace cad::application {

class ProjectController final
{
public:
    explicit ProjectController(ModelingController& modeling);

    bool save(const QString& path, QString& error);
    bool open(const QString& path, QString& error);
    void newProject();
    bool isDirty() const noexcept;

private:
    ModelingController& modeling_;
};

} // namespace cad::application
