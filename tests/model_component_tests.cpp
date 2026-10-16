#include "model/Body.h"
#include "model/ModelComponent.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>

#include <cassert>
#include <memory>

namespace {
class CompoundFeature final : public cad::parametric::ParametricFeature
{
public:
    CompoundFeature()
        : ParametricFeature("walls", "Walls") {}

    const char* typeId() const noexcept override { return "TestCompound"; }
    Ptr clone(std::string) const override { return {}; }

protected:
    TopoDS_Shape build() const override
    {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        builder.Add(compound, BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape());
        builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(20.0, 0.0, 0.0), 5.0, 6.0, 7.0).Shape());
        return compound;
    }
};
}

int main()
{
    cad::parametric::Body body;
    auto feature = std::make_shared<CompoundFeature>();
    body.addFeature(feature);
    assert(body.recompute());

    const auto components = cad::parametric::ComponentResolver::enumerate(body, "walls");
    assert(components.size() == 2);
    body.replaceComponents(components);
    for (const auto& component : components) {
        std::string error;
        const auto resolved = cad::parametric::ComponentResolver::resolve(body, component.id, error);
        assert(resolved.has_value());
        assert(error.empty());
        assert(!resolved->shape.IsNull());
    }
    std::string error;
    assert(!cad::parametric::ComponentResolver::resolve(body, "missing", error));
    assert(error == "Component not found");
    return 0;
}
