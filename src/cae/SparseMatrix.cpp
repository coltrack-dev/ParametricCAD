#include "cae/SparseMatrix.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace cad::cae {

SparseMatrixCSR::SparseMatrixCSR(
    const std::size_t rows, const std::size_t columns,
    std::vector<std::size_t> rowOffsets,
    std::vector<std::size_t> columnIndices,
    std::vector<double> values)
    : rows_(rows), columns_(columns), rowOffsets_(std::move(rowOffsets)),
      columnIndices_(std::move(columnIndices)), values_(std::move(values))
{
    if (rowOffsets_.size() != rows_ + 1 || columnIndices_.size() != values_.size()
        || rowOffsets_.back() != values_.size()) {
        throw std::invalid_argument("Invalid CSR storage");
    }
}

std::vector<double> SparseMatrixCSR::multiply(const std::vector<double>& vector) const
{
    if (vector.size() != columns_) throw std::invalid_argument("CSR vector size mismatch");
    std::vector<double> result(rows_, 0.0);
    for (std::size_t row = 0; row < rows_; ++row) {
        for (std::size_t entry = rowOffsets_[row]; entry < rowOffsets_[row + 1]; ++entry)
            result[row] += values_[entry] * vector[columnIndices_[entry]];
    }
    return result;
}

double SparseMatrixCSR::diagonal(const std::size_t row) const noexcept
{
    if (row >= rows_) return 0.0;
    for (std::size_t entry = rowOffsets_[row]; entry < rowOffsets_[row + 1]; ++entry)
        if (columnIndices_[entry] == row) return values_[entry];
    return 0.0;
}

bool SparseMatrixCSR::finite() const noexcept
{
    return std::all_of(values_.begin(), values_.end(),
        [](const double value) { return std::isfinite(value); });
}

SparseMatrixBuilder::SparseMatrixBuilder(const std::size_t rows, const std::size_t columns)
    : rows_(rows), columns_(columns)
{}

void SparseMatrixBuilder::add(const std::size_t row, const std::size_t column, const double value)
{
    if (row >= rows_ || column >= columns_) throw std::out_of_range("Sparse triplet index out of range");
    if (!std::isfinite(value)) throw std::invalid_argument("Sparse triplet is not finite");
    triplets_.push_back({row, column, value});
}

SparseMatrixCSR SparseMatrixBuilder::build() const
{
    std::map<std::pair<std::size_t, std::size_t>, double> entries;
    for (const auto& triplet : triplets_) entries[{triplet.row, triplet.column}] += triplet.value;
    std::vector<std::size_t> offsets(rows_ + 1, 0);
    for (const auto& [position, value] : entries) {
        if (value != 0.0) ++offsets[position.first + 1];
    }
    for (std::size_t row = 0; row < rows_; ++row) offsets[row + 1] += offsets[row];
    std::vector<std::size_t> columns(offsets.back());
    std::vector<double> values(offsets.back());
    auto cursor = offsets;
    for (const auto& [position, value] : entries) {
        if (value == 0.0) continue;
        const auto index = cursor[position.first]++;
        columns[index] = position.second;
        values[index] = value;
    }
    return {rows_, columns_, std::move(offsets), std::move(columns), std::move(values)};
}

double dotProduct(const std::vector<double>& left, const std::vector<double>& right)
{
    if (left.size() != right.size()) throw std::invalid_argument("Vector size mismatch");
    return std::inner_product(left.begin(), left.end(), right.begin(), 0.0);
}

double vectorNorm(const std::vector<double>& vector)
{
    return std::sqrt(dotProduct(vector, vector));
}

bool vectorIsFinite(const std::vector<double>& vector) noexcept
{
    return std::all_of(vector.begin(), vector.end(),
        [](const double value) { return std::isfinite(value); });
}

ConjugateGradientResult solveConjugateGradient(
    const SparseMatrixCSR& matrix,
    const std::vector<double>& rightHandSide,
    const ConjugateGradientParameters& parameters)
{
    ConjugateGradientResult result;
    result.solution.assign(rightHandSide.size(), 0.0);
    if (matrix.rows() == 0 || matrix.rows() != matrix.columns() || rightHandSide.size() != matrix.rows()
        || parameters.tolerance <= 0.0 || parameters.maxIterations == 0) {
        result.diagnostic = "CG requires a non-empty square matrix, matching RHS, positive tolerance and iterations";
        return result;
    }
    if (!matrix.finite() || !vectorIsFinite(rightHandSide)) {
        result.status = ConjugateGradientStatus::NonFinite;
        result.diagnostic = "CG input contains NaN or Inf";
        return result;
    }
    std::vector<double> diagonal;
    if (parameters.preconditioner == ConjugateGradientPreconditioner::Jacobi) {
        diagonal.resize(matrix.rows());
        for (std::size_t i = 0; i < matrix.rows(); ++i) {
            diagonal[i] = matrix.diagonal(i);
            if (!std::isfinite(diagonal[i])) {
                result.status = ConjugateGradientStatus::NonFinite;
                result.diagnostic = "CG diagonal contains NaN or Inf";
                return result;
            }
            if (diagonal[i] <= 0.0) {
                result.status = diagonal[i] == 0.0 ? ConjugateGradientStatus::ZeroDiagonal
                                                   : ConjugateGradientStatus::NonPositiveDefinite;
                result.diagnostic = "CG Jacobi preconditioner requires a positive diagonal";
                return result;
            }
        }
    }
    const auto rhsNorm = vectorNorm(rightHandSide);
    auto residual = rightHandSide;
    const auto scale = rhsNorm > 0.0 ? rhsNorm : 1.0;
    result.relativeResidual = vectorNorm(residual) / scale;
    auto relative = vectorNorm(residual) / scale;
    result.relativeResidualHistory.push_back(relative);
    if (relative <= parameters.tolerance) {
        result.status = ConjugateGradientStatus::Converged;
        result.diagnostic = "Initial solution satisfies tolerance";
        return result;
    }
    std::vector<double> preconditioned(residual.size());
    for (std::size_t i = 0; i < residual.size(); ++i)
        preconditioned[i] = parameters.preconditioner == ConjugateGradientPreconditioner::Jacobi
            ? residual[i] / diagonal[i] : residual[i];
    auto direction = preconditioned;
    auto rz = dotProduct(residual, preconditioned);
    if (!std::isfinite(rz) || rz <= 0.0) {
        result.status = ConjugateGradientStatus::NonPositiveDefinite;
        result.diagnostic = "CG initial preconditioned residual is not positive";
        return result;
    }
    for (std::size_t iteration = 1; iteration <= parameters.maxIterations; ++iteration) {
        const auto matrixDirection = matrix.multiply(direction);
        const auto denominator = dotProduct(direction, matrixDirection);
        if (!std::isfinite(denominator)) {
            result.status = ConjugateGradientStatus::NonFinite;
            result.diagnostic = "CG produced a non-finite matrix product";
            return result;
        }
        if (denominator <= 0.0) {
            result.status = ConjugateGradientStatus::NonPositiveDefinite;
            result.diagnostic = "CG detected a non-positive direction curvature";
            return result;
        }
        const auto alpha = rz / denominator;
        for (std::size_t i = 0; i < result.solution.size(); ++i) {
            result.solution[i] += alpha * direction[i];
            residual[i] -= alpha * matrixDirection[i];
        }
        relative = vectorNorm(residual) / scale;
        result.relativeResidualHistory.push_back(relative);
        result.iterations = iteration;
        result.relativeResidual = relative;
        if (!std::isfinite(relative) || !vectorIsFinite(result.solution)) {
            result.status = ConjugateGradientStatus::NonFinite;
            result.diagnostic = "CG solution or residual became non-finite";
            return result;
        }
        if (relative <= parameters.tolerance) {
            result.status = ConjugateGradientStatus::Converged;
            result.diagnostic = "CG converged";
            return result;
        }
        for (std::size_t i = 0; i < residual.size(); ++i)
            preconditioned[i] = parameters.preconditioner == ConjugateGradientPreconditioner::Jacobi
                ? residual[i] / diagonal[i] : residual[i];
        const auto newRz = dotProduct(residual, preconditioned);
        if (!std::isfinite(newRz) || newRz <= 0.0) {
            result.status = ConjugateGradientStatus::NonPositiveDefinite;
            result.diagnostic = "CG detected a non-positive preconditioned residual";
            return result;
        }
        const auto beta = newRz / rz;
        for (std::size_t i = 0; i < direction.size(); ++i) direction[i] = preconditioned[i] + beta * direction[i];
        rz = newRz;
    }
    result.status = ConjugateGradientStatus::NotConverged;
    result.diagnostic = "CG reached the maximum iteration count";
    return result;
}

} // namespace cad::cae
