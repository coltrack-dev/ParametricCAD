#include "operations/CylinderFeature.h"

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
