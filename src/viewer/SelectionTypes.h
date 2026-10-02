#pragma once

#include <AIS_InteractiveObject.hxx>
#include <QString>
#include <TopoDS_Shape.hxx>

#include <optional>

namespace cad::viewer {

enum class SelectionMode
{
    Object,
    Edge,
    Face
};

enum class SelectionKind
{
    Unknown,
    Object,
    Vertex,
    Edge,
    Face
};

struct SelectionItem
{
    QString featureId;
    SelectionKind kind{SelectionKind::Unknown};

    // This is an index in the current presentation shape only. It is not a
    // persistent topology identifier and must not survive a recompute.
    std::optional<int> currentSubshapeIndex;

    bool isValid() const noexcept
    {
        return !featureId.isEmpty() && kind != SelectionKind::Unknown;
    }

    friend bool operator==(const SelectionItem& left, const SelectionItem& right)
    {
        return left.featureId == right.featureId
            && left.kind == right.kind
            && left.currentSubshapeIndex == right.currentSubshapeIndex;
    }
};

struct SelectionHit
{
    SelectionItem item;
    TopoDS_Shape shape;
    Handle(AIS_InteractiveObject) presentation;

    bool isValid() const noexcept
    {
        return item.isValid() && !shape.IsNull() && !presentation.IsNull();
    }

    bool hasSubshape() const noexcept
    {
        return item.kind == SelectionKind::Vertex
            || item.kind == SelectionKind::Edge
            || item.kind == SelectionKind::Face;
    }

    // Hover identity is intentionally limited to the current presentation
    // and subshape occurrence. It is not a persistent topology identity.
    bool hasSameTransientIdentity(const SelectionHit& other) const noexcept
    {
        return item == other.item;
    }
};

} // namespace cad::viewer
