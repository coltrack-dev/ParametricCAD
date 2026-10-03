#pragma once

#include "operations/ParametricFeatures.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>

#include <string>

namespace cad::operations {

struct SketchProfile
{
    TopoDS_Wire wire;
    TopoDS_Face face;
};

class SketchProfileBuilder final
{
public:
    static SketchProfile build(const cad::parametric::SketchFeature& sketch);
};

} // namespace cad::operations
