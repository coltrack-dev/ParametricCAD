#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cad::cae {

struct SparseTriplet
{
    std::size_t row{};
    std::size_t column{};
    double value{};
};

class SparseMatrixCSR
{
public:
    SparseMatrixCSR() = default;
    SparseMatrixCSR(std::size_t rows, std::size_t columns,
                    std::vector<std::size_t> rowOffsets,
                    std::vector<std::size_t> columnIndices,
                    std::vector<double> values);

    std::size_t rows() const noexcept { return rows_; }
    std::size_t columns() const noexcept { return columns_; }
    const std::vector<std::size_t>& rowOffsets() const noexcept { return rowOffsets_; }
    const std::vector<std::size_t>& columnIndices() const noexcept { return columnIndices_; }
    const std::vector<double>& values() const noexcept { return values_; }

    std::vector<double> multiply(const std::vector<double>& vector) const;
    double diagonal(std::size_t row) const noexcept;
    bool finite() const noexcept;

private:
    std::size_t rows_{};
    std::size_t columns_{};
    std::vector<std::size_t> rowOffsets_;
    std::vector<std::size_t> columnIndices_;
    std::vector<double> values_;
};

class SparseMatrixBuilder
{
public:
    SparseMatrixBuilder(std::size_t rows, std::size_t columns);

    void add(std::size_t row, std::size_t column, double value);
    SparseMatrixCSR build() const;

private:
    std::size_t rows_{};
    std::size_t columns_{};
    std::vector<SparseTriplet> triplets_;
};

double dotProduct(const std::vector<double>& left, const std::vector<double>& right);
double vectorNorm(const std::vector<double>& vector);
bool vectorIsFinite(const std::vector<double>& vector) noexcept;

enum class ConjugateGradientStatus
{
    Converged,
    InvalidInput,
    NonPositiveDefinite,
    ZeroDiagonal,
    NonFinite,
    NotConverged
};

struct ConjugateGradientParameters
{
    double tolerance{1.0e-10};
    std::size_t maxIterations{1000};
};

struct ConjugateGradientResult
{
    std::vector<double> solution;
    std::vector<double> relativeResidualHistory;
    std::size_t iterations{};
    double relativeResidual{};
    ConjugateGradientStatus status{ConjugateGradientStatus::InvalidInput};
    std::string diagnostic;
};

ConjugateGradientResult solveConjugateGradient(
    const SparseMatrixCSR& matrix,
    const std::vector<double>& rightHandSide,
    const ConjugateGradientParameters& parameters = {});

} // namespace cad::cae
