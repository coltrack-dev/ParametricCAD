#include "application/SimulationController.h"

#include <cassert>

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
    assert(controller.acceptMeshResult(successfulMesh(), 1));
    assertMeshReady(controller);

    controller.beginElectrostaticSolve();
    assert(controller.adoptElectrostaticResult(successfulSolution(), 1));
    assertMeshReady(controller);
    assertSolutionReady(controller);

    controller.markGeometryChanged("box", 2);
    assert(controller.study().status == cad::application::SimulationStatus::Outdated);
    assert(controller.study().solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Outdated);

    controller.beginMeshGeneration("box", 2);
    assert(controller.acceptMeshResult(successfulMesh(), 2));
    assertMeshReady(controller);
    assert(controller.study().solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Outdated);

    controller.beginElectrostaticSolve();
    assert(controller.adoptElectrostaticResult(successfulSolution(), 2));
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
    assert(controller.adoptElectrostaticResult(successfulSolution(), 2));
    assertSolutionReady(controller);

    // A visibility-only refresh does not change the revision and therefore
    // cannot invalidate either state.
    controller.markGeometryChanged("box", 2);
    assertMeshReady(controller);
    assertSolutionReady(controller);

    // A result from an older asynchronous generation must not become current.
    controller.beginMeshGeneration("box", 3);
    controller.markGeometryChanged("box", 4);
    assert(!controller.acceptMeshResult(successfulMesh(), 3));
    assert(controller.study().status == cad::application::SimulationStatus::Outdated);
    assert(controller.study().meshRevision == 2);

    return 0;
}
