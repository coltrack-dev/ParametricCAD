#pragma once

#include "application/ElectrostaticBoundaryCondition.h"
#include "cae/GmshMeshGenerator.h"
#include "cae/ElectrostaticSolver.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace cad::application {

enum class SimulationStatus { NotGenerated, Generating, Ready, Outdated, Failed };
enum class ElectrostaticSolutionStatus { NotSolved, Solving, Ready, Outdated, Failed };

struct SimulationStudy
{
    std::string sourceFeatureId;
    cad::cae::MeshSettings settings;
    SimulationStatus status{SimulationStatus::NotGenerated};
    cad::cae::VolumeMesh mesh;
    cad::cae::MeshStatistics statistics;
    std::string diagnostic;
    std::uint64_t geometryRevision{0};
    std::uint64_t meshRevision{0};
    std::uint64_t pendingMeshRevision{0};
    cad::cae::ElectrostaticProblem electrostaticProblem;
    std::vector<ElectrostaticBoundaryCondition> boundaryConditions;
    cad::cae::ElectrostaticResult electrostaticResult;
    ElectrostaticSolutionStatus solutionStatus{ElectrostaticSolutionStatus::NotSolved};
    std::string solutionDiagnostic;

    void invalidate()
    {
        if (status == SimulationStatus::Ready) status = SimulationStatus::Outdated;
    }
};

class SimulationController final
{
public:
    SimulationController();
    SimulationStudy& study() noexcept { return study_; }
    const SimulationStudy& study() const noexcept { return study_; }

    cad::cae::MeshGenerationResult generate(const TopoDS_Shape& shape,
                                             const std::string& featureId,
                                             const cad::cae::MeshSettings& settings,
                                             const std::atomic_bool* cancel = nullptr);
    void deleteMesh();
    void beginMeshGeneration(const std::string& featureId, std::uint64_t geometryRevision);
    bool acceptMeshResult(const cad::cae::MeshGenerationResult& result,
                          std::uint64_t geometryRevision);
    void markGeometryChanged(const std::string& featureId,
                             std::uint64_t geometryRevision = 0);
    void setMesh(const cad::cae::VolumeMesh& mesh,
                 std::uint64_t geometryRevision = 0);
    bool addVoltageBoundary(
        const std::string& name,
        double potential,
        const std::vector<cad::topology::TopologicalReference>& faces,
        std::string& diagnostic);
    bool removeBoundary(const std::string& id);
    bool updateBoundary(const std::string& id, const std::string& name, double potential);
    void setMaterial(const cad::cae::ElectrostaticMaterial& material);
    void beginElectrostaticSolve();
    bool adoptElectrostaticResult(const cad::cae::ElectrostaticResult& result,
                                  std::uint64_t geometryRevision = 0);
    void clearElectrostaticResult();

private:
    SimulationStudy study_;
    std::unique_ptr<cad::cae::IMeshGenerator> generator_;
    std::size_t boundarySequence_{0};
};

} // namespace cad::application
