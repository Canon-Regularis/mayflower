// spectrum: the transfer matrix of the hard-rod strip, diagonalised.
//
// Battleships is one instance of a hard-rod lattice gas: non-overlapping k-mers
// on a grid. Fixing the fleet makes it a counting problem; letting the rod
// number float and weighting by a fugacity makes it a statistical-mechanics
// problem with a free energy, a density, and a correlation length. This tool
// computes those.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/spectrum.hpp"

int main() {
    using namespace mayflower;
    namespace k = mayflower::constants;

    std::printf("Hard-rod strip: the transfer matrix diagonalised\n");
    std::printf("===============================================\n\n");
    std::printf("Non-overlapping k-mers, horizontal or vertical, on an H-row strip of\n");
    std::printf("unbounded width. Each rod carries fugacity z, so z = 1 counts every\n");
    std::printf("packing once. lambda is the growth per column: the number of packings of\n");
    std::printf("an H x W strip goes like lambda^W.\n\n");

    std::printf("%3s %3s %16s %14s %10s %9s %s\n", "k", "H", "lambda", "log(lambda)/H",
                "density", "xi (cols)", "correlations");
    for (int rod = 2; rod <= 5; ++rod) {
        for (int h = 2; h <= 12; ++h) {
            if (h < rod) continue;
            Spectrum s;
            // The sweep holds k^(H+1) doubles, so wide strips with long rods fall
            // outside it and are skipped instead of thrashing.
            try { s = transferSpectrum(h, rod, 1.0); }
            catch (const std::exception&) { break; }
            std::printf("%3d %3d %16.10f %14.6f %10.4f %9.2f %s\n", rod, h, s.lambdaMax,
                        s.freeEnergyPerSite, s.density, s.correlationLength,
                        s.alternating ? "alternate sign" : "same sign");
        }
        std::printf("\n");
    }

    // Entropy per site as the strip widens. For k-mers at z = 1 this converges
    // to the entropy density of the two-dimensional gas.
    std::printf("Entropy per site as the strip widens, z = 1:\n");
    std::printf("%3s", "H");
    for (int rod = 2; rod <= 5; ++rod) std::printf("%14s", (std::string("k=") + std::to_string(rod)).c_str());
    std::printf("\n");
    for (int h = 2; h <= 12; ++h) {
        std::printf("%3d", h);
        for (int rod = 2; rod <= 5; ++rod) {
            if (h < rod) { std::printf("%14s", "."); continue; }
            try {
                const auto s = transferSpectrum(h, rod, 1.0);
                std::printf("%14.6f", s.freeEnergyPerSite);
            } catch (const std::exception&) { std::printf("%14s", "too wide"); }
        }
        std::printf("\n");
    }
    std::printf("\nA dot means the rod does not fit; \"too wide\" means the strip needs more\n"
                "state than the sweep will allocate.\n");

    // The strip entropy approaches the two-dimensional value like f(H) = f - a/H,
    // so H*f(H) - (H-1)*f(H-1) cancels the leading correction. For dimers that
    // limit is the monomer-dimer entropy of the square lattice, a constant this
    // sweep knows nothing about and has to arrive at on its own.
    std::printf("\nExtrapolating the strip entropy to two dimensions, f(H) = f - a/H:\n");
    std::printf("%3s %14s %14s\n", "H", "f(H)", "H f(H)-(H-1)f(H-1)");
    double previous = 0, extrapolated = 0;
    for (int h = 2; h <= 12; ++h) {
        const auto s2 = transferSpectrum(h, 2, 1.0);
        if (h > 2) {
            extrapolated = h * s2.freeEnergyPerSite - (h - 1) * previous;
            std::printf("%3d %14.6f %14.6f\n", h, s2.freeEnergyPerSite, extrapolated);
        } else {
            std::printf("%3d %14.6f %14s\n", h, s2.freeEnergyPerSite, "-");
        }
        previous = s2.freeEnergyPerSite;
    }
    std::printf("\n  extrapolated dimer entropy per site   %.6f\n", extrapolated);
    std::printf("  published monomer-dimer constant      0.662799   [recalled, worth citing\n");
    std::printf("                                                    from a source before use]\n");
    std::printf("  The sweep was given no part of that number.\n");

    // The 1 x W dimer strip is the Fibonacci recurrence, so its growth rate is
    // the golden ratio. A closed form the sweep has to reproduce.
    const auto fib = transferSpectrum(1, 2, 1.0);
    std::printf("\nClosed-form check: the 1-row dimer strip counts Fibonacci, so its growth\n");
    std::printf("rate is the golden ratio.\n  lambda      %.12f\n  (1+sqrt5)/2 %.12f\n",
                fib.lambdaMax, (1 + std::sqrt(5.0)) / 2);

    std::printf("\nThe Battleship instance sits inside this family as the fixed-fleet corner:\n");
    std::printf("  one 5, one 4, two 3s and one 2 on a 10x10 board, %llu arrangements,\n",
                static_cast<unsigned long long>(k::kOmega0));
    std::printf("  which is %.4f nats per site against the free rod gas above.\n",
                std::log(static_cast<double>(k::kOmega0)) / k::kCellCount);
    std::printf("  Fixing the rod count is what makes it a counting problem instead of a\n");
    std::printf("  thermodynamic one, and it is why the engine carries a fleet counter that\n");
    std::printf("  this transfer matrix drops.\n");

    const bool ok = std::abs(fib.lambdaMax - (1 + std::sqrt(5.0)) / 2) < 1e-9 &&
                    std::abs(extrapolated - 0.662799) < 1e-4;
    std::printf("\n%s\n", ok ? "OK" : "*** CHECK FAILED ***");
    return ok ? 0 : 1;
}
