#include "cae/ElectrostaticSolver.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace cad::cae {
namespace {

ElectrostaticResult failure(
    const ElectrostaticConvergenceStatus status, const std::string& diagnostic)
{
    ElectrostaticResult result;
    result.convergenceStatus = status;
    result.diagnostic = diagnostic;
    return result;
}

}

ElectrostaticResult solveElectrostatic(const ElectrostaticProblem& problem)
{
    const auto validation = validateMesh(problem.mesh);
    if (!validation.valid) return failure(
        ElectrostaticConvergenceStatus::InvalidInput, validation.error);
    if (problem.mesh.nodes.empty() || problem.mesh.tetrahedra.empty())
        return failure(ElectrostaticConvergenceStatus::InvalidInput, "Electrostatic mesh is empty");
    if (!std::isfinite(problem.material.relativePermittivity)
        || problem.material.relativePermittivity <= 0.0
        || !std::isfinite(problem.material.chargeDensity))
        return failure(ElectrostaticConvergenceStatus::InvalidInput, "Invalid electrostatic material");
    if (!problem.elementMaterials.empty()
        && problem.elementMaterials.size() != problem.mesh.tetrahedra.size())
        return failure(ElectrostaticConvergenceStatus::InvalidInput,
            "Element material count does not match tetrahedron count");

    std::map<std::size_t, double> prescribed;
    for (const auto& boundary : problem.dirichletBoundaries) {
        if (!std::isfinite(boundary.potential))
            return failure(ElectrostaticConvergenceStatus::InvalidInput,
                "Dirichlet potential is not finite");
        for (const auto node : boundary.nodes) {
            if (node >= problem.mesh.nodes.size())
                return failure(ElectrostaticConvergenceStatus::InvalidInput,
                    "Dirichlet boundary references an invalid node");
            const auto found = prescribed.find(node);
            if (found != prescribed.end() && std::abs(found->second - boundary.potential) > 1.0e-12)
                return failure(ElectrostaticConvergenceStatus::ConflictingBoundaryConditions,
                    "A node has conflicting Dirichlet potentials");
            prescribed[node] = boundary.potential;
        }
    }
    if (prescribed.empty()) return failure(ElectrostaticConvergenceStatus::SingularSystem,
        "At least one Dirichlet node is required to remove the constant-potential nullspace");

    SparseMatrixBuilder fullMatrix(problem.mesh.nodes.size(), problem.mesh.nodes.size());
    std::vector<double> fullLoad(problem.mesh.nodes.size(), 0.0);
    std::vector<Tet4Data> elements;
    elements.reserve(problem.mesh.tetrahedra.size());
    for (std::size_t elementIndex = 0; elementIndex < problem.mesh.tetrahedra.size(); ++elementIndex) {
        const auto computed = computeTet4(problem.mesh, problem.mesh.tetrahedra[elementIndex]);
        if (!computed.valid) return failure(ElectrostaticConvergenceStatus::InvalidInput,
            computed.diagnostic);
        elements.push_back(computed.data);
        const auto material = problem.elementMaterials.empty()
            ? problem.material : problem.elementMaterials[elementIndex];
        if (!std::isfinite(material.relativePermittivity) || material.relativePermittivity <= 0.0
            || !std::isfinite(material.chargeDensity))
            return failure(ElectrostaticConvergenceStatus::InvalidInput,
                "Invalid element electrostatic material");
        const auto localMatrix = tet4Stiffness(computed.data, material.relativePermittivity);
        const auto localLoad = tet4ChargeLoad(computed.data, material.chargeDensity);
        const auto& nodes = problem.mesh.tetrahedra[elementIndex].nodes;
        for (std::size_t row = 0; row < 4; ++row) {
            fullLoad[nodes[row]] += localLoad[row];
            for (std::size_t column = 0; column < 4; ++column)
                fullMatrix.add(nodes[row], nodes[column], localMatrix[row][column]);
        }
    }
    std::vector<std::size_t> freeNodes;
    std::vector<std::size_t> reducedIndex(problem.mesh.nodes.size(), 0);
    for (std::size_t node = 0; node < problem.mesh.nodes.size(); ++node) {
        if (prescribed.find(node) == prescribed.end()) {
            reducedIndex[node] = freeNodes.size();
            freeNodes.push_back(node);
        }
    }
    ElectrostaticResult result;
    result.nodalPotential.assign(problem.mesh.nodes.size(), 0.0);
    for (const auto& [node, potential] : prescribed) result.nodalPotential[node] = potential;
    if (freeNodes.empty()) {
        result.convergenceStatus = ElectrostaticConvergenceStatus::Converged;
        result.relativeResidualHistory = {0.0};
        return result;
    }
    const auto full = fullMatrix.build();
    SparseMatrixBuilder reducedMatrix(freeNodes.size(), freeNodes.size());
    std::vector<double> reducedLoad(freeNodes.size(), 0.0);
    for (std::size_t reducedRow = 0; reducedRow < freeNodes.size(); ++reducedRow) {
        const auto row = freeNodes[reducedRow];
        reducedLoad[reducedRow] = fullLoad[row];
        for (std::size_t entry = full.rowOffsets()[row]; entry < full.rowOffsets()[row + 1]; ++entry) {
            const auto column = full.columnIndices()[entry];
            const auto value = full.values()[entry];
            const auto boundary = prescribed.find(column);
            if (boundary != prescribed.end()) reducedLoad[reducedRow] -= value * boundary->second;
            else reducedMatrix.add(reducedRow, reducedIndex[column], value);
        }
    }
    const auto linear = solveConjugateGradient(reducedMatrix.build(), reducedLoad, problem.solver);
    result.iterations = linear.iterations;
    result.relativeResidual = linear.relativeResidual;
    result.relativeResidualHistory = linear.relativeResidualHistory;
    if (linear.status != ConjugateGradientStatus::Converged) {
        result.convergenceStatus = linear.status == ConjugateGradientStatus::ZeroDiagonal
            || linear.status == ConjugateGradientStatus::NonPositiveDefinite
            ? ElectrostaticConvergenceStatus::SingularSystem
            : ElectrostaticConvergenceStatus::SolverFailure;
        result.diagnostic = linear.diagnostic;
        return result;
    }
    for (std::size_t index = 0; index < freeNodes.size(); ++index)
        result.nodalPotential[freeNodes[index]] = linear.solution[index];
    result.elementElectricField.reserve(elements.size());
    for (std::size_t elementIndex = 0; elementIndex < elements.size(); ++elementIndex) {
        std::array<double, 3> gradient{};
        const auto& nodes = problem.mesh.tetrahedra[elementIndex].nodes;
        for (std::size_t node = 0; node < 4; ++node)
            for (std::size_t coordinate = 0; coordinate < 3; ++coordinate)
                gradient[coordinate] += result.nodalPotential[nodes[node]]
                    * elements[elementIndex].shapeFunctionGradients[node][coordinate];
        result.elementElectricField.push_back({-gradient[0], -gradient[1], -gradient[2]});
    }
    result.convergenceStatus = ElectrostaticConvergenceStatus::Converged;
    result.diagnostic = linear.diagnostic;
    return result;
}

} // namespace cad::cae
