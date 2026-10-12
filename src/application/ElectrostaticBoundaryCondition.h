#pragma once

#include "cae/Mesh.h"
#include "model/TopologicalReference.h"

#include <cstddef>
#include <string>
#include <vector>

namespace cad::application {

enum class ElectrostaticBoundaryType
{
    DirichletVoltage
};

struct ElectrostaticBoundaryCondition
{
    std::string id;
    ElectrostaticBoundaryType type{ElectrostaticBoundaryType::DirichletVoltage};
    std::string name;
    double potential{};
    std::vector<cad::topology::TopologicalReference> faces;
    std::vector<int> meshBoundaryIds;
    std::vector<std::size_t> meshNodes;
    bool valid{false};
    std::string diagnostic;
};

struct MeshFaceMapping
{
    bool valid{false};
    int boundaryId{};
    std::vector<std::size_t> nodes;
    std::string diagnostic;
};

MeshFaceMapping mapFaceToMeshRegion(
    const cad::topology::TopologicalReference& face,
    const cad::cae::VolumeMesh& mesh);

} // namespace cad::application
