#include "model/ParametricFeature.h"

#include <stdexcept>
#include <utility>

namespace cad::parametric {

ParametricFeature::ParametricFeature(
    std::string id,
    std::string name
)
    : id_(std::move(id)),
      name_(std::move(name))
{
    if (id_.empty()) {
        throw std::invalid_argument("Feature id must not be empty");
    }
}

const std::string& ParametricFeature::id() const noexcept
{
    return id_;
}

const std::string& ParametricFeature::name() const noexcept
{
    return name_;
}

void ParametricFeature::setName(std::string name)
{
    name_ = std::move(name);
}

FeatureState ParametricFeature::state() const noexcept
{
    return state_;
}

bool ParametricFeature::isDirty() const noexcept
{
    return state_ == FeatureState::Dirty;
}

const std::string& ParametricFeature::error() const noexcept
{
    return error_;
}

const TopoDS_Shape& ParametricFeature::shape() const noexcept
{
    return shape_;
}

const std::vector<std::weak_ptr<ParametricFeature>>&
ParametricFeature::dependencies() const noexcept
{
    return dependencies_;
}

void ParametricFeature::addDependency(const Ptr& dependency)
{
    if (!dependency) {
        throw std::invalid_argument("Feature dependency must not be null");
    }

    dependencies_.push_back(dependency);
    markDirty();
}

void ParametricFeature::markDirty() noexcept
{
    state_ = FeatureState::Dirty;
    error_.clear();
}

bool ParametricFeature::recompute()
{
    try {
        for (const std::weak_ptr<ParametricFeature>& weakDependency : dependencies_) {
            const Ptr dependency = weakDependency.lock();

            if (!dependency) {
                throw std::runtime_error("Feature dependency no longer exists");
            }

            if (dependency->state() == FeatureState::Failed) {
                throw std::runtime_error(
                    "Dependency '" + dependency->name() + "' is in failed state"
                );
            }
        }

        TopoDS_Shape rebuiltShape = build();
        if (rebuiltShape.IsNull()) {
            throw std::runtime_error("Feature returned a null shape");
        }

        shape_ = std::move(rebuiltShape);
        state_ = FeatureState::UpToDate;
        error_.clear();
        return true;
    } catch (const std::exception& exception) {
        shape_.Nullify();
        state_ = FeatureState::Failed;
        error_ = exception.what();
        return false;
    }
}

} // namespace cad::parametric
