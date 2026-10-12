#include "cae/GmshMeshGenerator.h"

#include <BRepTools.hxx>
#include <QProcess>
#include <QTemporaryDir>

#include <fstream>
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
                        mesh.surfaceTriangles.push_back({nodes, entity});
                    }
                }
            }
        }
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
