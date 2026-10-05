#pragma once

#include "import/IfcOpenShellAdapter.h"

#include <QString>

namespace cad::parametric { class Body; }

namespace cad::application {

class IfcImporter final
{
public:
    cad::import::IfcImportResult importIntoBody(const QString& path,
                                                 cad::parametric::Body& body) const;
};

} // namespace cad::application
