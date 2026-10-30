#include "cae/Mesh.h"

#include <cassert>
#include <cmath>

using cad::cae::MeshNode;
using cad::cae::Tetrahedron;
using cad::cae::VolumeMesh;

int main()
{
    VolumeMesh tetra;
    tetra.nodes = {
        MeshNode{10, 0.0, 0.0, 0.0},
        MeshNode{20, 1.0, 0.0, 0.0},
        MeshNode{30, 0.0, 1.0, 0.0},
        MeshNode{40, 0.0, 0.0, 1.0}};
    tetra.tetrahedra.push_back(Tetrahedron{99, {0, 1, 2, 3}});

    assert(std::abs(cad::cae::tetrahedronSignedVolume(tetra, tetra.tetrahedra[0])
        - 1.0 / 6.0) < 1.0e-12);
    const auto valid = cad::cae::validateMesh(tetra);
    assert(valid.valid);
    assert(cad::cae::meshStatistics(tetra).tetrahedronCount == 1);

    auto degenerate = tetra;
    degenerate.tetrahedra[0].nodes[3] = 2;
    assert(!cad::cae::validateMesh(degenerate).valid);

    auto invalidIndex = tetra;
    invalidIndex.tetrahedra[0].nodes[3] = 99;
    assert(!cad::cae::validateMesh(invalidIndex).valid);
    return 0;
}
