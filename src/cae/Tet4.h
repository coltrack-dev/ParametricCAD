#pragma once

#include "cae/Mesh.h"

#include <array>
#include <string>

namespace cad::cae {

constexpr double VacuumPermittivity = 8.8541878128e-12;

struct Tet4Data
{
    double volume{};
    std::array<std::array<double, 3>, 4> shapeFunctionGradients{};
};

struct Tet4Computation
{
    bool valid{false};
    Tet4Data data;
    std::string diagnostic;
};

Tet4Computation computeTet4(const VolumeMesh& mesh, const Tetrahedron& tetrahedron);
std::array<std::array<double, 4>, 4> tet4Stiffness(
    const Tet4Data& element, double relativePermittivity);
std::array<double, 4> tet4ChargeLoad(
    const Tet4Data& element, double chargeDensity);

} // namespace cad::cae
