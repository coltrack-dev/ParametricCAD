#include "cae/GmshMeshGenerator.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <QCoreApplication>

#include <cassert>
#include <cmath>
#include <cstdlib>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const char* executable = std::getenv("PARAMETRIC_CAD_GMSH_EXECUTABLE");
    cad::cae::GmshMeshGenerator generator(
        executable ? QString::fromLocal8Bit(executable) : QStringLiteral("gmsh"));
    cad::cae::MeshSettings settings;
    settings.minimumElementSize = 5.0;
    settings.maximumElementSize = 15.0;
    const auto result = generator.generate(
        BRepPrimAPI_MakeBox(100.0, 50.0, 30.0).Shape(), settings);
    assert(result.success);
    assert(result.mesh.nodes.size() > 0);
    assert(result.mesh.tetrahedra.size() > 0);
    assert(std::abs(result.statistics.volume - 150000.0) < 150000.0 * 0.05);
    return 0;
}
