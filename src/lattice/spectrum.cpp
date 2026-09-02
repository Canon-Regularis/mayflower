#include "mayflower/spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace mayflower {
namespace {

// State during a column sweep: an extension profile, one base-k digit per row
// holding how many further columns a horizontal rod in that row still occupies,
// plus the rows a vertical rod in this column still has to fill. The profile
// alone survives a column boundary, since a vertical rod must fit inside its
// column.
struct Sweep {
    int H, k;
    std::int64_t profiles;              // k^H
    std::vector<std::int64_t> pow;      // pow[r] = k^r
    std::vector<double> work, next;

    Sweep(int height, int rodLength) : H(height), k(rodLength) {
        pow.assign(static_cast<std::size_t>(H) + 1, 1);
        for (int r = 1; r <= H; ++r) pow[static_cast<std::size_t>(r)] = pow[static_cast<std::size_t>(r) - 1] * k;
        profiles = pow[static_cast<std::size_t>(H)];
        const std::int64_t cells = profiles * k;
        if (cells > (std::int64_t{1} << 22))
            throw std::invalid_argument("strip too large for the transfer sweep");
        work.assign(static_cast<std::size_t>(cells), 0.0);
        next.assign(static_cast<std::size_t>(cells), 0.0);
    }

    // One application of T. `v` is indexed by profile; the result overwrites it.
    // `finiteWidth` limits horizontal starts when a patch has an edge; pass -1
    // for the infinite strip, where the operator is column-homogeneous.
    void apply(std::vector<double>& v, double z, int col = -1, int width = -1) {
        std::fill(work.begin(), work.end(), 0.0);
        for (std::int64_t e = 0; e < profiles; ++e)
            if (v[static_cast<std::size_t>(e)] != 0.0)
                work[static_cast<std::size_t>(e * k)] = v[static_cast<std::size_t>(e)];

        for (int row = 0; row < H; ++row) {
            std::fill(next.begin(), next.end(), 0.0);
            const std::int64_t unit = pow[static_cast<std::size_t>(row)];
            const bool verticalFits = row + k <= H;
            const bool horizontalFits = (width < 0) || (col + k <= width);

            for (std::int64_t e = 0; e < profiles; ++e) {
                const int d = static_cast<int>((e / unit) % k);
                for (int vrem = 0; vrem < k; ++vrem) {
                    const double w = work[static_cast<std::size_t>(e * k + vrem)];
                    if (w == 0.0) continue;

                    if (d > 0) {                       // horizontal rod passing through
                        if (vrem > 0) continue;        // would overlap a vertical rod
                        next[static_cast<std::size_t>((e - unit) * k)] += w;
                        continue;
                    }
                    if (vrem > 0) {                    // vertical rod continuing down
                        next[static_cast<std::size_t>(e * k + (vrem - 1))] += w;
                        continue;
                    }
                    next[static_cast<std::size_t>(e * k)] += w;                       // empty
                    if (horizontalFits)
                        next[static_cast<std::size_t>((e + (k - 1) * unit) * k)] += w * z;
                    // A monomer has one orientation. At k = 1 both branches leave
                    // no residual and land on the same successor, so counting the
                    // vertical one too would weight every cell (1 + 2z) instead
                    // of (1 + z).
                    if (k > 1 && verticalFits)
                        next[static_cast<std::size_t>(e * k + (k - 1))] += w * z;
                }
            }
            work.swap(next);
        }

        // A vertical rod cannot cross a column boundary, so only vrem == 0 lives.
        for (std::int64_t e = 0; e < profiles; ++e)
            v[static_cast<std::size_t>(e)] = work[static_cast<std::size_t>(e * k)];
    }
};

double l1(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x;
    return s;
}

}  // namespace

Spectrum transferSpectrum(int height, int rodLength, double z, int maxIterations,
                          double tolerance) {
    if (height < 1 || rodLength < 1) throw std::invalid_argument("bad strip");
    Spectrum out;
    Sweep sweep(height, rodLength);
    std::vector<double> v(static_cast<std::size_t>(sweep.profiles), 0.0);
    v[0] = 1.0;   // start from the empty boundary

    double prev = 0;
    std::vector<double> deltas;   // signed, so sign alternation is visible
    for (int it = 1; it <= maxIterations; ++it) {
        const double before = l1(v);
        sweep.apply(v, z);
        const double after = l1(v);
        if (after <= 0) { out.iterations = it; return out; }
        const double lambda = after / before;
        for (double& x : v) x /= after;   // renormalise, the operator is positive

        const double delta = lambda - prev;
        if (it > 2) deltas.push_back(delta);
        if (it > 6 && std::abs(delta) < tolerance * std::max(1.0, lambda)) {
            out.converged = true;
            out.lambdaMax = lambda;
            out.iterations = it;
            break;
        }
        prev = lambda;
        out.lambdaMax = lambda;
        out.iterations = it;
    }

    // The power method's error decays like (lambda_2 / lambda_1)^n. When the
    // subdominant eigenvalue is negative or complex the correction alternates
    // sign, so consecutive ratios are useless and a lag of two is not: taking
    // |d_n / d_{n-2}| squares out the sign.
    if (deltas.size() >= 6) {
        const std::size_t take = std::max<std::size_t>(2, deltas.size() / 4);
        double acc = 0;
        int used = 0, flips = 0;
        for (std::size_t i = deltas.size() - take; i < deltas.size(); ++i) {
            if (i >= 2 && deltas[i - 2] != 0.0) {
                acc += std::sqrt(std::abs(deltas[i] / deltas[i - 2]));
                ++used;
            }
            if (i >= 1 && deltas[i] * deltas[i - 1] < 0) ++flips;
        }
        if (used) {
            out.ratio = acc / used;
            out.alternating = flips > static_cast<int>(take) / 2;
            if (out.ratio > 0 && out.ratio < 1)
                out.correlationLength = -1.0 / std::log(out.ratio);
        }
    }

    out.freeEnergyPerSite = std::log(out.lambdaMax) / height;

    // Density by a central difference: rho = (k/H) * z d(log lambda)/dz. The
    // sweep is reused, since its arrays are the expensive part.
    const double h = std::max(1e-4, z * 1e-3);
    std::vector<double> u(static_cast<std::size_t>(sweep.profiles), 0.0);
    const auto growth = [&](double zz) {
        std::fill(u.begin(), u.end(), 0.0);
        u[0] = 1.0;
        double lam = 0, p = 0;
        for (int it = 1; it <= 200; ++it) {
            const double b = l1(u);
            sweep.apply(u, zz);
            const double a = l1(u);
            if (a <= 0) return 0.0;
            lam = a / b;
            for (double& x : u) x /= a;
            if (it > 6 && std::abs(lam - p) < 1e-11 * std::max(1.0, lam)) break;
            p = lam;
        }
        return std::log(lam);
    };
    const double dlog = (growth(z + h) - growth(z - h)) / (2 * h);
    out.density = rodLength * z * dlog / height;
    return out;
}

double partitionFunction(int height, int width, int rodLength, double z) {
    Sweep sweep(height, rodLength);
    std::vector<double> v(static_cast<std::size_t>(sweep.profiles), 0.0);
    v[0] = 1.0;
    for (int col = 0; col < width; ++col) sweep.apply(v, z, col, width);
    return v[0];   // the profile must be empty at the right edge
}

}  // namespace mayflower
