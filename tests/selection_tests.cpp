#include "viewer/SelectionAdapter.h"

#include <AIS_Point.hxx>
#include <AIS_Shape.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <Geom_CartesianPoint.hxx>

#include <cassert>

using cad::viewer::OcctSelectionAdapter;
using cad::viewer::SelectionHit;
using cad::viewer::SelectionItem;
using cad::viewer::SelectionKind;
using cad::viewer::SelectionState;

int main()
{
    const auto box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
    const auto edge = BRepBuilderAPI_MakeEdge(gp_Pnt(0.0, 0.0, 0.0),
                                               gp_Pnt(1.0, 0.0, 0.0)).Edge();

    assert(OcctSelectionAdapter::kindForShape(box) == SelectionKind::Object);
    assert(OcctSelectionAdapter::kindForShape(TopoDS::Edge(edge))
        == SelectionKind::Edge);
    assert(OcctSelectionAdapter::kindForShape({}) == SelectionKind::Unknown);

    SelectionItem face{"box-1", SelectionKind::Face, 3};
    SelectionItem sameFace{"box-1", SelectionKind::Face, 3};
    SelectionItem otherFace{"box-1", SelectionKind::Face, 4};
    assert(face.isValid());
    assert(face == sameFace);
    assert(!(face == otherFace));

    auto presentation = Handle(AIS_Shape)(new AIS_Shape(box));
    SelectionHit faceHit{face, box, presentation};
    SelectionHit replacementPresentationHit{
        sameFace,
        BRepPrimAPI_MakeBox(20.0, 20.0, 30.0).Shape(),
        Handle(AIS_Shape)(new AIS_Shape(box))
    };
    assert(faceHit.hasSameTransientIdentity(replacementPresentationHit));
    assert(faceHit.hasSubshape());
    assert(!faceHit.hasSameTransientIdentity(SelectionHit{otherFace, {}, {}}));

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(box, TopAbs_FACE, faces);
    const auto selectedFace = faces(1);
    SelectionHit validFaceHit{
        SelectionItem{"box-1", SelectionKind::Face, faces.FindIndex(selectedFace)},
        selectedFace,
        presentation
    };
    assert(OcctSelectionAdapter::isValidFaceHit(validFaceHit));
    assert(validFaceHit.item.currentSubshapeIndex == faces.FindIndex(selectedFace));
    assert(!OcctSelectionAdapter::isValidFaceHit(
        SelectionHit{SelectionItem{"box-1", SelectionKind::Object, std::nullopt},
                     box, presentation}));
    assert(!OcctSelectionAdapter::isValidFaceHit(
        SelectionHit{SelectionItem{"box-1", SelectionKind::Edge, 1},
                     edge, presentation}));
    assert(!OcctSelectionAdapter::isValidFaceHit(
        SelectionHit{SelectionItem{"box-1", SelectionKind::Face, 0},
                     selectedFace, presentation}));
    assert(!OcctSelectionAdapter::isValidFaceHit(
        SelectionHit{SelectionItem{"box-1", SelectionKind::Face,
                                   faces.Extent() + 1},
                     selectedFace, presentation}));
    assert(!OcctSelectionAdapter::isValidFaceHit(
        SelectionHit{SelectionItem{"missing", SelectionKind::Face, 1},
                     selectedFace, Handle(AIS_InteractiveObject){}}));

    SelectionHit validObjectHit{
        SelectionItem{"box-1", SelectionKind::Object, std::nullopt},
        box,
        presentation
    };
    assert(!OcctSelectionAdapter::isValidFaceHit(validObjectHit));

    SelectionState state;
    state.hovered = faceHit;
    state.rebuildSelected({faceHit, replacementPresentationHit, faceHit});
    assert(state.selected.size() == 1);
    assert(state.primary && *state.primary == face);
    assert(state.hovered && state.hovered->hasSameTransientIdentity(faceHit));
    state.hovered.reset();
    state.rebuildSelected({});
    assert(state.selected.empty());
    assert(!state.primary);
    assert(!state.hovered);

    SelectionItem invalid;
    assert(!invalid.isValid());

    std::map<QString, Handle(AIS_Shape)> presentations{
        {QStringLiteral("box-1"), presentation}
    };
    Handle(AIS_InteractiveContext) noContext;
    OcctSelectionAdapter adapter(noContext, presentations);
    assert(adapter.featureIdFor(presentation) == QStringLiteral("box-1"));
    assert(!adapter.featureIdFor(Handle(AIS_InteractiveObject){}));
    assert(adapter.selectedHits().empty());
    assert(!adapter.detectedHit());
    assert(!adapter.validatedSelectedFaceHit());
    assert(!adapter.validatedSelectedObjectHit());

    OcctSelectionAdapter objectAdapter(noContext, presentations);
    assert(objectAdapter.featureIdFor(validObjectHit.presentation)
        == validObjectHit.item.featureId);
    assert(objectAdapter.isValidObjectHit(validObjectHit));
    assert(!objectAdapter.isValidObjectHit(
        SelectionHit{SelectionItem{"missing", SelectionKind::Object, std::nullopt},
                     box, presentation}));
    assert(!objectAdapter.isValidObjectHit(
        SelectionHit{SelectionItem{"box-1", SelectionKind::Object, std::nullopt},
                     {}, presentation}));
    assert(!objectAdapter.isValidObjectHit(
        SelectionHit{SelectionItem{"box-1", SelectionKind::Object, std::nullopt},
                     box,
                     Handle(AIS_InteractiveObject)(new AIS_Point(
                         new Geom_CartesianPoint(gp_Pnt(0.0, 0.0, 0.0))))}));

    return 0;
}
