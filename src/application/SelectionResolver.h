#pragma once

#include "application/Selection.h"
#include "model/Body.h"

#include <TopoDS_Shape.hxx>

#include <optional>

namespace cad::application {

class SelectionResolver final
{
public:
    explicit SelectionResolver(const cad::parametric::Body& body) noexcept
        : body_(body)
    {
    }

    std::optional<TopoDS_Shape> resolve(
        const SelectionDescriptor& descriptor
    ) const;

private:
    const cad::parametric::Body& body_;
};

} // namespace cad::application
