#pragma once

#include "viewer/SelectionTypes.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>

#include <map>
#include <optional>
#include <vector>

namespace cad::viewer {

class OcctSelectionAdapter final
{
public:
    OcctSelectionAdapter(
        const Handle(AIS_InteractiveContext)& context,
        const std::map<QString, Handle(AIS_Shape)>& featureObjects
    );

    std::optional<QString> featureIdFor(
        const Handle(AIS_InteractiveObject)& presentation
    ) const;

    std::vector<SelectionHit> selectedHits() const;

    static SelectionKind kindForShape(const TopoDS_Shape& shape) noexcept;

private:
    std::optional<SelectionHit> makeHit(
        const Handle(AIS_InteractiveObject)& presentation,
        const TopoDS_Shape& selectedShape,
        bool hasSelectedShape
    ) const;

    Handle(AIS_InteractiveContext) context_;
    const std::map<QString, Handle(AIS_Shape)>& featureObjects_;
};

} // namespace cad::viewer
