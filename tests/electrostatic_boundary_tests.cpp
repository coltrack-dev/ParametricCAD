#include "application/ElectrostaticBoundaryCondition.h"
#include "application/SimulationController.h"

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
    std::vector<cad::topology::TopologicalReference> references;
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
        references.push_back(reference);
    }
    assert(regionId == 6);

    cad::application::SimulationController controller;
    cad::cae::MeshGenerationResult generated;
    generated.success = true;
    generated.mesh = mesh;
    controller.beginMeshGeneration("box", 1);
    const auto meshAccepted = controller.acceptMeshResult(generated, 1);
    assert(meshAccepted);
    std::string diagnostic;
    const auto firstAdded = controller.addVoltageBoundary(
        "A", 100.0, {references[0]}, diagnostic);
    assert(firstAdded);
    const auto secondAdded = controller.addVoltageBoundary(
        "B", 0.0, {references[1]}, diagnostic);
    assert(secondAdded);
    assert(controller.study().boundaryConditions.size() == 2);
    controller.deleteMesh();
    assert(controller.study().status == cad::application::SimulationStatus::NotGenerated);
    assert(controller.study().sourceFeatureId == "box");
    assert(controller.study().boundaryConditions.size() == 2);
    assert(!controller.study().boundaryConditions[0].valid);
    controller.beginMeshGeneration("box", 1);
    const auto remeshAccepted = controller.acceptMeshResult(generated, 1);
    assert(remeshAccepted);
    assert(controller.study().boundaryConditions[0].valid);
    assert(controller.study().boundaryConditions[1].valid);
    controller.beginElectrostaticSolve();
    cad::cae::ElectrostaticResult firstSolved;
    firstSolved.convergenceStatus = cad::cae::ElectrostaticConvergenceStatus::Converged;
    assert(controller.adoptElectrostaticResult(firstSolved, 1));
    assert(controller.study().solvedMeshRevision == 1);

    // A resized box changes the stored centroid/signature, but the semantic
    // Box.Face reference must resolve against the current B-Rep and remap to
    // the new mesh surface region and node set.
    const auto resized = BRepBuilderAPI_Transform(
        BRepPrimAPI_MakeBox(140.0, 50.0, 30.0).Shape(), transform, true).Shape();
    cad::cae::VolumeMesh resizedMesh;
    for (std::size_t node = 0; node < 18; ++node)
        resizedMesh.nodes.push_back({node, static_cast<double>(node + 20), 0.0, 0.0});
    int resizedRegionId = 0;
    for (TopExp_Explorer explorer(resized, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const auto face = explorer.Current();
        const auto reference = cad::topology::TopologicalSignatureBuilder::createReference(
            "box", resized, face);
        const auto* signature = std::get_if<cad::topology::FaceSignature>(
            &*reference.signature);
        assert(signature != nullptr);
        cad::cae::SurfaceRegion region;
        region.boundaryId = ++resizedRegionId;
        region.area = signature->area;
        region.centroid = {signature->centroid.X(), signature->centroid.Y(), signature->centroid.Z()};
        if (signature->normal) region.normal = {
            signature->normal->X(), signature->normal->Y(), signature->normal->Z()};
        resizedMesh.surfaceRegions.push_back(region);
        resizedMesh.surfaceTriangles.push_back({
            {static_cast<std::size_t>((resizedRegionId - 1) * 3),
             static_cast<std::size_t>((resizedRegionId - 1) * 3 + 1),
             static_cast<std::size_t>((resizedRegionId - 1) * 3 + 2)},
            resizedRegionId});
    }
    cad::cae::MeshGenerationResult resizedGeneration;
    resizedGeneration.success = true;
    resizedGeneration.mesh = resizedMesh;
    controller.beginMeshGeneration("box", 2);
    assert(controller.acceptMeshResult(resizedGeneration, 2));
    assert(controller.study().solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Outdated);
    assert(controller.remapBoundaries(resized, diagnostic));
    assert(controller.study().boundaryConditions[0].valid);
    assert(controller.study().boundaryConditions[1].valid);
    assert(controller.study().boundaryConditions[0].meshNodes.size() == 3);
    const auto* remappedSignature = std::get_if<cad::topology::FaceSignature>(
        &*controller.study().boundaryConditions[0].faces[0].signature);
    const auto* originalSignature = std::get_if<cad::topology::FaceSignature>(
        &*references[0].signature);
    assert(remappedSignature && originalSignature);
    assert(controller.study().boundaryConditions[0].faces[0].semanticId
        == references[0].semanticId);

    controller.beginElectrostaticSolve();
    cad::cae::ElectrostaticResult solved;
    solved.convergenceStatus = cad::cae::ElectrostaticConvergenceStatus::Converged;
    assert(controller.adoptElectrostaticResult(solved, controller.study().meshRevision));
    assert(controller.study().solvedMeshRevision == controller.study().meshRevision);

    const auto firstId = controller.study().boundaryConditions.front().id;
    const auto removed = controller.removeBoundary(firstId);
    assert(removed);
    assert(controller.study().boundaryConditions.size() == 1);
    assert(controller.study().boundaryConditions.front().name == "B");
    assert(controller.study().status == cad::application::SimulationStatus::Ready);
    assert(controller.study().solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Outdated);
    const auto removedAgain = controller.removeBoundary(firstId);
    assert(!removedAgain);
    return 0;
}
