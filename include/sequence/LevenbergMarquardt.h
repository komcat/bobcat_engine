// LevenbergMarquardt.h - Minimal header-only Levenberg-Marquardt solver
// For small-parameter curve fitting (e.g., 3 params, 5-9 data points)
#pragma once

#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace LMFit {

struct FitResult {
    std::vector<double> params;    // Fitted parameters
    double residualSum = 0.0;      // Sum of squared residuals
    int iterations = 0;
    bool converged = false;
};

// Model function type: f(x, params) -> y
using ModelFunc = std::function<double(double x, const std::vector<double>& params)>;

inline FitResult Solve(
    const std::vector<double>& xData,
    const std::vector<double>& yData,
    const std::vector<double>& initialParams,
    ModelFunc model,
    int maxIterations = 800,
    double tolerance = 1e-8)
{
    const size_t N = xData.size();
    const size_t P = initialParams.size();

    if (N == 0 || N != yData.size() || P == 0) {
        return FitResult{initialParams, 0.0, 0, false};
    }

    // Numerical Jacobian step size
    const double eps = 1e-8;

    std::vector<double> params = initialParams;
    double lambda = 1e-3;
    const double lambdaUp = 10.0;
    const double lambdaDown = 10.0;

    // Compute initial residuals and cost
    auto computeResiduals = [&](const std::vector<double>& p) -> std::vector<double> {
        std::vector<double> r(N);
        for (size_t i = 0; i < N; ++i) {
            r[i] = yData[i] - model(xData[i], p);
        }
        return r;
    };

    auto computeCost = [](const std::vector<double>& r) -> double {
        double s = 0.0;
        for (double v : r) s += v * v;
        return s;
    };

    std::vector<double> residuals = computeResiduals(params);
    double cost = computeCost(residuals);

    FitResult result;
    result.params = params;
    result.residualSum = cost;

    for (int iter = 0; iter < maxIterations; ++iter) {
        // Compute Jacobian J[i][j] = d(model(x_i, p)) / d(p_j)  via central differences
        // Note: J_residual[i][j] = -J_model[i][j] since r = y - model
        // We compute J of model, so gradient is -J^T * r
        std::vector<std::vector<double>> J(N, std::vector<double>(P, 0.0));
        for (size_t j = 0; j < P; ++j) {
            std::vector<double> pPlus = params;
            std::vector<double> pMinus = params;
            double h = std::max(eps, std::abs(params[j]) * eps);
            pPlus[j] += h;
            pMinus[j] -= h;
            for (size_t i = 0; i < N; ++i) {
                J[i][j] = (model(xData[i], pPlus) - model(xData[i], pMinus)) / (2.0 * h);
            }
        }

        // Compute J^T * J  (P x P)
        std::vector<std::vector<double>> JtJ(P, std::vector<double>(P, 0.0));
        for (size_t j = 0; j < P; ++j) {
            for (size_t k = 0; k < P; ++k) {
                double sum = 0.0;
                for (size_t i = 0; i < N; ++i) {
                    sum += J[i][j] * J[i][k];
                }
                JtJ[j][k] = sum;
            }
        }

        // Compute J^T * r  (P x 1)
        // Since r = y - model, and J is of model: gradient = -J^T * r = J^T * (model - y)
        // We want dp such that (J^T J + lambda*I) dp = J^T * r
        std::vector<double> JtR(P, 0.0);
        for (size_t j = 0; j < P; ++j) {
            double sum = 0.0;
            for (size_t i = 0; i < N; ++i) {
                sum += J[i][j] * residuals[i];
            }
            JtR[j] = sum;
        }

        // Solve (J^T J + lambda * diag(J^T J)) dp = J^T * r
        // Using simple Gaussian elimination for small P
        std::vector<std::vector<double>> A = JtJ;
        std::vector<double> b = JtR;
        for (size_t j = 0; j < P; ++j) {
            A[j][j] += lambda * (JtJ[j][j] > 0.0 ? JtJ[j][j] : 1.0);
        }

        // Gaussian elimination with partial pivoting
        for (size_t col = 0; col < P; ++col) {
            // Partial pivot
            size_t maxRow = col;
            for (size_t row = col + 1; row < P; ++row) {
                if (std::abs(A[row][col]) > std::abs(A[maxRow][col])) {
                    maxRow = row;
                }
            }
            if (maxRow != col) {
                std::swap(A[col], A[maxRow]);
                std::swap(b[col], b[maxRow]);
            }

            double pivot = A[col][col];
            if (std::abs(pivot) < 1e-30) continue; // Singular

            for (size_t row = col + 1; row < P; ++row) {
                double factor = A[row][col] / pivot;
                for (size_t k = col; k < P; ++k) {
                    A[row][k] -= factor * A[col][k];
                }
                b[row] -= factor * b[col];
            }
        }

        // Back substitution
        std::vector<double> dp(P, 0.0);
        for (int j = static_cast<int>(P) - 1; j >= 0; --j) {
            double sum = b[static_cast<size_t>(j)];
            for (size_t k = static_cast<size_t>(j) + 1; k < P; ++k) {
                sum -= A[static_cast<size_t>(j)][k] * dp[k];
            }
            if (std::abs(A[static_cast<size_t>(j)][static_cast<size_t>(j)]) > 1e-30) {
                dp[static_cast<size_t>(j)] = sum / A[static_cast<size_t>(j)][static_cast<size_t>(j)];
            }
        }

        // Try update
        std::vector<double> newParams(P);
        for (size_t j = 0; j < P; ++j) {
            newParams[j] = params[j] + dp[j];
        }

        std::vector<double> newResiduals = computeResiduals(newParams);
        double newCost = computeCost(newResiduals);

        if (newCost < cost) {
            // Improvement — accept and decrease lambda
            params = newParams;
            residuals = newResiduals;
            double costDelta = cost - newCost;
            cost = newCost;
            lambda /= lambdaDown;
            if (lambda < 1e-15) lambda = 1e-15;

            result.params = params;
            result.residualSum = cost;
            result.iterations = iter + 1;

            // Check convergence
            double maxDp = 0.0;
            for (size_t j = 0; j < P; ++j) {
                maxDp = std::max(maxDp, std::abs(dp[j]));
            }
            if (maxDp < tolerance && costDelta < tolerance) {
                result.converged = true;
                return result;
            }
        } else {
            // No improvement — increase lambda
            lambda *= lambdaUp;
            if (lambda > 1e15) {
                // Lambda too large, give up
                result.converged = false;
                result.iterations = iter + 1;
                return result;
            }
        }
    }

    result.converged = false;
    result.iterations = maxIterations;
    return result;
}

} // namespace LMFit
