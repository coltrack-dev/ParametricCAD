#pragma once

#include <TopoDS_Shape.hxx>
#include <string>

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

    virtual void recompute() = 0;

protected:
    void setShape(TopoDS_Shape shape);

private:
    std::string name_;
    TopoDS_Shape shape_;
};
