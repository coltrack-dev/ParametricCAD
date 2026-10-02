#include "viewer/SelectionAdapter.h"

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <StdSelect_BRepOwner.hxx>

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

void OcctSelectionAdapter::moveTo(
    const QPoint& position,
    const Handle(V3d_View)& view,
    const bool updateViewer
) const
{
    if (context_.IsNull() || view.IsNull()) return;

    context_->MoveTo(
        position.x(),
        position.y(),
        view,
        updateViewer ? Standard_True : Standard_False
    );
}

void OcctSelectionAdapter::selectDetected(
    const AIS_SelectionScheme scheme
) const
{
    if (context_.IsNull()) return;
    context_->SelectDetected(scheme);
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

std::optional<SelectionHit> OcctSelectionAdapter::detectedHit() const
{
    if (context_.IsNull() || !context_->HasDetected()) return std::nullopt;

    const auto presentation = context_->DetectedInteractive();
    const auto owner = Handle(StdSelect_BRepOwner)::DownCast(
        context_->DetectedOwner());
    const bool hasDetectedShape = !owner.IsNull() && owner->HasShape();
    const TopoDS_Shape detectedShape = hasDetectedShape
        ? owner->Shape()
        : TopoDS_Shape{};
    return makeHit(presentation, detectedShape, hasDetectedShape);
}

bool OcctSelectionAdapter::isValidFaceHit(const SelectionHit& hit)
{
    if (!hit.item.isValid() || hit.item.kind != SelectionKind::Face
        || hit.shape.IsNull() || hit.shape.ShapeType() != TopAbs_FACE
        || hit.presentation.IsNull() || !hit.item.currentSubshapeIndex
        || *hit.item.currentSubshapeIndex <= 0) {
        return false;
    }

    const auto parent = Handle(AIS_Shape)::DownCast(hit.presentation);
    if (parent.IsNull() || parent->Shape().IsNull()) return false;

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(parent->Shape(), TopAbs_FACE, faces);
    const int resolvedIndex = faces.FindIndex(hit.shape);
    return resolvedIndex == *hit.item.currentSubshapeIndex;
}

std::optional<SelectionHit> OcctSelectionAdapter::validatedSelectedFaceHit() const
{
    for (const auto& hit : selectedHits()) {
        if (isValidFaceHit(hit)) return hit;
    }
    return std::nullopt;
}

bool OcctSelectionAdapter::isValidObjectHit(const SelectionHit& hit) const
{
    if (!hit.item.isValid() || hit.shape.IsNull() || hit.presentation.IsNull()) {
        return false;
    }

    // Transform currently uses the parent AIS_Shape even when the selected
    // item is a face or edge. Keep that existing parent-selection semantics.
    const auto parent = Handle(AIS_Shape)::DownCast(hit.presentation);
    if (parent.IsNull() || parent->Shape().IsNull()) return false;

    const auto resolvedId = featureIdFor(hit.presentation);
    return resolvedId && *resolvedId == hit.item.featureId;
}

std::optional<SelectionHit> OcctSelectionAdapter::validatedSelectedObjectHit() const
{
    // Preserve beginTransform()/updateTransformGizmo() semantics: the first
    // item in OCCT selected iteration is the selected parent object.
    const auto hits = selectedHits();
    if (hits.empty() || !isValidObjectHit(hits.front())) return std::nullopt;
    return hits.front();
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
