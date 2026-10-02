#include "operations/CylinderFeature.h"
#include "operations/ParametricFeatures.h"

#include <BRepPrimAPI_MakeCylinder.hxx>
#include <stdexcept>

CylinderFeature::CylinderFeature(double radius, double height)
    : Feature("Cylinder"),
      radius_(radius),
      height_(height)
{
}

void CylinderFeature::recompute()
{
    if (radius_ <= 0.0 || height_ <= 0.0) {
        throw std::invalid_argument("Cylinder dimensions must be positive");
    }

    setShape(BRepPrimAPI_MakeCylinder(radius_, height_).Shape());
}

std::shared_ptr<cad::parametric::ParametricFeature>
CylinderFeature::toParametricFeature(const std::string& id) const
{
    return std::make_shared<cad::parametric::CylinderParametricFeature>(
        id, radius_, height_);
}
