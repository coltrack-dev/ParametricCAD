#include "cae/ElectrostaticSolver.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

using cad::cae::ElectrostaticMaterial;
using cad::cae::ElectrostaticProblem;
using cad::cae::ElectrostaticConvergenceStatus;
using cad::cae::MeshNode;
using cad::cae::SparseMatrixBuilder;
using cad::cae::Tetrahedron;
using cad::cae::VolumeMesh;

constexpr double Epsilon = cad::cae::VacuumPermittivity;

void checkNear(const double actual, const double expected, const double tolerance)
{
    assert(std::abs(actual - expected) <= tolerance);
}

void sparseAndCg()
{
    SparseMatrixBuilder builder(2, 2);
    builder.add(0, 0, 2.0);
    builder.add(0, 1, 1.0);
    builder.add(0, 1, 2.0);
    builder.add(1, 0, 1.0);
    builder.add(1, 1, 3.0);
    const auto matrix = builder.build();
    const auto product = matrix.multiply({1.0, 2.0});
    checkNear(product[0], 8.0, 1.0e-12);
    checkNear(product[1], 7.0, 1.0e-12);
    SparseMatrixBuilder spdBuilder(2, 2);
    spdBuilder.add(0, 0, 4.0);
    spdBuilder.add(0, 1, 1.0);
    spdBuilder.add(1, 0, 1.0);
    spdBuilder.add(1, 1, 3.0);
    const auto result = cad::cae::solveConjugateGradient(spdBuilder.build(), {1.0, 2.0});
    assert(result.status == cad::cae::ConjugateGradientStatus::Converged);
    checkNear(result.solution[0], 1.0 / 11.0, 1.0e-10);
    checkNear(result.solution[1], 7.0 / 11.0, 1.0e-10);
    assert(!result.relativeResidualHistory.empty());
}

VolumeMesh boxMesh(const std::size_t xDivisions = 1)
{
    VolumeMesh mesh;
    const auto nodeIndex = [xDivisions](const std::size_t i, const std::size_t j, const std::size_t k) {
        return (k * 2 + j) * (xDivisions + 1) + i;
    };
    for (std::size_t k = 0; k < 2; ++k)
        for (std::size_t j = 0; j < 2; ++j)
            for (std::size_t i = 0; i <= xDivisions; ++i)
                mesh.nodes.push_back({mesh.nodes.size(), 100.0 * i / xDivisions,
                    50.0 * j, 30.0 * k});
    for (std::size_t i = 0; i < xDivisions; ++i) {
        const auto a = nodeIndex(i, 0, 0), b = nodeIndex(i + 1, 0, 0);
        const auto c = nodeIndex(i + 1, 1, 0), d = nodeIndex(i, 1, 0);
        const auto e = nodeIndex(i, 0, 1), f = nodeIndex(i + 1, 0, 1);
        const auto g = nodeIndex(i + 1, 1, 1), h = nodeIndex(i, 1, 1);
        const std::array<std::array<std::size_t, 4>, 6> connectivity{{
            {{a, b, c, g}}, {{a, c, d, g}}, {{a, d, h, g}},
            {{a, h, e, g}}, {{a, e, f, g}}, {{a, f, b, g}}}};
        for (const auto nodes : connectivity)
            mesh.tetrahedra.push_back({mesh.tetrahedra.size(), nodes});
    }
    return mesh;
}

ElectrostaticProblem boxProblem(const std::size_t xDivisions = 1)
{
    ElectrostaticProblem problem;
    problem.mesh = boxMesh(xDivisions);
    problem.material = {1.0, 0.0};
    problem.solver = {1.0e-12, 1000};
    std::vector<std::size_t> left, right;
    for (std::size_t node = 0; node < problem.mesh.nodes.size(); ++node) {
        if (problem.mesh.nodes[node].x == 0.0) left.push_back(node);
        if (problem.mesh.nodes[node].x == 100.0) right.push_back(node);
    }
    problem.dirichletBoundaries = {{"left", left, 0.0}, {"right", right, 100.0}};
    return problem;
}

void tet4AndBox()
{
    const auto mesh = boxMesh();
    const auto element = cad::cae::computeTet4(mesh, mesh.tetrahedra.front());
    assert(element.valid);
    checkNear(element.data.volume, 100.0 * 50.0 * 30.0 * 1.0e-9 / 6.0, 1.0e-18);
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            checkNear(cad::cae::tet4Stiffness(element.data, 2.0)[row][column],
                cad::cae::tet4Stiffness(element.data, 2.0)[column][row], 1.0e-24);

    auto problem = boxProblem(2);
    const auto result = cad::cae::solveElectrostatic(problem);
    assert(result.convergenceStatus == ElectrostaticConvergenceStatus::Converged);
    std::cout << "Box CG iterations=" << result.iterations
              << " relative residual=" << result.relativeResidual << '\n';
    checkNear(result.nodalPotential[1], 50.0, 1.0e-10);
    for (const auto& field : result.elementElectricField) {
        checkNear(field[0], -1000.0, 1.0e-8);
        checkNear(field[1], 0.0, 1.0e-8);
        checkNear(field[2], 0.0, 1.0e-8);
    }
}

void dirichletAndInvalidInput()
{
    auto problem = boxProblem();
    problem.dirichletBoundaries.push_back({"conflict", {0}, 1.0});
    assert(cad::cae::solveElectrostatic(problem).convergenceStatus
        == ElectrostaticConvergenceStatus::ConflictingBoundaryConditions);
    problem = boxProblem();
    problem.mesh.tetrahedra[0].nodes[3] = problem.mesh.tetrahedra[0].nodes[2];
    assert(cad::cae::solveElectrostatic(problem).convergenceStatus
        == ElectrostaticConvergenceStatus::InvalidInput);
}

double refinementError(const std::size_t divisions)
{
    auto problem = boxProblem(divisions);
    const double scale = 1.0e6;
    problem.material.chargeDensity = -2.0 * Epsilon * scale;
    problem.dirichletBoundaries[1].potential = scale * 0.1 * 0.1;
    const auto result = cad::cae::solveElectrostatic(problem);
    assert(result.convergenceStatus == ElectrostaticConvergenceStatus::Converged);
    double error = 0.0;
    for (std::size_t element = 0; element < problem.mesh.tetrahedra.size(); ++element) {
        double centroidX = 0.0;
        for (const auto node : problem.mesh.tetrahedra[element].nodes)
            centroidX += problem.mesh.nodes[node].x * 1.0e-3 / 4.0;
        const auto exactField = -2.0 * scale * centroidX;
        error = std::max(error, std::abs(result.elementElectricField[element][0] - exactField));
    }
    return error;
}

} // namespace

int main()
{
    sparseAndCg();
    tet4AndBox();
    dirichletAndInvalidInput();
    const auto coarse = refinementError(2);
    const auto refined = refinementError(4);
    std::cout << "Refinement errors: coarse=" << coarse << " refined=" << refined << '\n';
    assert(refined < coarse);
    return 0;
}
