// The transfer matrix, diagonalised.
//
// The counting DP carries a fleet counter, which ties it to one fixed fleet and
// makes the column-to-column operator depend on how much of the fleet is spent.
// Drop the counter and give each rod a fugacity z, and the operator becomes the
// same at every column. That is a transfer matrix in the statistical-mechanics
// sense, and the quantities it yields are the ones that language names:
//
//   Z_{H x W}(z) = <1| T(z)^W |0>            grand-canonical partition function
//   log lambda_max                           free energy per column
//   xi = 1 / log(lambda_1 / lambda_2)        correlation length, in columns
//
// The model is a hard-rod lattice gas: non-overlapping k-mers, horizontal or
// vertical, on an H-row strip. Battleships is the fixed-fleet, single-instance
// corner of the same object.
//
// lambda_max comes from power iteration, where applying T is one column sweep of
// the same DP the engine already runs, so no matrix is ever formed. The
// subdominant eigenvalue falls out of the convergence rate: the error of the
// power method decays like (lambda_2 / lambda_1)^n.
#pragma once

#include <cstdint>
#include <vector>

namespace mayflower {

struct Spectrum {
    double lambdaMax = 0;        // growth per column
    double ratio = 0;            // lambda_2 / lambda_1, from the convergence rate
    double freeEnergyPerSite = 0;// log(lambda_max) / height
    double correlationLength = 0;// columns; zero when the ratio could not be read
    bool alternating = false;    // the subdominant eigenvalue is negative or complex,
                                 // so correlations flip sign column to column
    double density = 0;          // fraction of sites covered by rods
    int iterations = 0;
    bool converged = false;
};

// Growth rate of an H-row strip packed with non-overlapping k-mers, each
// weighted by fugacity z. z = 1 counts every packing equally.
Spectrum transferSpectrum(int height, int rodLength, double z,
                          int maxIterations = 400, double tolerance = 1e-12);

// Grand-canonical partition function of a finite H x W patch, summing z^(rods)
// over every packing. Used to check lambda: Z(W+1)/Z(W) tends to lambda_max.
double partitionFunction(int height, int width, int rodLength, double z);

}  // namespace mayflower
