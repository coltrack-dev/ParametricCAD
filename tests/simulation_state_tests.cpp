#include "application/SimulationController.h"
#include "model/Body.h"
#include "operations/ParametricFeatures.h"

#include <cassert>
#include <memory>

namespace {

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
