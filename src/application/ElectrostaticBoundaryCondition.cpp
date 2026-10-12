#include "application/ElectrostaticBoundaryCondition.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace cad::application {
namespace {

double distance(const gp_Pnt& point, const std::array<double, 3>& centroid)
{
    const auto dx = point.X() - centroid[0];
    const auto dy = point.Y() - centroid[1];
    const auto dz = point.Z() - centroid[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}

MeshFaceMapping mapFaceToMeshRegion(
    const cad::topology::TopologicalReference& face,
    const cad::cae::VolumeMesh& mesh)
{
    MeshFaceMapping result;
    if (face.kind != cad::topology::TopologicalKind::Face || !face.signature) {
        result.diagnostic = "Boundary face has no persistent topology signature";
        return result;
    }
    const auto* signature = std::get_if<cad::topology::FaceSignature>(&*face.signature);
    if (!signature || !std::isfinite(signature->area) || signature->area <= 0.0) {
        result.diagnostic = "Boundary reference is not a valid face signature";
        return result;
    }
    std::vector<const cad::cae::SurfaceRegion*> candidates;
    for (const auto& region : mesh.surfaceRegions) {
        const auto relativeArea = std::abs(region.area - signature->area)
            / std::max(region.area, signature->area);
        const auto centroidTolerance = std::max(1.0e-4, std::sqrt(signature->area) * 1.0e-4);
        if (relativeArea > 1.0e-3 || distance(signature->centroid, region.centroid) > centroidTolerance)
            continue;
        if (signature->normal) {
            const auto& normal = *signature->normal;
            const auto dot = std::abs(normal.X() * region.normal[0]
                + normal.Y() * region.normal[1] + normal.Z() * region.normal[2]);
            if (dot < 0.999) continue;
        }
        candidates.push_back(&region);
    }
    if (candidates.size() != 1) {
        result.diagnostic = candidates.empty()
            ? "CAD face did not match a unique mesh surface region"
            : "CAD face matched multiple mesh surface regions";
        return result;
    }
    result.boundaryId = candidates.front()->boundaryId;
    std::set<std::size_t> nodes;
    for (const auto& triangle : mesh.surfaceTriangles) {
        if (triangle.boundaryId != result.boundaryId) continue;
        nodes.insert(triangle.nodes.begin(), triangle.nodes.end());
    }
    if (nodes.empty()) {
        result.diagnostic = "Matched mesh surface region contains no nodes";
        return result;
    }
    result.nodes.assign(nodes.begin(), nodes.end());
    result.valid = true;
    return result;
}

} // namespace cad::application
