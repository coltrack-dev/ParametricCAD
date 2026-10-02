#include "viewer/SelectionAdapter.h"

#include <AIS_Shape.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopoDS.hxx>

#include <cassert>

using cad::viewer::OcctSelectionAdapter;
using cad::viewer::SelectionItem;
using cad::viewer::SelectionKind;

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

    SelectionItem invalid;
    assert(!invalid.isValid());

    auto presentation = Handle(AIS_Shape)(new AIS_Shape(box));
    std::map<QString, Handle(AIS_Shape)> presentations{
        {QStringLiteral("box-1"), presentation}
    };
    Handle(AIS_InteractiveContext) noContext;
    OcctSelectionAdapter adapter(noContext, presentations);
    assert(adapter.featureIdFor(presentation) == QStringLiteral("box-1"));
    assert(!adapter.featureIdFor(Handle(AIS_InteractiveObject){}));
    assert(adapter.selectedHits().empty());

    return 0;
}
