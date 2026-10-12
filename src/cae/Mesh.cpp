#include "cae/Mesh.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace cad::cae {
namespace {

bool validNode(const VolumeMesh& mesh, const std::size_t index)
{
    return index < mesh.nodes.size();
}

double edgeSquared(const MeshNode& a, const MeshNode& b)
{
    const auto dx = a.x - b.x;
    const auto dy = a.y - b.y;
    const auto dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

}

double tetrahedronSignedVolume(const VolumeMesh& mesh, const Tetrahedron& tetrahedron)
{
    const auto& a = mesh.nodes[tetrahedron.nodes[0]];
    const auto& b = mesh.nodes[tetrahedron.nodes[1]];
    const auto& c = mesh.nodes[tetrahedron.nodes[2]];
    const auto& d = mesh.nodes[tetrahedron.nodes[3]];
    const auto abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    const auto acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
    const auto adx = d.x - a.x, ady = d.y - a.y, adz = d.z - a.z;
    return (abx * (acy * adz - acz * ady)
          - aby * (acx * adz - acz * adx)
          + abz * (acx * ady - acy * adx)) / 6.0;
}

MeshValidationResult validateMesh(const VolumeMesh& mesh)
{
    for (const auto& tetrahedron : mesh.tetrahedra) {
        for (const auto index : tetrahedron.nodes) {
            if (!validNode(mesh, index)) return {false, "Tetrahedron node index is out of range"};
        }
        if (std::set<std::size_t>(tetrahedron.nodes.begin(), tetrahedron.nodes.end()).size() != 4)
            return {false, "Tetrahedron contains duplicate node indices"};
        if (std::abs(tetrahedronSignedVolume(mesh, tetrahedron)) <= 1.0e-12)
            return {false, "Tetrahedron has zero volume"};
    }
    for (const auto& triangle : mesh.surfaceTriangles) {
        for (const auto index : triangle.nodes)
            if (!validNode(mesh, index)) return {false, "Surface triangle node index is out of range"};
        if (triangle.nodes[0] == triangle.nodes[1]
            || triangle.nodes[0] == triangle.nodes[2]
            || triangle.nodes[1] == triangle.nodes[2])
            return {false, "Surface triangle contains duplicate node indices"};
    }
    return {true, {}};
}

MeshStatistics meshStatistics(const VolumeMesh& mesh)
{
    MeshStatistics result;
    result.nodeCount = mesh.nodes.size();
    result.tetrahedronCount = mesh.tetrahedra.size();
    if (mesh.tetrahedra.empty()) return result;
    result.minimumQuality = std::numeric_limits<double>::max();
    double qualitySum = 0.0;
    for (const auto& tetrahedron : mesh.tetrahedra) {
        const auto& n = tetrahedron.nodes;
        const auto volume = std::abs(tetrahedronSignedVolume(mesh, tetrahedron));
        result.volume += volume;
        const double edges = edgeSquared(mesh.nodes[n[0]], mesh.nodes[n[1]])
            + edgeSquared(mesh.nodes[n[0]], mesh.nodes[n[2]])
            + edgeSquared(mesh.nodes[n[0]], mesh.nodes[n[3]])
            + edgeSquared(mesh.nodes[n[1]], mesh.nodes[n[2]])
            + edgeSquared(mesh.nodes[n[1]], mesh.nodes[n[3]])
            + edgeSquared(mesh.nodes[n[2]], mesh.nodes[n[3]]);
        const auto quality = edges <= 0.0 ? 0.0 : std::min(1.0,
            6.0 * std::sqrt(2.0) * volume / std::pow(edges / 6.0, 1.5));
        result.minimumQuality = std::min(result.minimumQuality, quality);
        qualitySum += quality;
    }
    result.averageQuality = qualitySum / static_cast<double>(mesh.tetrahedra.size());
    return result;
}

} // namespace cad::cae
