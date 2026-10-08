#pragma once

#include "operations/ParametricFeatures.h"

#include <TopoDS_Wire.hxx>

#include <vector>

namespace cad::operations {

struct SketchPath
{
    TopoDS_Wire wire;
    std::vector<cad::parametric::SketchEntityId> entityIds;
};

class SketchPathBuilder final
{
public:
    // An empty entity list means all non-construction Line/Arc entities.
    static SketchPath build(
        const cad::parametric::SketchFeature& sketch,
        const std::vector<cad::parametric::SketchEntityId>& entityIds = {});
};

} // namespace cad::operations
