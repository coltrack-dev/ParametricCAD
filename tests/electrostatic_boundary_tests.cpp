#include "application/ElectrostaticBoundaryCondition.h"

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>

#include <cassert>
#include <variant>

int main()
{
    const auto original = BRepPrimAPI_MakeBox(100.0, 50.0, 30.0).Shape();
    gp_Trsf transform;
    transform.SetRotation(gp_Ax1(gp_Pnt(20.0, -10.0, 7.0), gp_Dir(0.3, 0.7, 0.2)), 0.4);
    transform.SetTranslationPart(gp_Vec(20.0, -10.0, 7.0));
    const auto transformed = BRepBuilderAPI_Transform(original, transform, true).Shape();
    cad::cae::VolumeMesh mesh;
    for (std::size_t node = 0; node < 18; ++node)
        mesh.nodes.push_back({node, static_cast<double>(node), 0.0, 0.0});
    for (int id = 1; id <= 6; ++id) {
        cad::cae::SurfaceRegion region;
        region.boundaryId = id;
        mesh.surfaceRegions.push_back(region);
        mesh.surfaceTriangles.push_back({
            {static_cast<std::size_t>((id - 1) * 3),
             static_cast<std::size_t>((id - 1) * 3 + 1),
             static_cast<std::size_t>((id - 1) * 3 + 2)}, id});
    }
    int regionId = 0;
    for (TopExp_Explorer explorer(transformed, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const auto face = explorer.Current();
        const auto reference = cad::topology::TopologicalSignatureBuilder::createReference(
            "box", transformed, face);
        const auto* signature = std::get_if<cad::topology::FaceSignature>(
            &*reference.signature);
        assert(signature != nullptr);
        auto& region = mesh.surfaceRegions[static_cast<std::size_t>(regionId++)];
        region.area = signature->area;
        region.centroid = std::array<double, 3>{
            signature->centroid.X(), signature->centroid.Y(), signature->centroid.Z()};
        if (signature->normal) region.normal = std::array<double, 3>{
            signature->normal->X(), signature->normal->Y(), signature->normal->Z()};
        const auto mapping = cad::application::mapFaceToMeshRegion(reference, mesh);
        assert(mapping.valid);
        assert(mapping.boundaryId == region.boundaryId);
    }
    assert(regionId == 6);
    return 0;
}
