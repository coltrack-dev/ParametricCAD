#include "application/SimulationController.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace cad::application {

namespace {

bool simulationTraceEnabled()
{
    return std::getenv("PARAMETRIC_CAD_CAE_TRACE") != nullptr
        || std::getenv("PARAMETRICCAD_TRACE_SIMULATION") != nullptr;
}

const char* meshStatusName(const SimulationStatus status)
{
    switch (status) {
    case SimulationStatus::NotGenerated: return "NotGenerated";
    case SimulationStatus::Generating: return "Generating";
    case SimulationStatus::Ready: return "Ready";
    case SimulationStatus::Outdated: return "Outdated";
    case SimulationStatus::Failed: return "Failed";
    }
    return "Unknown";
}

const char* solutionStatusName(const ElectrostaticSolutionStatus status)
{
    switch (status) {
    case ElectrostaticSolutionStatus::NotSolved: return "NotSolved";
    case ElectrostaticSolutionStatus::Solving: return "Solving";
    case ElectrostaticSolutionStatus::Ready: return "Ready";
    case ElectrostaticSolutionStatus::Outdated: return "Outdated";
    case ElectrostaticSolutionStatus::Failed: return "Failed";
    }
    return "Unknown";
}

void traceTransition(const SimulationStudy& study, const char* reason,
                     const char* oldMesh, const char* oldSolution)
{
    if (!simulationTraceEnabled()) return;
    std::fprintf(stderr,
        "[SIM] study=%p feature=%s mesh=%s->%s solution=%s->%s "
        "geometryRevision=%llu meshRevision=%llu reason=%s\n",
        static_cast<const void*>(&study), study.sourceFeatureId.c_str(), oldMesh,
        meshStatusName(study.status), oldSolution,
        solutionStatusName(study.solutionStatus),
        static_cast<unsigned long long>(study.geometryRevision),
        static_cast<unsigned long long>(study.meshRevision), reason);
    std::fflush(stderr);
}

}

SimulationController::SimulationController()
    : generator_(std::make_unique<cad::cae::GmshMeshGenerator>())
{}

void SimulationController::beginMeshGeneration(
    const std::string& featureId, const std::uint64_t geometryRevision)
{
    const auto oldMesh = meshStatusName(study_.status);
    const auto oldSolution = solutionStatusName(study_.solutionStatus);
    study_.sourceFeatureId = featureId;
    study_.geometryRevision = geometryRevision;
    study_.pendingMeshRevision = geometryRevision;
    study_.status = SimulationStatus::Generating;
    traceTransition(study_, "mesh generation started", oldMesh, oldSolution);
}

bool SimulationController::acceptMeshResult(
    const cad::cae::MeshGenerationResult& result, const std::uint64_t geometryRevision)
{
    const auto oldMesh = meshStatusName(study_.status);
    const auto oldSolution = solutionStatusName(study_.solutionStatus);
    if (!result.success) {
        study_.diagnostic = result.diagnostic;
        study_.status = result.diagnostic == "Mesh generation cancelled"
            ? SimulationStatus::NotGenerated : SimulationStatus::Failed;
        traceTransition(study_, "mesh generation failed", oldMesh, oldSolution);
        return false;
    }
    if (geometryRevision != study_.geometryRevision
        || geometryRevision != study_.pendingMeshRevision) {
        study_.diagnostic = "Mesh generation result belongs to an older geometry revision";
        study_.status = SimulationStatus::Outdated;
        traceTransition(study_, "stale mesh generation result rejected", oldMesh, oldSolution);
        return false;
    }
    study_.mesh = result.mesh;
    study_.statistics = result.statistics;
    study_.diagnostic.clear();
    study_.status = SimulationStatus::Ready;
    study_.meshRevision = geometryRevision;
    setMesh(result.mesh, geometryRevision);
    traceTransition(study_, "mesh generation completed", oldMesh, oldSolution);
    return true;
}

cad::cae::MeshGenerationResult SimulationController::generate(
    const TopoDS_Shape& shape, const std::string& featureId,
    const cad::cae::MeshSettings& settings, const std::atomic_bool* cancel)
{
    study_.sourceFeatureId = featureId;
    study_.settings = settings;
    beginMeshGeneration(featureId, study_.geometryRevision);
    const auto result = generator_->generate(shape, settings, cancel);
    acceptMeshResult(result, study_.geometryRevision);
    return result;
}

void SimulationController::deleteMesh()
{
    study_.mesh = {};
    study_.statistics = {};
    study_.diagnostic.clear();
    study_.status = SimulationStatus::NotGenerated;
    study_.meshRevision = 0;
    study_.pendingMeshRevision = 0;
    study_.electrostaticProblem = {};
    study_.boundaryConditions.clear();
    clearElectrostaticResult();
}

void SimulationController::markGeometryChanged(
    const std::string& featureId, const std::uint64_t geometryRevision)
{
    if (study_.sourceFeatureId == featureId) {
        const auto oldMesh = meshStatusName(study_.status);
        const auto oldSolution = solutionStatusName(study_.solutionStatus);
        const auto revision = geometryRevision == 0
            ? study_.geometryRevision + 1 : geometryRevision;
        if (revision == study_.geometryRevision) return;
        study_.geometryRevision = revision;
        if (study_.status == SimulationStatus::Ready
            && study_.meshRevision != study_.geometryRevision)
            study_.status = SimulationStatus::Outdated;
        if (study_.solutionStatus == ElectrostaticSolutionStatus::Ready
            || study_.solutionStatus == ElectrostaticSolutionStatus::Solving)
            study_.solutionStatus = ElectrostaticSolutionStatus::Outdated;
        traceTransition(study_, "geometry revision changed", oldMesh, oldSolution);
    }
}

void SimulationController::setMesh(const cad::cae::VolumeMesh& mesh,
                                   const std::uint64_t geometryRevision)
{
    if (geometryRevision != 0) {
        study_.geometryRevision = geometryRevision;
        study_.meshRevision = geometryRevision;
    } else {
        study_.meshRevision = study_.geometryRevision;
    }
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
    if (study_.boundaryConditions.size() == oldSize) {
        if (simulationTraceEnabled())
            std::fprintf(stderr, "[SIM] boundary-remove id=%s success=0 count=%zu\n",
                id.c_str(), oldSize);
        return false;
    }
    setMesh(study_.mesh);
    study_.solutionStatus = ElectrostaticSolutionStatus::Outdated;
    if (simulationTraceEnabled())
        std::fprintf(stderr, "[SIM] boundary-remove id=%s success=1 count=%zu->%zu\n",
            id.c_str(), oldSize, study_.boundaryConditions.size());
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

void SimulationController::beginElectrostaticSolve()
{
    const auto oldMesh = meshStatusName(study_.status);
    const auto oldSolution = solutionStatusName(study_.solutionStatus);
    study_.solutionStatus = ElectrostaticSolutionStatus::Solving;
    traceTransition(study_, "electrostatic solve started", oldMesh, oldSolution);
}

bool SimulationController::adoptElectrostaticResult(
    const cad::cae::ElectrostaticResult& result, const std::uint64_t geometryRevision)
{
    const auto oldMesh = meshStatusName(study_.status);
    const auto oldSolution = solutionStatusName(study_.solutionStatus);
    if (geometryRevision != 0
        && (geometryRevision != study_.geometryRevision
            || geometryRevision != study_.meshRevision)) {
        study_.solutionStatus = ElectrostaticSolutionStatus::Outdated;
        traceTransition(study_, "stale electrostatic result rejected", oldMesh, oldSolution);
        return false;
    }
    study_.electrostaticResult = result;
    study_.solutionDiagnostic = result.diagnostic;
    study_.solutionStatus = result.convergenceStatus
        == cad::cae::ElectrostaticConvergenceStatus::Converged
        ? ElectrostaticSolutionStatus::Ready : ElectrostaticSolutionStatus::Failed;
    traceTransition(study_, "electrostatic solve completed", oldMesh, oldSolution);
    return true;
}

void SimulationController::clearElectrostaticResult()
{
    study_.electrostaticResult = {};
    study_.solutionDiagnostic.clear();
    study_.solutionStatus = ElectrostaticSolutionStatus::NotSolved;
}

} // namespace cad::application
