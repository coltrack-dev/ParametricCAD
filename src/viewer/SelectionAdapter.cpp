#include "viewer/SelectionAdapter.h"

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include <utility>

namespace cad::viewer {

OcctSelectionAdapter::OcctSelectionAdapter(
    const Handle(AIS_InteractiveContext)& context,
    const std::map<QString, Handle(AIS_Shape)>& featureObjects
)
    : context_(context), featureObjects_(featureObjects)
{
}

std::optional<QString> OcctSelectionAdapter::featureIdFor(
    const Handle(AIS_InteractiveObject)& presentation
) const
{
    if (presentation.IsNull()) return std::nullopt;

    for (const auto& [id, object] : featureObjects_) {
        if (object == presentation) return id;
    }
    return std::nullopt;
}

SelectionKind OcctSelectionAdapter::kindForShape(
    const TopoDS_Shape& shape
) noexcept
{
    if (shape.IsNull()) return SelectionKind::Unknown;

    switch (shape.ShapeType()) {
    case TopAbs_VERTEX: return SelectionKind::Vertex;
    case TopAbs_EDGE: return SelectionKind::Edge;
    case TopAbs_FACE: return SelectionKind::Face;
    default: return SelectionKind::Object;
    }
}

std::optional<SelectionHit> OcctSelectionAdapter::makeHit(
    const Handle(AIS_InteractiveObject)& presentation,
    const TopoDS_Shape& selectedShape,
    const bool hasSelectedShape
) const
{
    const auto featureId = featureIdFor(presentation);
    const auto selectedObject = Handle(AIS_Shape)::DownCast(presentation);
    if (!featureId || selectedObject.IsNull()) return std::nullopt;

    const TopoDS_Shape& parentShape = selectedObject->Shape();
    if (parentShape.IsNull()) return std::nullopt;

    TopoDS_Shape shape = hasSelectedShape && !selectedShape.IsNull()
        ? selectedShape
        : parentShape;
    const SelectionKind kind = hasSelectedShape
        ? kindForShape(shape)
        : SelectionKind::Object;

    if (kind == SelectionKind::Unknown) return std::nullopt;

    SelectionItem item{*featureId, kind, std::nullopt};
    if (kind != SelectionKind::Object) {
        TopTools_IndexedMapOfShape subshapes;
        TopExp::MapShapes(parentShape, shape.ShapeType(), subshapes);
        const int index = subshapes.FindIndex(shape);
        if (index > 0) item.currentSubshapeIndex = index;
    }

    return SelectionHit{std::move(item), shape, presentation};
}

std::vector<SelectionHit> OcctSelectionAdapter::selectedHits() const
{
    std::vector<SelectionHit> result;
    if (context_.IsNull()) return result;

    for (context_->InitSelected(); context_->MoreSelected(); context_->NextSelected()) {
        const auto presentation = context_->SelectedInteractive();
        const bool hasSelectedShape = context_->HasSelectedShape();
        const TopoDS_Shape selectedShape = hasSelectedShape
            ? context_->SelectedShape()
            : TopoDS_Shape{};
        const auto hit = makeHit(presentation, selectedShape, hasSelectedShape);
        if (hit) result.push_back(*hit);
    }
    return result;
}

} // namespace cad::viewer
