#include "application/SelectionResolver.h"

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

namespace cad::application {

namespace {

std::optional<TopAbs_ShapeEnum> shapeTypeFor(const SelectionKind kind)
{
    switch (kind) {
    case SelectionKind::Face: return TopAbs_FACE;
    case SelectionKind::Edge: return TopAbs_EDGE;
    case SelectionKind::Vertex: return TopAbs_VERTEX;
    case SelectionKind::Object: return std::nullopt;
    case SelectionKind::Unknown: return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

std::optional<TopoDS_Shape> SelectionResolver::resolve(
    const SelectionDescriptor& descriptor
) const
{
    if (descriptor.featureId.empty()) return std::nullopt;
    const auto feature = body_.findFeature(descriptor.featureId);
    if (!feature || feature->shape().IsNull()) return std::nullopt;

    if (descriptor.kind == SelectionKind::Object) {
        if (descriptor.subshapeIndex) return std::nullopt;
        return feature->shape();
    }

    const auto type = shapeTypeFor(descriptor.kind);
    if (!type || !descriptor.subshapeIndex || *descriptor.subshapeIndex <= 0) {
        return std::nullopt;
    }

    TopTools_IndexedMapOfShape subshapes;
    TopExp::MapShapes(feature->shape(), *type, subshapes);
    const int index = *descriptor.subshapeIndex;
    if (index > subshapes.Extent()) return std::nullopt;
    return subshapes(index);
}

} // namespace cad::application
