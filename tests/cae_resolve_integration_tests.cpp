#include "application/ModelingController.h"
#include "application/SimulationController.h"
#include "cae/ElectrostaticSolver.h"
#include "model/TopologicalReference.h"

#include <BRepCheck_Analyzer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs.hxx>
#include <QCoreApplication>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

struct EndFaces {
    cad::topology::TopologicalReference minimum;
    cad::topology::TopologicalReference maximum;
};

EndFaces findXEndFaces(const std::string& featureId, const TopoDS_Shape& shape)
{
    EndFaces result;
    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const auto reference = cad::topology::TopologicalSignatureBuilder::createReference(
            featureId, shape, explorer.Current());
        const auto* signature = std::get_if<cad::topology::FaceSignature>(
            &*reference.signature);
        assert(signature != nullptr);
        if (signature->centroid.X() < minimum) {
            minimum = signature->centroid.X();
            result.minimum = reference;
        }
        if (signature->centroid.X() > maximum) {
            maximum = signature->centroid.X();
            result.maximum = reference;
        }
    }
    assert(minimum < maximum);
    return result;
}

double meanFieldComponent(
    const cad::cae::ElectrostaticResult& result, const std::size_t component)
{
    assert(!result.elementElectricField.empty());
    double sum = 0.0;
    for (const auto& field : result.elementElectricField) sum += field[component];
    return sum / static_cast<double>(result.elementElectricField.size());
}

void assertSolved(
    const cad::application::SimulationController& simulation,
    const double expectedEx)
{
    const auto& study = simulation.study();
    assert(study.status == cad::application::SimulationStatus::Ready);
    assert(study.solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Ready);
    assert(study.solvedMeshRevision == study.meshRevision);
    assert(study.electrostaticResult.convergenceStatus
        == cad::cae::ElectrostaticConvergenceStatus::Converged);
    const auto ex = meanFieldComponent(study.electrostaticResult, 0);
    assert(std::abs(ex - expectedEx) < std::abs(expectedEx) * 0.10);
    assert(std::abs(meanFieldComponent(study.electrostaticResult, 1))
        < std::abs(expectedEx) * 0.10);
    assert(std::abs(meanFieldComponent(study.electrostaticResult, 2))
        < std::abs(expectedEx) * 0.10);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const char* executable = std::getenv("PARAMETRIC_CAD_GMSH_EXECUTABLE");
    auto generator = std::make_unique<cad::cae::GmshMeshGenerator>(
        executable ? QString::fromLocal8Bit(executable) : QStringLiteral("gmsh"));
    cad::application::SimulationController simulation(std::move(generator));
    cad::application::ModelingController modeling;

    const auto created = modeling.createBox();
    assert(created.success);
    const auto boxId = created.id;
    assert(modeling.setFeatureProperty(boxId, "depth", 50.0).success);
    assert(modeling.setFeatureProperty(boxId, "height", 30.0).success);
    auto box = modeling.body().findFeature(boxId);
    assert(box && box->shape().ShapeType() == TopAbs_SOLID);
    assert(BRepCheck_Analyzer(box->shape()).IsValid());

    const auto settings = cad::cae::MeshSettings{5.0, 15.0};
    simulation.study().sourceFeatureId = boxId;
    simulation.study().geometryRevision = box->geometryRevision();
    const auto firstMesh = simulation.generate(box->shape(), boxId, settings);
    assert(firstMesh.success);
    assert(simulation.study().status == cad::application::SimulationStatus::Ready);

    const auto endFaces = findXEndFaces(boxId, box->shape());
    std::string diagnostic;
    assert(simulation.addVoltageBoundary("Ground", 0.0,
        {endFaces.minimum}, diagnostic));
    assert(simulation.addVoltageBoundary("Voltage", 100.0,
        {endFaces.maximum}, diagnostic));
    assert(simulation.study().boundaryConditions.size() == 2);
    for (const auto& boundary : simulation.study().boundaryConditions)
        assert(boundary.valid && !boundary.meshNodes.empty());

    const auto firstRequest = simulation.beginElectrostaticSolve();
    const auto firstResult = cad::cae::solveElectrostatic(
        simulation.study().electrostaticProblem);
    assert(simulation.adoptElectrostaticResult(
        firstResult, simulation.study().meshRevision, firstRequest));
    assertSolved(simulation, -1000.0);

    assert(modeling.setFeatureProperty(boxId, "width", 120.0).success);
    box = modeling.body().findFeature(boxId);
    assert(box);
    simulation.markGeometryChanged(boxId, box->geometryRevision());
    assert(simulation.study().status == cad::application::SimulationStatus::Outdated);
    assert(simulation.study().solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Outdated);

    const auto secondMesh = simulation.generate(box->shape(), boxId, settings);
    assert(secondMesh.success);
    assert(simulation.study().status == cad::application::SimulationStatus::Ready);
    assert(simulation.study().solutionStatus
        == cad::application::ElectrostaticSolutionStatus::Outdated);
    assert(simulation.remapBoundaries(box->shape(), diagnostic));
    assert(diagnostic.empty());
    assert(simulation.study().boundaryConditions.size() == 2);
    for (const auto& boundary : simulation.study().boundaryConditions)
        assert(boundary.valid && !boundary.meshNodes.empty());

    const auto secondRequest = simulation.beginElectrostaticSolve();
    const auto secondResult = cad::cae::solveElectrostatic(
        simulation.study().electrostaticProblem);
    assert(simulation.adoptElectrostaticResult(
        secondResult, simulation.study().meshRevision, secondRequest));
    assertSolved(simulation, -1000.0 * 100.0 / 120.0);
    return 0;
}
