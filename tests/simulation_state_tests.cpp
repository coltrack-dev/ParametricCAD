#include "application/SimulationController.h"
#include "cae/MeshGenerator.h"
#include "model/Body.h"
#include "operations/ParametricFeatures.h"

#include <cassert>
#include <memory>
#include <vector>

namespace {

class FakeMeshGenerator final : public cad::cae::IMeshGenerator
{
public:
    FakeMeshGenerator(std::size_t& calls,
                      std::vector<cad::cae::MeshGenerationResult> results)
        : calls_(calls), results_(std::move(results))
    {}

    cad::cae::MeshGenerationResult generate(
        const TopoDS_Shape&, const cad::cae::MeshSettings&,
        const std::atomic_bool*) override
    {
        ++calls_;
        if (results_.empty()) return {false, {}, {}, "No fake result"};
        auto result = results_.front();
        results_.erase(results_.begin());
        return result;
    }

private:
    std::size_t& calls_;
    std::vector<cad::cae::MeshGenerationResult> results_;
};

cad::cae::MeshGenerationResult successfulMesh()
{
    cad::cae::MeshGenerationResult result;
    result.success = true;
    result.statistics.nodeCount = 4;
    result.statistics.tetrahedronCount = 1;
    return result;
}

cad::cae::ElectrostaticResult successfulSolution()
{
    cad::cae::ElectrostaticResult result;
    result.convergenceStatus = cad::cae::ElectrostaticConvergenceStatus::Converged;
    result.relativeResidual = 1.0e-12;
    return result;
}

cad::cae::MeshGenerationResult fakeMeshResult(const std::size_t nodeCount)
{
    cad::cae::MeshGenerationResult result;
    result.success = true;
    for (std::size_t node = 0; node < nodeCount; ++node)
        result.mesh.nodes.push_back({node, static_cast<double>(node), 0.0, 0.0});
    return result;
}

void assertMeshReady(const cad::application::SimulationController& controller)
{
    assert(controller.study().status == cad::application::SimulationStatus::Ready);
    assert(controller.study().meshRevision == controller.study().geometryRevision);
}

void assertSolutionReady(const cad::application::SimulationController& controller)
{
    assert(controller.study().solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Ready);
}

}

int main()
{
    std::size_t fakeCalls = 0;
    cad::application::SimulationController fakeController(
        std::make_unique<FakeMeshGenerator>(fakeCalls, std::vector{
            fakeMeshResult(4), fakeMeshResult(8),
            cad::cae::MeshGenerationResult{false, {}, {}, "fake gmsh failure"}}));
    const auto studyId = fakeController.study().studyId;
    const auto firstGeneration = fakeController.generate(
        TopoDS_Shape{}, "box", {}, nullptr);
    assert(firstGeneration.success);
    assert(fakeCalls == 1);
    assert(fakeController.study().status == cad::application::SimulationStatus::Ready);
    assert(fakeController.study().studyId == studyId);
    const auto secondGeneration = fakeController.generate(
        TopoDS_Shape{}, "box", {}, nullptr);
    assert(secondGeneration.success);
    assert(fakeCalls == 2);
    assert(fakeController.study().status == cad::application::SimulationStatus::Ready);
    assert(fakeController.study().statistics.nodeCount == 8);
    const auto failedGeneration = fakeController.generate(
        TopoDS_Shape{}, "box", {}, nullptr);
    assert(!failedGeneration.success);
    assert(fakeCalls == 3);
    assert(fakeController.study().status == cad::application::SimulationStatus::Failed);
    assert(fakeController.study().statistics.nodeCount == 8);

    const auto activeGeneration = fakeController.beginMeshGeneration("box", 1);
    assert(activeGeneration != 0);
    assert(fakeController.beginMeshGeneration("box", 1) == 0);
    assert(!fakeController.acceptMeshResult(
        fakeMeshResult(12), 1, activeGeneration - 1));
    assert(fakeController.meshGenerationActive());
    fakeController.deleteMesh();
    assert(!fakeController.acceptMeshResult(fakeMeshResult(16), 1, activeGeneration));
    assert(fakeController.study().status == cad::application::SimulationStatus::NotGenerated);

    fakeController.deleteMesh();
    assert(fakeController.study().status == cad::application::SimulationStatus::NotGenerated);
    assert(fakeController.study().studyId == studyId);
    const auto regenerated = fakeController.generate(
        TopoDS_Shape{}, "box", {}, nullptr);
    assert(!regenerated.success);
    assert(fakeCalls == 4);

    cad::application::SimulationController controller;

    controller.beginMeshGeneration("box", 1);
    const auto firstMeshAccepted = controller.acceptMeshResult(successfulMesh(), 1);
    assert(firstMeshAccepted);
    assertMeshReady(controller);

    controller.beginElectrostaticSolve();
    const auto firstSolutionAccepted = controller.adoptElectrostaticResult(
        successfulSolution(), 1);
    assert(firstSolutionAccepted);
    assertMeshReady(controller);
    assertSolutionReady(controller);

    controller.markGeometryChanged("box", 2);
    assert(controller.study().status == cad::application::SimulationStatus::Outdated);
    assert(controller.study().solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Outdated);

    controller.beginMeshGeneration("box", 2);
    const auto secondMeshAccepted = controller.acceptMeshResult(successfulMesh(), 2);
    assert(secondMeshAccepted);
    assertMeshReady(controller);
    assert(controller.study().solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Outdated);

    controller.beginElectrostaticSolve();
    const auto secondSolutionAccepted = controller.adoptElectrostaticResult(
        successfulSolution(), 2);
    assert(secondSolutionAccepted);
    assertMeshReady(controller);
    assertSolutionReady(controller);

    controller.setMaterial({2.0, 0.0});
    assertMeshReady(controller);
    assert(controller.study().solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Outdated);

    controller.beginMeshGeneration("box", 2);
    assert(controller.acceptMeshResult(successfulMesh(), 2));
    assertMeshReady(controller);
    assert(controller.study().solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Outdated);

    controller.beginElectrostaticSolve();
    const auto thirdSolutionAccepted = controller.adoptElectrostaticResult(
        successfulSolution(), 2);
    assert(thirdSolutionAccepted);
    assertSolutionReady(controller);

    // A visibility-only refresh does not change the revision and therefore
    // cannot invalidate either state.
    controller.markGeometryChanged("box", 2);
    assertMeshReady(controller);
    assertSolutionReady(controller);

    // A result from an older asynchronous generation must not become current.
    controller.beginMeshGeneration("box", 3);
    controller.markGeometryChanged("box", 4);
    const auto staleMeshAccepted = controller.acceptMeshResult(successfulMesh(), 3);
    assert(!staleMeshAccepted);
    assert(controller.study().status == cad::application::SimulationStatus::Outdated);
    assert(controller.study().meshRevision == 2);

    // The revision used by a study belongs to its source feature, not to
    // the whole Body. An unrelated feature must not invalidate the study.
    cad::parametric::Body body;
    auto source = std::make_shared<cad::parametric::BoxParametricFeature>(
        "source", 100.0, 50.0, 30.0);
    auto unrelated = std::make_shared<cad::parametric::BoxParametricFeature>(
        "unrelated", 10.0, 10.0, 10.0);
    body.addFeature(source);
    body.addFeature(unrelated);
    const auto initialRecompute = body.recompute();
    assert(initialRecompute);
    const auto sourceRevision = body.featureGeometryRevision("source");
    cad::application::SimulationController featureController;
    featureController.beginMeshGeneration("source", sourceRevision);
    const auto featureMeshAccepted = featureController.acceptMeshResult(
        successfulMesh(), sourceRevision);
    assert(featureMeshAccepted);
    featureController.beginElectrostaticSolve();
    const auto featureSolutionAccepted = featureController.adoptElectrostaticResult(
        successfulSolution(), sourceRevision);
    assert(featureSolutionAccepted);
    unrelated->setNumericProperty("width", 20.0);
    body.markDirtyFrom(unrelated->id());
    const auto unrelatedRecompute = body.recompute();
    assert(unrelatedRecompute);
    featureController.markGeometryChanged("source", body.featureGeometryRevision("source"));
    assertMeshReady(featureController);
    assertSolutionReady(featureController);

    source->setNumericProperty("width", 120.0);
    body.markDirtyFrom(source->id());
    const auto sourceRecompute = body.recompute();
    assert(sourceRecompute);
    featureController.markGeometryChanged("source", body.featureGeometryRevision("source"));
    assert(featureController.study().status == cad::application::SimulationStatus::Outdated);
    assert(featureController.study().solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Outdated);

    return 0;
}
