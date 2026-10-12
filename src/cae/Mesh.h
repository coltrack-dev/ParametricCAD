#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace cad::cae {

struct MeshNode { std::size_t id{}; double x{}; double y{}; double z{}; };
struct Tetrahedron { std::size_t id{}; std::array<std::size_t, 4> nodes{}; };
struct SurfaceTriangle { std::array<std::size_t, 3> nodes{}; int boundaryId{}; };

struct VolumeMesh {
    std::vector<MeshNode> nodes;
    std::vector<Tetrahedron> tetrahedra;
    std::vector<SurfaceTriangle> surfaceTriangles;
};

struct MeshStatistics {
    std::size_t nodeCount{};
    std::size_t tetrahedronCount{};
    double volume{};
    double minimumQuality{};
    double averageQuality{};
};

struct MeshSettings {
    double minimumElementSize{1.0};
    double maximumElementSize{10.0};
};

struct MeshValidationResult {
    bool valid{false};
    std::string error;
};

MeshValidationResult validateMesh(const VolumeMesh& mesh);
MeshStatistics meshStatistics(const VolumeMesh& mesh);
double tetrahedronSignedVolume(const VolumeMesh& mesh, const Tetrahedron& tetrahedron);

} // namespace cad::cae
