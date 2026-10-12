#pragma once

#include "cae/Mesh.h"

#include <TopoDS_Shape.hxx>

#include <atomic>
#include <string>

namespace cad::cae {

struct MeshGenerationResult {
    bool success{false};
    VolumeMesh mesh;
    MeshStatistics statistics;
    std::string diagnostic;
};

class IMeshGenerator
{
public:
    virtual ~IMeshGenerator() = default;
    virtual MeshGenerationResult generate(const TopoDS_Shape& shape,
                                          const MeshSettings& settings,
                                          const std::atomic_bool* cancel = nullptr) = 0;
};

} // namespace cad::cae
