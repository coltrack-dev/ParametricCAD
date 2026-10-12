#include "application/SimulationController.h"

#include "model/TopologicalReference.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <map>
#include <thread>
#include <functional>

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
    std::size_t validBoundaries = 0;
    std::size_t dirichletNodes = 0;
    for (const auto& boundary : study.boundaryConditions) {
        if (boundary.valid) ++validBoundaries;
        dirichletNodes += boundary.meshNodes.size();
    }
    std::fprintf(stderr,
        "[SIM] thread=%zu studyId=%s feature=%s mesh=%s->%s solution=%s->%s "
        "geometryRevision=%llu meshRevision=%llu solvedMeshRevision=%llu "
        "boundaries=%zu valid=%zu invalid=%zu dirichletNodes=%zu "
        "pendingSolve=%llu reason=%s\n",
        std::hash<std::thread::id>{}(std::this_thread::get_id()),
        study.studyId.c_str(), study.sourceFeatureId.c_str(), oldMesh,
        meshStatusName(study.status), oldSolution,
        solutionStatusName(study.solutionStatus),
        static_cast<unsigned long long>(study.geometryRevision),
        static_cast<unsigned long long>(study.meshRevision),
        static_cast<unsigned long long>(study.solvedMeshRevision),
        study.boundaryConditions.size(), validBoundaries,
        study.boundaryConditions.size() - validBoundaries, dirichletNodes,
        static_cast<unsigned long long>(study.pendingSolveRequestId), reason);
    std::fflush(stderr);
}

}

SimulationController::SimulationController(std::unique_ptr<cad::cae::IMeshGenerator> generator)
    : generator_(generator ? std::move(generator)
                           : std::make_unique<cad::cae::GmshMeshGenerator>())
{}

std::uint64_t SimulationController::beginMeshGeneration(
    const std::string& featureId, const std::uint64_t geometryRevision)
{
    if (meshGenerationActive()) return 0;
    const auto oldMesh = meshStatusName(study_.status);
    const auto oldSolution = solutionStatusName(study_.solutionStatus);
    study_.sourceFeatureId = featureId;
    study_.geometryRevision = geometryRevision;
    study_.pendingMeshRevision = geometryRevision;
    study_.pendingMeshGenerationId = ++meshGenerationSequence_;
    study_.status = SimulationStatus::Generating;
    if (study_.solutionStatus == ElectrostaticSolutionStatus::Ready
        || study_.solutionStatus == ElectrostaticSolutionStatus::Solving)
        study_.solutionStatus = ElectrostaticSolutionStatus::Outdated;
    study_.pendingSolveRequestId = 0;
    if (study_.solutionStatus != ElectrostaticSolutionStatus::NotSolved)
        study_.electrostaticResult = {};
    study_.solutionDiagnostic.clear();
    traceTransition(study_, "mesh generation started", oldMesh, oldSolution);
    return study_.pendingMeshGenerationId;
}

bool SimulationController::acceptMeshResult(
    const cad::cae::MeshGenerationResult& result, const std::uint64_t geometryRevision)
{
    return acceptMeshResult(result, geometryRevision, study_.pendingMeshGenerationId);
}

bool SimulationController::acceptMeshResult(
    const cad::cae::MeshGenerationResult& result,
    const std::uint64_t geometryRevision,
    const std::uint64_t generationId)
{
    const auto oldMesh = meshStatusName(study_.status);
    const auto oldSolution = solutionStatusName(study_.solutionStatus);
    if (generationId == 0 || generationId != study_.pendingMeshGenerationId) {
        traceTransition(study_, "stale mesh generation request ignored", oldMesh, oldSolution);
        return false;
    }
    if (geometryRevision != study_.geometryRevision
        || geometryRevision != study_.pendingMeshRevision) {
        study_.pendingMeshGenerationId = 0;
        study_.diagnostic = "Mesh generation result belongs to an older geometry revision";
        study_.status = SimulationStatus::Outdated;
        traceTransition(study_, "stale mesh generation result rejected", oldMesh, oldSolution);
        return false;
    }
    if (!result.success) {
        study_.pendingMeshGenerationId = 0;
        study_.diagnostic = result.diagnostic;
        study_.status = result.diagnostic == "Mesh generation cancelled"
            ? SimulationStatus::NotGenerated : SimulationStatus::Failed;
        traceTransition(study_, "mesh generation failed", oldMesh, oldSolution);
        return false;
    }
    study_.pendingMeshGenerationId = 0;
    study_.mesh = result.mesh;
    study_.statistics = result.statistics;
    study_.diagnostic.clear();
    study_.status = SimulationStatus::Ready;
    study_.meshRevision = geometryRevision;
    study_.solvedMeshRevision = 0;
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
    const auto generationId = beginMeshGeneration(featureId, study_.geometryRevision);
    if (generationId == 0) {
        return {false, {}, {}, "Mesh generation is already active for this study"};
    }
    const auto result = generator_->generate(shape, settings, cancel);
    acceptMeshResult(result, study_.geometryRevision, generationId);
    return result;
}

void SimulationController::deleteMesh()
{
    const auto oldMesh = meshStatusName(study_.status);
    const auto oldSolution = solutionStatusName(study_.solutionStatus);
    ++meshGenerationSequence_;
    study_.pendingMeshGenerationId = 0;
    study_.mesh = {};
    study_.statistics = {};
    study_.diagnostic.clear();
    study_.status = SimulationStatus::NotGenerated;
    study_.meshRevision = 0;
    study_.pendingMeshRevision = 0;
    study_.electrostaticProblem.mesh = {};
    study_.electrostaticProblem.dirichletBoundaries.clear();
    for (auto& boundary : study_.boundaryConditions) {
        boundary.meshBoundaryIds.clear();
        boundary.meshNodes.clear();
        boundary.valid = false;
        boundary.diagnostic = "Mesh has not been generated";
    }
    clearElectrostaticResult();
    traceTransition(study_, "mesh deleted", oldMesh, oldSolution);
}

bool SimulationController::meshGenerationActive() const noexcept
{
    return study_.status == SimulationStatus::Generating
        && study_.pendingMeshGenerationId != 0;
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
        study_.pendingSolveRequestId = 0;
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
    study_.solvedMeshRevision = 0;
    study_.pendingSolveRequestId = 0;
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

bool SimulationController::remapBoundaries(
    const TopoDS_Shape& currentShape, std::string& diagnostic)
{
    diagnostic.clear();
    study_.electrostaticProblem.mesh = study_.mesh;
    study_.electrostaticProblem.dirichletBoundaries.clear();

    bool allValid = true;
    std::map<std::size_t, double> prescribed;
    for (auto& boundary : study_.boundaryConditions) {
        boundary.meshBoundaryIds.clear();
        boundary.meshNodes.clear();
        boundary.valid = true;
        boundary.diagnostic.clear();

        std::vector<cad::topology::TopologicalReference> refreshedFaces;
        refreshedFaces.reserve(boundary.faces.size());
        for (const auto& reference : boundary.faces) {
            const auto resolved = cad::topology::TopologicalReferenceResolver::resolveAgainstShape(
                reference, currentShape);
            if (simulationTraceEnabled()) {
                std::fprintf(stderr,
                    "[SIM] boundary-resolve studyId=%s boundaryId=%s status=%d candidates=%zu error=%s\n",
                    study_.studyId.c_str(), boundary.id.c_str(),
                    static_cast<int>(resolved.status),
                    static_cast<std::size_t>(resolved.candidateCount),
                    resolved.error.c_str());
            }
            if (resolved.status != cad::topology::ResolveStatus::Resolved
                || !resolved.shape) {
                boundary.valid = false;
                boundary.diagnostic = resolved.error.empty()
                    ? "Boundary reference unresolved on current CAD geometry"
                    : resolved.error;
                break;
            }
            try {
                const auto refreshed = cad::topology::TopologicalSignatureBuilder::createReference(
                    reference.featureId, currentShape, *resolved.shape);
                const auto mapping = mapFaceToMeshRegion(refreshed, study_.mesh);
                if (simulationTraceEnabled()) {
                    std::fprintf(stderr,
                        "[SIM] boundary-map studyId=%s boundaryId=%s valid=%d region=%d nodes=%zu diagnostic=%s\n",
                        study_.studyId.c_str(), boundary.id.c_str(), mapping.valid ? 1 : 0,
                        mapping.boundaryId, mapping.nodes.size(), mapping.diagnostic.c_str());
                }
                if (!mapping.valid) {
                    boundary.valid = false;
                    boundary.diagnostic = mapping.diagnostic;
                    break;
                }
                refreshedFaces.push_back(refreshed);
                boundary.meshBoundaryIds.push_back(mapping.boundaryId);
                boundary.meshNodes.insert(boundary.meshNodes.end(),
                    mapping.nodes.begin(), mapping.nodes.end());
            } catch (const std::exception& error) {
                boundary.valid = false;
                boundary.diagnostic = error.what();
                break;
            }
        }

        if (boundary.valid) {
            boundary.faces = std::move(refreshedFaces);
            std::sort(boundary.meshBoundaryIds.begin(), boundary.meshBoundaryIds.end());
            boundary.meshBoundaryIds.erase(std::unique(boundary.meshBoundaryIds.begin(),
                boundary.meshBoundaryIds.end()), boundary.meshBoundaryIds.end());
            std::sort(boundary.meshNodes.begin(), boundary.meshNodes.end());
            boundary.meshNodes.erase(std::unique(boundary.meshNodes.begin(),
                boundary.meshNodes.end()), boundary.meshNodes.end());
            if (boundary.meshNodes.empty()) {
                boundary.valid = false;
                boundary.diagnostic = "Boundary mapping produced no mesh nodes";
            }
        }

        if (!boundary.valid) {
            boundary.meshBoundaryIds.clear();
            boundary.meshNodes.clear();
            allValid = false;
            if (diagnostic.empty()) diagnostic = boundary.name + ": " + boundary.diagnostic;
            continue;
        }

        for (const auto node : boundary.meshNodes) {
            const auto [it, inserted] = prescribed.emplace(node, boundary.potential);
            if (!inserted && std::abs(it->second - boundary.potential) > 1.0e-12) {
                boundary.valid = false;
                boundary.diagnostic = "Boundary conditions assign conflicting potentials to one mesh node";
                allValid = false;
                if (diagnostic.empty()) diagnostic = boundary.name + ": " + boundary.diagnostic;
                break;
            }
        }
    }

    for (const auto& boundary : study_.boundaryConditions) {
        if (boundary.valid) study_.electrostaticProblem.dirichletBoundaries.push_back({
            boundary.name, boundary.meshNodes, boundary.potential});
    }
    study_.solutionStatus = ElectrostaticSolutionStatus::Outdated;
    study_.solvedMeshRevision = 0;
    if (simulationTraceEnabled()) {
        std::fprintf(stderr, "[SIM] boundary-remap studyId=%s success=%d diagnostic=%s\n",
            study_.studyId.c_str(), allValid ? 1 : 0, diagnostic.c_str());
        std::fflush(stderr);
    }
    if (!allValid && diagnostic.empty()) diagnostic = "One or more boundary references are invalid";
    return allValid;
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

std::uint64_t SimulationController::beginElectrostaticSolve()
{
    const auto oldMesh = meshStatusName(study_.status);
    const auto oldSolution = solutionStatusName(study_.solutionStatus);
    study_.solutionStatus = ElectrostaticSolutionStatus::Solving;
    study_.solvedMeshRevision = 0;
    study_.pendingSolveRequestId = ++solveRequestSequence_;
    traceTransition(study_, "electrostatic solve started", oldMesh, oldSolution);
    return study_.pendingSolveRequestId;
}

bool SimulationController::adoptElectrostaticResult(
    const cad::cae::ElectrostaticResult& result, const std::uint64_t geometryRevision,
    const std::uint64_t requestId)
{
    const auto oldMesh = meshStatusName(study_.status);
    const auto oldSolution = solutionStatusName(study_.solutionStatus);
    if (requestId != 0 && requestId != study_.pendingSolveRequestId) {
        traceTransition(study_, "stale electrostatic request rejected", oldMesh, oldSolution);
        return false;
    }
    if (geometryRevision != 0
        && (geometryRevision != study_.geometryRevision
            || geometryRevision != study_.meshRevision)) {
        study_.solutionStatus = ElectrostaticSolutionStatus::Outdated;
        study_.pendingSolveRequestId = 0;
        traceTransition(study_, "stale electrostatic result rejected", oldMesh, oldSolution);
        return false;
    }
    study_.electrostaticResult = result;
    study_.solutionDiagnostic = result.diagnostic;
    study_.solutionStatus = result.convergenceStatus
        == cad::cae::ElectrostaticConvergenceStatus::Converged
        ? ElectrostaticSolutionStatus::Ready : ElectrostaticSolutionStatus::Failed;
    study_.solvedMeshRevision = study_.solutionStatus == ElectrostaticSolutionStatus::Ready
        ? study_.meshRevision : 0;
    study_.pendingSolveRequestId = 0;
    traceTransition(study_, "electrostatic solve completed", oldMesh, oldSolution);
    return true;
}

void SimulationController::clearElectrostaticResult()
{
    study_.electrostaticResult = {};
    study_.solutionDiagnostic.clear();
    study_.solutionStatus = ElectrostaticSolutionStatus::NotSolved;
    study_.solvedMeshRevision = 0;
    study_.pendingSolveRequestId = 0;
}

} // namespace cad::application
