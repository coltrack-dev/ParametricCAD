#include "cae/SparseMatrix.h"
#include "cae/Tet4.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;
using cad::cae::ConjugateGradientParameters;
using cad::cae::ConjugateGradientPreconditioner;
using cad::cae::SparseMatrixBuilder;
using cad::cae::SparseMatrixCSR;
using cad::cae::Tet4Data;
using cad::cae::Tetrahedron;
using cad::cae::VolumeMesh;

struct GridSpec
{
    std::string label;
    std::size_t nx{};
    std::size_t ny{};
    std::size_t nz{};
};

struct BenchmarkOptions
{
    std::vector<GridSpec> cases;
    std::size_t repetitions{1};
    double tolerance{1.0e-8};
    std::size_t maxIterations{5000};
    ConjugateGradientPreconditioner preconditioner{
        ConjugateGradientPreconditioner::Jacobi};
    std::string output{"cae_benchmarks.csv"};
};

struct BenchmarkRow
{
    std::string caseName;
    std::size_t nodes{};
    std::size_t tetrahedra{};
    std::size_t dofs{};
    std::size_t nnz{};
    double assemblyMs{};
    double localMs{};
    double tripletMs{};
    double csrMs{};
    double dirichletMs{};
    double matvecMs{};
    double solveMs{};
    std::size_t iterations{};
    double residual{};
    double trueResidual{};
    double peakMemoryMb{};
    double meshMemoryMb{};
    double tripletMemoryMb{};
    double csrMemoryMb{};
    double cgVectorsMemoryMb{};
    bool converged{false};
    double manufacturedError{};
};

double elapsedMs(const Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::size_t nodeIndex(
    const std::size_t i, const std::size_t j, const std::size_t k,
    const GridSpec& grid)
{
    return (k * (grid.ny + 1) + j) * (grid.nx + 1) + i;
}

VolumeMesh makeStructuredMesh(const GridSpec& grid)
{
    VolumeMesh mesh;
    mesh.nodes.reserve((grid.nx + 1) * (grid.ny + 1) * (grid.nz + 1));
    for (std::size_t k = 0; k <= grid.nz; ++k)
        for (std::size_t j = 0; j <= grid.ny; ++j)
            for (std::size_t i = 0; i <= grid.nx; ++i)
                mesh.nodes.push_back({mesh.nodes.size(),
                    100.0 * static_cast<double>(i) / grid.nx,
                    50.0 * static_cast<double>(j) / grid.ny,
                    30.0 * static_cast<double>(k) / grid.nz});

    mesh.tetrahedra.reserve(6 * grid.nx * grid.ny * grid.nz);
    for (std::size_t k = 0; k < grid.nz; ++k) {
        for (std::size_t j = 0; j < grid.ny; ++j) {
            for (std::size_t i = 0; i < grid.nx; ++i) {
                const auto a = nodeIndex(i, j, k, grid);
                const auto b = nodeIndex(i + 1, j, k, grid);
                const auto c = nodeIndex(i + 1, j + 1, k, grid);
                const auto d = nodeIndex(i, j + 1, k, grid);
                const auto e = nodeIndex(i, j, k + 1, grid);
                const auto f = nodeIndex(i + 1, j, k + 1, grid);
                const auto g = nodeIndex(i + 1, j + 1, k + 1, grid);
                const auto h = nodeIndex(i, j + 1, k + 1, grid);
                const std::array<std::array<std::size_t, 4>, 6> split{{
                    {{a, b, c, g}}, {{a, c, d, g}}, {{a, d, h, g}},
                    {{a, h, e, g}}, {{a, e, f, g}}, {{a, f, b, g}}}};
                for (const auto nodes : split)
                    mesh.tetrahedra.push_back({mesh.tetrahedra.size(), nodes});
            }
        }
    }
    return mesh;
}

GridSpec specForLabel(const std::string& label)
{
    if (label == "smoke") return {label, 4, 4, 4};
    if (label == "10k") return {label, 17, 17, 6};
    if (label == "100k") return {label, 47, 47, 8};
    if (label == "1m") return {label, 100, 100, 17};
    throw std::invalid_argument("Unknown benchmark case: " + label);
}

GridSpec specForElementCount(const std::size_t requested)
{
    const auto cells = std::max<std::size_t>(1, (requested + 5) / 6);
    const auto nx = std::max<std::size_t>(1,
        static_cast<std::size_t>(std::cbrt(static_cast<double>(cells))));
    const auto ny = nx;
    const auto nz = std::max<std::size_t>(1, (cells + nx * ny - 1) / (nx * ny));
    return {"custom", nx, ny, nz};
}

std::vector<std::size_t> boundaryNodes(
    const VolumeMesh& mesh, const bool right)
{
    const auto target = right ? 100.0 : 0.0;
    std::vector<std::size_t> result;
    for (std::size_t node = 0; node < mesh.nodes.size(); ++node)
        if (std::abs(mesh.nodes[node].x - target) < 1.0e-12) result.push_back(node);
    return result;
}

double peakMemoryMb()
{
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string key;
    while (status >> key) {
        if (key != "VmHWM:") {
            std::string ignored;
            std::getline(status, ignored);
            continue;
        }
        double kb = 0.0;
        status >> kb;
        return kb / 1024.0;
    }
#elif defined(__APPLE__)
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#elif defined(_WIN32)
    return 0.0;
#endif
    return 0.0;
}

double megabytes(const std::size_t bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

std::string cpuName()
{
#if defined(__linux__)
    std::ifstream cpuInfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuInfo, line)) {
        constexpr std::string_view prefix = "model name\t: ";
        if (line.rfind(prefix, 0) == 0) return line.substr(prefix.size());
    }
#endif
    return "unknown";
}

const char* preconditionerName(const ConjugateGradientPreconditioner value)
{
    return value == ConjugateGradientPreconditioner::Jacobi ? "jacobi" : "none";
}

ConjugateGradientPreconditioner parsePreconditioner(const std::string& value)
{
    if (value == "none") return ConjugateGradientPreconditioner::None;
    if (value == "jacobi") return ConjugateGradientPreconditioner::Jacobi;
    throw std::invalid_argument("Preconditioner must be none or jacobi");
}

void printUsage()
{
    std::cout << "Usage: cae_benchmarks [options]\n"
        << "  --case smoke|10k|100k|1m|all\n"
        << "  --elements N\n"
        << "  --repetitions N\n"
        << "  --tolerance VALUE\n"
        << "  --max-iterations N\n"
        << "  --preconditioner none|jacobi\n"
        << "  --output FILE.csv\n";
}

BenchmarkOptions parseOptions(const int argc, char** argv)
{
    BenchmarkOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto value = [&]() -> std::string {
            if (index + 1 >= argc) throw std::invalid_argument("Missing value for " + argument);
            return argv[++index];
        };
        if (argument == "--help") {
            printUsage();
            std::exit(0);
        } else if (argument == "--case") {
            const auto name = value();
            if (name == "all") options.cases = {specForLabel("10k"), specForLabel("100k"), specForLabel("1m")};
            else options.cases = {specForLabel(name)};
        } else if (argument == "--elements") {
            options.cases = {specForElementCount(std::stoull(value()))};
        } else if (argument == "--repetitions") options.repetitions = std::stoull(value());
        else if (argument == "--tolerance") options.tolerance = std::stod(value());
        else if (argument == "--max-iterations") options.maxIterations = std::stoull(value());
        else if (argument == "--preconditioner") options.preconditioner = parsePreconditioner(value());
        else if (argument == "--output") options.output = value();
        else throw std::invalid_argument("Unknown option: " + argument);
    }
    if (options.cases.empty()) options.cases = {
        specForLabel("10k"), specForLabel("100k"), specForLabel("1m")};
    if (options.repetitions == 0 || options.maxIterations == 0 || options.tolerance <= 0.0)
        throw std::invalid_argument("Repetitions, max iterations and tolerance must be positive");
    return options;
}

BenchmarkRow runCase(const GridSpec& grid, const BenchmarkOptions& options)
{
    const auto mesh = makeStructuredMesh(grid);
    const auto validation = cad::cae::validateMesh(mesh);
    if (!validation.valid) throw std::runtime_error("Generated invalid mesh: " + validation.error);
    const auto left = boundaryNodes(mesh, false);
    const auto right = boundaryNodes(mesh, true);
    const auto dofCount = mesh.nodes.size() - left.size() - right.size();
    const double quadraticCoefficient = 1.0e6;
    const double chargeDensity = -2.0 * cad::cae::VacuumPermittivity * quadraticCoefficient;

    BenchmarkRow row;
    row.caseName = grid.label;
    row.nodes = mesh.nodes.size();
    row.tetrahedra = mesh.tetrahedra.size();
    row.dofs = dofCount;

    std::vector<std::array<std::array<double, 4>, 4>> localMatrices;
    std::vector<std::array<double, 4>> localLoads;
    localMatrices.reserve(mesh.tetrahedra.size());
    localLoads.reserve(mesh.tetrahedra.size());
    auto start = Clock::now();
    for (const auto& tetrahedron : mesh.tetrahedra) {
        const auto element = cad::cae::computeTet4(mesh, tetrahedron);
        if (!element.valid) throw std::runtime_error(element.diagnostic);
        localMatrices.push_back(cad::cae::tet4Stiffness(element.data, 1.0));
        localLoads.push_back(cad::cae::tet4ChargeLoad(element.data, chargeDensity));
    }
    row.localMs = elapsedMs(start);

    SparseMatrixBuilder fullBuilder(mesh.nodes.size(), mesh.nodes.size());
    std::vector<double> fullLoad(mesh.nodes.size(), 0.0);
    start = Clock::now();
    for (std::size_t elementIndex = 0; elementIndex < mesh.tetrahedra.size(); ++elementIndex) {
        const auto& nodes = mesh.tetrahedra[elementIndex].nodes;
        for (std::size_t localRow = 0; localRow < 4; ++localRow) {
            fullLoad[nodes[localRow]] += localLoads[elementIndex][localRow];
            for (std::size_t localColumn = 0; localColumn < 4; ++localColumn)
                fullBuilder.add(nodes[localRow], nodes[localColumn],
                    localMatrices[elementIndex][localRow][localColumn]);
        }
    }
    row.tripletMs = elapsedMs(start);
    row.assemblyMs = row.localMs + row.tripletMs;

    start = Clock::now();
    const auto fullMatrix = fullBuilder.build();
    row.csrMs = elapsedMs(start);

    std::vector<bool> prescribed(mesh.nodes.size(), false);
    std::vector<double> prescribedValue(mesh.nodes.size(), 0.0);
    for (const auto node : left) prescribed[node] = true;
    for (const auto node : right) {
        prescribed[node] = true;
        prescribedValue[node] = quadraticCoefficient * std::pow(mesh.nodes[node].x * 1.0e-3, 2);
    }
    std::vector<std::size_t> freeNodes;
    std::vector<std::size_t> reducedIndex(mesh.nodes.size(), 0);
    for (std::size_t node = 0; node < mesh.nodes.size(); ++node)
        if (!prescribed[node]) {
            reducedIndex[node] = freeNodes.size();
            freeNodes.push_back(node);
        }
    SparseMatrixBuilder reducedBuilder(freeNodes.size(), freeNodes.size());
    std::vector<double> reducedLoad(freeNodes.size(), 0.0);
    start = Clock::now();
    for (std::size_t reducedRow = 0; reducedRow < freeNodes.size(); ++reducedRow) {
        const auto rowIndex = freeNodes[reducedRow];
        reducedLoad[reducedRow] = fullLoad[rowIndex];
        for (std::size_t entry = fullMatrix.rowOffsets()[rowIndex];
             entry < fullMatrix.rowOffsets()[rowIndex + 1]; ++entry) {
            const auto column = fullMatrix.columnIndices()[entry];
            const auto value = fullMatrix.values()[entry];
            if (prescribed[column]) reducedLoad[reducedRow] -= value * prescribedValue[column];
            else reducedBuilder.add(reducedRow, reducedIndex[column], value);
        }
    }
    const auto reducedMatrix = reducedBuilder.build();
    row.dirichletMs = elapsedMs(start);
    row.nnz = reducedMatrix.values().size();
    row.meshMemoryMb = megabytes(
        mesh.nodes.capacity() * sizeof(cad::cae::MeshNode)
        + mesh.tetrahedra.capacity() * sizeof(cad::cae::Tetrahedron));
    row.tripletMemoryMb = megabytes(
        mesh.tetrahedra.size() * 16 * sizeof(cad::cae::SparseTriplet));
    row.csrMemoryMb = megabytes(
        fullMatrix.rowOffsets().capacity() * sizeof(std::size_t)
        + fullMatrix.columnIndices().capacity() * sizeof(std::size_t)
        + fullMatrix.values().capacity() * sizeof(double));
    row.cgVectorsMemoryMb = megabytes(4 * reducedMatrix.rows() * sizeof(double));

    const std::vector<double> probe(reducedMatrix.columns(), 1.0);
    start = Clock::now();
    const auto probeResult = reducedMatrix.multiply(probe);
    row.matvecMs = elapsedMs(start);
    if (!cad::cae::vectorIsFinite(probeResult)) throw std::runtime_error("CSR matvec produced NaN/Inf");

    ConjugateGradientParameters parameters;
    parameters.tolerance = options.tolerance;
    parameters.maxIterations = options.maxIterations;
    parameters.preconditioner = options.preconditioner;
    start = Clock::now();
    const auto linear = cad::cae::solveConjugateGradient(
        reducedMatrix, reducedLoad, parameters);
    row.solveMs = elapsedMs(start);
    row.iterations = linear.iterations;
    row.residual = linear.relativeResidual;
    row.converged = linear.status == cad::cae::ConjugateGradientStatus::Converged;
    if (!row.converged) throw std::runtime_error(grid.label + ": " + linear.diagnostic);

    const auto reconstructed = reducedMatrix.multiply(linear.solution);
    std::vector<double> residual(reducedLoad.size());
    for (std::size_t index = 0; index < residual.size(); ++index)
        residual[index] = reconstructed[index] - reducedLoad[index];
    row.trueResidual = cad::cae::vectorNorm(residual)
        / std::max(1.0, cad::cae::vectorNorm(reducedLoad));

    double maxError = 0.0;
    for (std::size_t node = 0; node < mesh.nodes.size(); ++node) {
        double potential = prescribed[node] ? prescribedValue[node]
            : linear.solution[reducedIndex[node]];
        const auto exact = quadraticCoefficient * std::pow(mesh.nodes[node].x * 1.0e-3, 2);
        maxError = std::max(maxError, std::abs(potential - exact));
        if (!std::isfinite(potential)) throw std::runtime_error("Solution contains NaN/Inf");
    }
    row.manufacturedError = maxError;
    row.peakMemoryMb = peakMemoryMb();
    return row;
}

void writeCsv(const std::string& path, const BenchmarkOptions& options,
              const std::vector<BenchmarkRow>& rows)
{
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Cannot open benchmark output: " + path);
    output << "# os="
#if defined(_WIN32)
           << "Windows"
#elif defined(__APPLE__)
           << "macOS"
#elif defined(__linux__)
           << "Linux"
#else
           << "unknown"
#endif
           << ",compiler="
#if defined(_MSC_VER)
           << "MSVC " << _MSC_VER
#elif defined(__clang__)
           << "Clang " << __clang_major__ << '.' << __clang_minor__
#elif defined(__GNUC__)
           << "GCC " << __GNUC__ << '.' << __GNUC_MINOR__
#else
           << "unknown"
#endif
           << ",build_type="
#ifdef PARAMETRIC_CAD_BENCHMARK_BUILD_TYPE
           << PARAMETRIC_CAD_BENCHMARK_BUILD_TYPE
#else
           << "unknown"
#endif
           << ",cpu=" << cpuName()
           << ",threads=1,hardware_threads=" << std::thread::hardware_concurrency()
           << '\n';
    output << "# preconditioner=" << preconditionerName(options.preconditioner)
           << ",tolerance=" << options.tolerance
           << ",max_iterations=" << options.maxIterations
           << ",repetitions=" << options.repetitions << '\n';
    output << "case,nodes,tetrahedra,dofs,nnz,assembly_ms,local_ms,triplet_ms,csr_ms,dirichlet_ms,matvec_ms,solve_ms,iterations,residual,true_residual,peak_memory_mb,mesh_memory_mb,triplet_memory_mb,csr_memory_mb,cg_vectors_memory_mb,manufactured_error\n";
    output << std::setprecision(12);
    for (const auto& row : rows)
        output << row.caseName << ',' << row.nodes << ',' << row.tetrahedra << ','
            << row.dofs << ',' << row.nnz << ',' << row.assemblyMs << ',' << row.localMs
            << ',' << row.tripletMs
            << ',' << row.csrMs << ',' << row.dirichletMs << ',' << row.matvecMs << ','
            << row.solveMs << ',' << row.iterations << ',' << row.residual << ','
            << row.trueResidual << ',' << row.peakMemoryMb << ',' << row.meshMemoryMb
            << ',' << row.tripletMemoryMb << ',' << row.csrMemoryMb << ','
            << row.cgVectorsMemoryMb << ',' << row.manufacturedError << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const auto options = parseOptions(argc, argv);
        std::vector<BenchmarkRow> rows;
        rows.reserve(options.cases.size() * options.repetitions);
        std::cout << "preconditioner=" << preconditionerName(options.preconditioner)
                  << " tolerance=" << options.tolerance
                  << " max_iterations=" << options.maxIterations << '\n';
        for (const auto& grid : options.cases) {
            for (std::size_t repetition = 0; repetition < options.repetitions; ++repetition) {
                const auto row = runCase(grid, options);
                rows.push_back(row);
                std::cout << row.caseName << " nodes=" << row.nodes
                    << " tetrahedra=" << row.tetrahedra << " dofs=" << row.dofs
                    << " nnz=" << row.nnz << " solve_ms=" << row.solveMs
                    << " iterations=" << row.iterations
                    << " residual=" << row.residual
                    << " true_residual=" << row.trueResidual << '\n';
            }
        }
        writeCsv(options.output, options, rows);
        std::cout << "csv=" << options.output << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cae_benchmarks: " << error.what() << '\n';
        return 1;
    }
}
