#include "application/SimulationController.h"

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
}

void SimulationController::markGeometryChanged(const std::string& featureId)
{
    if (study_.sourceFeatureId == featureId) study_.invalidate();
}

} // namespace cad::application
