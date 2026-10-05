#pragma once

#include "import/IfcOpenShellAdapter.h"
#include "model/ParametricFeature.h"

#include <QString>

#include <functional>
#include <vector>

namespace cad::parametric { class Body; }

namespace cad::application {

class IfcImporter final
{
public:
    cad::import::IfcImportResult prepare(
        const QString& path,
        cad::import::IfcImportProgressCallback progress = {},
        cad::import::IfcImportCancellation cancellation = {}) const;

    std::vector<cad::parametric::ParametricFeature::Ptr> makeFeatures(
        const cad::import::IfcImportResult& result,
        const QString& path,
        const cad::parametric::Body& body) const;

    cad::import::IfcImportResult importIntoBody(const QString& path,
                                                 cad::parametric::Body& body) const;
};

} // namespace cad::application
