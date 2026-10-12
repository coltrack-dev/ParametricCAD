#include "cae/Tet4.h"

#include <algorithm>
#include <cmath>

namespace cad::cae {
namespace {

double determinant(const double matrix[3][3])
{
    return matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1])
        - matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0])
        + matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
}

}

Tet4Computation computeTet4(const VolumeMesh& mesh, const Tetrahedron& tetrahedron)
{
    Tet4Computation result;
    for (const auto index : tetrahedron.nodes) {
        if (index >= mesh.nodes.size()) {
            result.diagnostic = "Tet4 references a node outside the mesh";
            return result;
        }
    }
    double matrix[4][8]{};
    for (std::size_t row = 0; row < 4; ++row) {
        const auto& node = mesh.nodes[tetrahedron.nodes[row]];
        matrix[row][0] = 1.0;
        matrix[row][1] = node.x * 1.0e-3;
        matrix[row][2] = node.y * 1.0e-3;
        matrix[row][3] = node.z * 1.0e-3;
        matrix[row][4 + row] = 1.0;
    }
    double jacobian[3][3]{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            jacobian[row][column] = matrix[row + 1][column + 1] - matrix[0][column + 1];
    const auto signedDeterminant = determinant(jacobian);
    if (!std::isfinite(signedDeterminant) || std::abs(signedDeterminant) <= 1.0e-24) {
        result.diagnostic = "Tet4 is degenerate";
        return result;
    }
    result.data.volume = std::abs(signedDeterminant) / 6.0;
    for (std::size_t column = 0; column < 4; ++column) {
        double augmented[4][5]{};
        for (std::size_t row = 0; row < 4; ++row) {
            const auto& node = mesh.nodes[tetrahedron.nodes[row]];
            augmented[row][0] = 1.0;
            augmented[row][1] = node.x * 1.0e-3;
            augmented[row][2] = node.y * 1.0e-3;
            augmented[row][3] = node.z * 1.0e-3;
            augmented[row][4] = row == column ? 1.0 : 0.0;
        }
        for (std::size_t pivot = 0; pivot < 4; ++pivot) {
            auto pivotRow = pivot;
            for (std::size_t row = pivot + 1; row < 4; ++row)
                if (std::abs(augmented[row][pivot]) > std::abs(augmented[pivotRow][pivot])) pivotRow = row;
            if (std::abs(augmented[pivotRow][pivot]) <= 1.0e-24) {
                result.diagnostic = "Tet4 shape-function matrix is singular";
                return result;
            }
            for (std::size_t entry = 0; entry < 5; ++entry)
                std::swap(augmented[pivot][entry], augmented[pivotRow][entry]);
            const auto scale = augmented[pivot][pivot];
            for (auto& entry : augmented[pivot]) entry /= scale;
            for (std::size_t row = 0; row < 4; ++row) {
                if (row == pivot) continue;
                const auto factor = augmented[row][pivot];
                for (std::size_t entry = 0; entry < 5; ++entry)
                    augmented[row][entry] -= factor * augmented[pivot][entry];
            }
        }
        result.data.shapeFunctionGradients[column] = {
            augmented[1][4], augmented[2][4], augmented[3][4]};
    }
    result.valid = true;
    return result;
}

std::array<std::array<double, 4>, 4> tet4Stiffness(
    const Tet4Data& element, const double relativePermittivity)
{
    std::array<std::array<double, 4>, 4> result{};
    const auto coefficient = VacuumPermittivity * relativePermittivity * element.volume;
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            result[row][column] = coefficient * (
                element.shapeFunctionGradients[row][0] * element.shapeFunctionGradients[column][0]
                + element.shapeFunctionGradients[row][1] * element.shapeFunctionGradients[column][1]
                + element.shapeFunctionGradients[row][2] * element.shapeFunctionGradients[column][2]);
    return result;
}

std::array<double, 4> tet4ChargeLoad(const Tet4Data& element, const double chargeDensity)
{
    const auto value = chargeDensity * element.volume / 4.0;
    return {value, value, value, value};
}

} // namespace cad::cae
