#pragma once

#include <TopoDS_Shape.hxx>
#include <memory>
#include <string>

namespace cad::parametric { class ParametricFeature; }

class Feature
{
public:
    explicit Feature(std::string name);
    virtual ~Feature() = default;

    Feature(const Feature&) = delete;
    Feature& operator=(const Feature&) = delete;
    Feature(Feature&&) = default;
    Feature& operator=(Feature&&) = default;

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const TopoDS_Shape& shape() const noexcept;

    virtual std::shared_ptr<cad::parametric::ParametricFeature>
    toParametricFeature(const std::string& id) const = 0;
    virtual const char* legacyIdPrefix() const noexcept = 0;

    virtual void recompute() = 0;

protected:
    void setShape(TopoDS_Shape shape);

private:
    std::string name_;
    TopoDS_Shape shape_;
};
