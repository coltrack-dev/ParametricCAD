#pragma once

#include <TopoDS_Shape.hxx>

#include <memory>
#include <string>
#include <vector>

namespace cad::parametric {

enum class FeatureState
{
    Dirty,
    UpToDate,
    Failed
};

/**
 * Base class for the new parametric modeling layer.
 *
 * It remains next to the legacy Feature class for source and file compatibility;
 * the application runtime uses ParametricFeature for canonical model features.
 */
class ParametricFeature
{
public:
    using Ptr = std::shared_ptr<ParametricFeature>;

    ParametricFeature(std::string id, std::string name);
    virtual ~ParametricFeature() = default;

    const std::string& id() const noexcept;
    const std::string& name() const noexcept;
    void setName(std::string name);

    FeatureState state() const noexcept;
    bool isDirty() const noexcept;
    const std::string& error() const noexcept;

    const TopoDS_Shape& shape() const noexcept;

    const std::vector<std::weak_ptr<ParametricFeature>>& dependencies() const noexcept;
    void addDependency(const Ptr& dependency);

    void markDirty() noexcept;
    bool recompute();

protected:
    virtual TopoDS_Shape build() const = 0;

private:
    std::string id_;
    std::string name_;
    TopoDS_Shape shape_;
    FeatureState state_{FeatureState::Dirty};
    std::string error_;
    std::vector<std::weak_ptr<ParametricFeature>> dependencies_;
};

} // namespace cad::parametric
