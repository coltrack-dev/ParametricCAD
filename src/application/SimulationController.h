#pragma once

#include "cae/GmshMeshGenerator.h"

#include <atomic>
#include <memory>
#include <string>

namespace cad::application {

enum class SimulationStatus { NotGenerated, Generating, Ready, Outdated, Failed };

struct SimulationStudy
{
    std::string sourceFeatureId;
    cad::cae::MeshSettings settings;
    SimulationStatus status{SimulationStatus::NotGenerated};
    cad::cae::VolumeMesh mesh;
    cad::cae::MeshStatistics statistics;
    std::string diagnostic;

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
    void markGeometryChanged(const std::string& featureId);

private:
    SimulationStudy study_;
    std::unique_ptr<cad::cae::IMeshGenerator> generator_;
};

} // namespace cad::application
