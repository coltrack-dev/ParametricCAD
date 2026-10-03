#pragma once

#include "operations/ParametricFeatures.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>

#include <string>
#include <vector>

namespace cad::operations {

struct SketchProfile
{
    TopoDS_Wire wire;
    TopoDS_Face face;
    std::vector<TopoDS_Face> faces;
};

class SketchProfileBuilder final
{
public:
    static SketchProfile build(const cad::parametric::SketchFeature& sketch);
};

} // namespace cad::operations
