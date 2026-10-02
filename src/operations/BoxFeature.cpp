#include "operations/BoxFeature.h"
#include "operations/ParametricFeatures.h"

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

std::shared_ptr<cad::parametric::ParametricFeature>
BoxFeature::toParametricFeature(const std::string& id) const
{
    return std::make_shared<cad::parametric::BoxParametricFeature>(
        id, width_, depth_, height_);
}
