#include "model/Feature.h"
#include <utility>

Feature::Feature(std::string name)
    : name_(std::move(name))
{
}

const std::string& Feature::name() const noexcept
{
    return name_;
}

const TopoDS_Shape& Feature::shape() const noexcept
{
    return shape_;
}

void Feature::setShape(TopoDS_Shape shape)
{
    shape_ = std::move(shape);
}
