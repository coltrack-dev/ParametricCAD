#include "operations/BoxFeature.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <stdexcept>

BoxFeature::BoxFeature(double width, double depth, double height)
    : Feature("Box"),
      width_(width),
      depth_(depth),
      height_(height)
{
}

void BoxFeature::recompute()
{
    if (width_ <= 0.0 || depth_ <= 0.0 || height_ <= 0.0) {
        throw std::invalid_argument("Box dimensions must be positive");
    }

    setShape(BRepPrimAPI_MakeBox(width_, depth_, height_).Shape());
}

double BoxFeature::width() const noexcept { return width_; }
double BoxFeature::depth() const noexcept { return depth_; }
double BoxFeature::height() const noexcept { return height_; }
