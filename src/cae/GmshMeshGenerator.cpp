#include "cae/GmshMeshGenerator.h"

#include <BRepTools.hxx>
#include <QProcess>
#include <QTemporaryDir>

#include <fstream>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace cad::cae {
namespace {

MeshGenerationResult fail(const std::string& message) { return {false, {}, {}, message}; }

bool cancelled(const std::atomic_bool* token) { return token && token->load(std::memory_order_relaxed); }

MeshGenerationResult readMsh41(const QString& path)
{
    std::ifstream input(path.toStdString());
    if (!input) return fail("Could not open Gmsh MSH output");
    VolumeMesh mesh;
    std::string line;
    std::unordered_map<std::size_t, std::size_t> tags;
    while (std::getline(input, line)) {
        if (line == "$Nodes") {
            std::size_t blocks{}, count{}, minTag{}, maxTag{};
            input >> blocks >> count >> minTag >> maxTag;
            mesh.nodes.reserve(count);
            for (std::size_t block = 0; block < blocks; ++block) {
                int dimension{}, entity{};
                std::size_t parametric{}, blockCount{};
                input >> dimension >> entity >> parametric >> blockCount;
                std::vector<std::size_t> blockTags(blockCount);
                for (auto& tag : blockTags) input >> tag;
                for (std::size_t i = 0; i < blockCount; ++i) {
                    double x{}, y{}, z{};
                    input >> x >> y >> z;
                    const auto index = mesh.nodes.size();
                    mesh.nodes.push_back({blockTags[i], x, y, z});
                    tags.emplace(blockTags[i], index);
                    if (parametric) {
                        for (int coordinate = 0; coordinate < dimension; ++coordinate) {
                            double ignored{};
                            input >> ignored;
                        }
                    }
                }
            }
        } else if (line == "$Elements") {
            std::size_t blocks{}, count{}, minTag{}, maxTag{};
            input >> blocks >> count >> minTag >> maxTag;
            for (std::size_t block = 0; block < blocks; ++block) {
                int dimension{}, entity{}, type{};
                std::size_t blockCount{};
                input >> dimension >> entity >> type >> blockCount;
                const int nodeCount = type == 2 ? 3 : type == 4 ? 4 : 0;
                for (std::size_t i = 0; i < blockCount; ++i) {
                    std::size_t elementTag{};
                    input >> elementTag;
                    if (nodeCount == 0) {
                        std::string ignored;
                        std::getline(input, ignored);
                        continue;
                    }
                    std::vector<std::size_t> nodeTags(nodeCount);
                    for (auto& tag : nodeTags) input >> tag;
                    if (type == 4) {
                        std::array<std::size_t, 4> nodes{};
                        for (int n = 0; n < 4; ++n) {
                            const auto found = tags.find(nodeTags[n]);
                            if (found == tags.end()) return fail("MSH references an unknown node tag");
                            nodes[n] = found->second;
                        }
                        mesh.tetrahedra.push_back({elementTag, nodes});
                    } else {
                        std::array<std::size_t, 3> nodes{};
                        for (int n = 0; n < 3; ++n) {
                            const auto found = tags.find(nodeTags[n]);
                            if (found == tags.end()) return fail("MSH references an unknown node tag");
                            nodes[n] = found->second;
                        }
                        mesh.surfaceTriangles.push_back({nodes, entity,
                            std::numeric_limits<std::size_t>::max()});
                    }
                }
            }
        }
    }
    for (auto& triangle : mesh.surfaceTriangles) {
        const std::set<std::size_t> triangleNodes(
            triangle.nodes.begin(), triangle.nodes.end());
        for (std::size_t element = 0; element < mesh.tetrahedra.size(); ++element) {
            const auto& tetraNodes = mesh.tetrahedra[element].nodes;
            const std::set<std::size_t> tetraSet(tetraNodes.begin(), tetraNodes.end());
            if (std::includes(tetraSet.begin(), tetraSet.end(),
                              triangleNodes.begin(), triangleNodes.end())) {
                triangle.elementIndex = element;
                break;
            }
        }
    }
    std::map<int, SurfaceRegion> regions;
    for (const auto& triangle : mesh.surfaceTriangles) {
        const auto& a = mesh.nodes[triangle.nodes[0]];
        const auto& b = mesh.nodes[triangle.nodes[1]];
        const auto& c = mesh.nodes[triangle.nodes[2]];
        const std::array<double, 3> ab{b.x - a.x, b.y - a.y, b.z - a.z};
        const std::array<double, 3> ac{c.x - a.x, c.y - a.y, c.z - a.z};
        const std::array<double, 3> cross{
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0]};
        const double crossLength = std::sqrt(
            cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
        const double area = crossLength / 2.0;
        auto& region = regions[triangle.boundaryId];
        region.boundaryId = triangle.boundaryId;
        region.area += area;
        region.centroid[0] += area * (a.x + b.x + c.x) / 3.0;
        region.centroid[1] += area * (a.y + b.y + c.y) / 3.0;
        region.centroid[2] += area * (a.z + b.z + c.z) / 3.0;
        const double sign = region.normal[0] * cross[0]
            + region.normal[1] * cross[1] + region.normal[2] * cross[2] < 0.0 ? -1.0 : 1.0;
        region.normal[0] += sign * cross[0];
        region.normal[1] += sign * cross[1];
        region.normal[2] += sign * cross[2];
    }
    for (auto& [id, region] : regions) {
        if (region.area > 0.0) {
            for (auto& coordinate : region.centroid) coordinate /= region.area;
        }
        const auto length = std::sqrt(region.normal[0] * region.normal[0]
            + region.normal[1] * region.normal[1] + region.normal[2] * region.normal[2]);
        if (length > 0.0)
            for (auto& coordinate : region.normal) coordinate /= length;
        mesh.surfaceRegions.push_back(region);
    }
    const auto validation = validateMesh(mesh);
    if (!validation.valid) return fail(validation.error);
    const auto statistics = meshStatistics(mesh);
    return {true, std::move(mesh), statistics, {}};
}

}

GmshMeshGenerator::GmshMeshGenerator(QString executable) : executable_(std::move(executable)) {}

MeshGenerationResult GmshMeshGenerator::generate(const TopoDS_Shape& shape,
                                                 const MeshSettings& settings,
                                                 const std::atomic_bool* cancel)
{
    if (shape.IsNull()) return fail("Cannot mesh a null shape");
    if (settings.minimumElementSize <= 0.0 || settings.maximumElementSize < settings.minimumElementSize)
        return fail("Invalid mesh element size range");
    QTemporaryDir directory;
    if (!directory.isValid()) return fail("Could not create a temporary mesh directory");
    const auto brep = directory.filePath(QStringLiteral("input.brep"));
    const auto msh = directory.filePath(QStringLiteral("output.msh"));
    if (!BRepTools::Write(shape, brep.toStdString().c_str())) return fail("Could not write temporary BREP");
    if (cancelled(cancel)) return fail("Mesh generation cancelled");
    QProcess process;
    QStringList arguments{
        QStringLiteral("-3"), brep, QStringLiteral("-format"), QStringLiteral("msh41"),
        QStringLiteral("-o"), msh, QStringLiteral("-clmin"), QString::number(settings.minimumElementSize),
        QStringLiteral("-clmax"), QString::number(settings.maximumElementSize),
        QStringLiteral("-save_all")};
    process.start(executable_, arguments);
    if (!process.waitForStarted(3000)) return fail("Gmsh executable was not found: " + executable_.toStdString());
    while (!process.waitForFinished(100)) {
        if (cancelled(cancel)) {
            process.kill();
            process.waitForFinished();
            return fail("Mesh generation cancelled");
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return fail("Gmsh failed: " + process.readAllStandardError().toStdString());
    return readMsh41(msh);
}

} // namespace cad::cae
