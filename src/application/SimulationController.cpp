#include "application/SimulationController.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace cad::application {

SimulationController::SimulationController()
    : generator_(std::make_unique<cad::cae::GmshMeshGenerator>())
{}

cad::cae::MeshGenerationResult SimulationController::generate(
    const TopoDS_Shape& shape, const std::string& featureId,
    const cad::cae::MeshSettings& settings, const std::atomic_bool* cancel)
{
    study_.sourceFeatureId = featureId;
    study_.settings = settings;
    study_.status = SimulationStatus::Generating;
    const auto result = generator_->generate(shape, settings, cancel);
    if (result.success) {
        study_.mesh = result.mesh;
        study_.statistics = result.statistics;
        study_.diagnostic.clear();
        study_.status = SimulationStatus::Ready;
        setMesh(result.mesh);
    } else {
        study_.diagnostic = result.diagnostic;
        study_.status = result.diagnostic == "Mesh generation cancelled"
            ? SimulationStatus::NotGenerated : SimulationStatus::Failed;
    }
    return result;
}

void SimulationController::deleteMesh()
{
    study_.mesh = {};
    study_.statistics = {};
    study_.diagnostic.clear();
    study_.status = SimulationStatus::NotGenerated;
    study_.electrostaticProblem = {};
    study_.boundaryConditions.clear();
    clearElectrostaticResult();
}

void SimulationController::markGeometryChanged(const std::string& featureId)
{
    if (study_.sourceFeatureId == featureId) {
        study_.invalidate();
        if (study_.solutionStatus == ElectrostaticSolutionStatus::Ready)
            study_.solutionStatus = ElectrostaticSolutionStatus::Outdated;
    }
}

void SimulationController::setMesh(const cad::cae::VolumeMesh& mesh)
{
    study_.mesh = mesh;
    study_.statistics = cad::cae::meshStatistics(mesh);
    study_.electrostaticProblem.mesh = mesh;
    for (auto& boundary : study_.boundaryConditions) {
        boundary.meshBoundaryIds.clear();
        boundary.meshNodes.clear();
        boundary.valid = true;
        boundary.diagnostic.clear();
        for (const auto& face : boundary.faces) {
            const auto mapping = mapFaceToMeshRegion(face, mesh);
            if (!mapping.valid) {
                boundary.valid = false;
                boundary.diagnostic = mapping.diagnostic;
                break;
            }
            boundary.meshBoundaryIds.push_back(mapping.boundaryId);
            boundary.meshNodes.insert(boundary.meshNodes.end(),
                mapping.nodes.begin(), mapping.nodes.end());
        }
        std::sort(boundary.meshBoundaryIds.begin(), boundary.meshBoundaryIds.end());
        boundary.meshBoundaryIds.erase(std::unique(boundary.meshBoundaryIds.begin(),
            boundary.meshBoundaryIds.end()), boundary.meshBoundaryIds.end());
        std::sort(boundary.meshNodes.begin(), boundary.meshNodes.end());
        boundary.meshNodes.erase(std::unique(boundary.meshNodes.begin(),
            boundary.meshNodes.end()), boundary.meshNodes.end());
    }
    study_.electrostaticProblem.dirichletBoundaries.clear();
    for (const auto& boundary : study_.boundaryConditions) {
        if (boundary.valid) study_.electrostaticProblem.dirichletBoundaries.push_back({
            boundary.name, boundary.meshNodes, boundary.potential});
    }
    if (study_.solutionStatus == ElectrostaticSolutionStatus::Ready)
        study_.solutionStatus = ElectrostaticSolutionStatus::Outdated;
}

bool SimulationController::addVoltageBoundary(
    const std::string& name, const double potential,
    const std::vector<cad::topology::TopologicalReference>& faces,
    std::string& diagnostic)
{
    if (faces.empty() || !std::isfinite(potential)) {
        diagnostic = "Select at least one valid face and provide a finite potential";
        return false;
    }
    ElectrostaticBoundaryCondition boundary;
    boundary.id = "electrostatic-boundary-" + std::to_string(++boundarySequence_);
    boundary.name = name;
    boundary.potential = potential;
    boundary.faces = faces;
    study_.boundaryConditions.push_back(boundary);
    setMesh(study_.mesh);
    const auto& added = study_.boundaryConditions.back();
    if (!added.valid) {
        diagnostic = added.diagnostic;
        study_.boundaryConditions.pop_back();
        setMesh(study_.mesh);
        return false;
    }
    study_.solutionStatus = ElectrostaticSolutionStatus::Outdated;
    return true;
}

bool SimulationController::removeBoundary(const std::string& id)
{
    const auto oldSize = study_.boundaryConditions.size();
    study_.boundaryConditions.erase(std::remove_if(study_.boundaryConditions.begin(),
        study_.boundaryConditions.end(), [&id](const auto& boundary) { return boundary.id == id; }),
        study_.boundaryConditions.end());
    if (study_.boundaryConditions.size() == oldSize) return false;
    setMesh(study_.mesh);
    study_.solutionStatus = ElectrostaticSolutionStatus::Outdated;
    return true;
}

bool SimulationController::updateBoundary(
    const std::string& id, const std::string& name, const double potential)
{
    for (auto& boundary : study_.boundaryConditions) {
        if (boundary.id != id) continue;
        if (name.empty() || !std::isfinite(potential)) return false;
        boundary.name = name;
        boundary.potential = potential;
        setMesh(study_.mesh);
        study_.solutionStatus = ElectrostaticSolutionStatus::Outdated;
        return boundary.valid;
    }
    return false;
}

void SimulationController::setMaterial(const cad::cae::ElectrostaticMaterial& material)
{
    study_.electrostaticProblem.material = material;
    study_.solutionStatus = ElectrostaticSolutionStatus::Outdated;
}

void SimulationController::adoptElectrostaticResult(const cad::cae::ElectrostaticResult& result)
{
    study_.electrostaticResult = result;
    study_.solutionDiagnostic = result.diagnostic;
    study_.solutionStatus = result.convergenceStatus
        == cad::cae::ElectrostaticConvergenceStatus::Converged
        ? ElectrostaticSolutionStatus::Ready : ElectrostaticSolutionStatus::Failed;
}

void SimulationController::clearElectrostaticResult()
{
    study_.electrostaticResult = {};
    study_.solutionDiagnostic.clear();
    study_.solutionStatus = ElectrostaticSolutionStatus::NotSolved;
}

} // namespace cad::application
