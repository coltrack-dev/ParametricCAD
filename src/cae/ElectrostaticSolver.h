#pragma once

#include "cae/SparseMatrix.h"
#include "cae/Tet4.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace cad::cae {

struct ElectrostaticMaterial
{
    double relativePermittivity{1.0};
    double chargeDensity{0.0}; // C/m^3
};

struct DirichletBoundary
{
    std::string name;
    std::vector<std::size_t> nodes;
    double potential{}; // V
};

struct ElectrostaticProblem
{
    VolumeMesh mesh;
    ElectrostaticMaterial material;
    std::vector<ElectrostaticMaterial> elementMaterials;
    std::vector<DirichletBoundary> dirichletBoundaries;
    ConjugateGradientParameters solver;
};

enum class ElectrostaticConvergenceStatus
{
    Converged,
    InvalidInput,
    ConflictingBoundaryConditions,
    SingularSystem,
    SolverFailure
};

struct ElectrostaticResult
{
    std::vector<double> nodalPotential;
    std::vector<std::array<double, 3>> elementElectricField;
    std::size_t iterations{};
    double relativeResidual{};
    std::vector<double> relativeResidualHistory;
    ElectrostaticConvergenceStatus convergenceStatus{
        ElectrostaticConvergenceStatus::InvalidInput};
    std::string diagnostic;
};

ElectrostaticResult solveElectrostatic(const ElectrostaticProblem& problem);

} // namespace cad::cae
