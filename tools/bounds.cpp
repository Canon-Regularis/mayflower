// bounds: the lower-bound ladder for expected shots, with each rung labelled by
// how firmly it is established.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "mayflower/certify.hpp"
#include "mayflower/constants.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/profile_dp.hpp"

namespace {

double seconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

int main() {
    using namespace mayflower;
    namespace k = mayflower::constants;

    const Instance inst = standardInstance();
    std::printf("Lower-bound ladder, %s\n", inst.describe().c_str());
    std::printf("=====================================\n\n");

    const double h0 = std::log2(static_cast<double>(k::kOmega0));
    const double entropyBound = h0 / k::kMaxBitsPerShot;

    std::printf("E1  coverage           %6.2f shots   exact: all %d ship cells must be shot\n",
                static_cast<double>(k::kCoverageBound), k::kShipCells);
    std::printf("E2  entropy            %6.2f shots   exact: H = %.4f bits over an alphabet of %d\n",
                entropyBound, h0, k::kOutcomeAlphabetSize);
    std::printf("                                     E2 < E1, so counting ship cells is the\n");
    std::printf("                                     stronger constraint and E2 is vacuous.\n\n");

    const auto wf = waterFillingBound(inst.fleet, k::kOmega0, inst.cellCount());
    std::printf("E4  water-filling      %6.4f shots   exact, given the transcript-counting argument\n",
                wf.bound);
    std::printf("                                     K = %llu distinct announcement strings,\n",
                static_cast<unsigned long long>(wf.hitTranscripts));
    std::printf("                                     the count saturates at depth %d.\n\n",
                wf.saturatesAt);

    std::printf("Blocking numbers beta(L), the fewest shots guaranteeing contact with a lone\n");
    std::printf("length-L ship. Exact, and the DP is checked against brute force on small boards.\n\n");
    std::vector<int> betas;
    for (int L : {2, 3, 4, 5}) {
        const auto b = blockingNumber(inst.width, inst.height, L);
        betas.push_back(b.blocking);
        std::printf("  beta(%d) = %2d   largest L-free set %2d   (%.2f s)\n", L, b.blocking,
                    b.largestFreeSet, b.seconds);
    }
    std::printf("\n");

    // Cross-check between two independent pieces of machinery: block every
    // 5-placement, feed those cells to the counting DP as misses, and the
    // hypothesis space must collapse to nothing.
    std::printf("Cross-check: a beta(5) blocking set makes the fleet impossible.\n");
    const auto found = blockingWitness(inst.width, inst.height, 5);
    const std::vector<int>& witness = found.cells;
    std::vector<CellConstraint> cells(static_cast<std::size_t>(inst.cellCount()),
                                      CellConstraint::Free);
    for (int c : witness) cells[static_cast<std::size_t>(c)] = CellConstraint::MustBeEmpty;
    const auto t0 = std::chrono::steady_clock::now();
    const std::uint64_t blocked = countConfigurations(inst, cells).count;
    std::printf("  %zu cells as misses -> |Omega| = %llu   %s  (%.2f s)\n", witness.size(),
                static_cast<unsigned long long>(blocked),
                blocked == 0 ? "as required" : "*** UNEXPECTED ***", seconds(t0));

    // Dropping any single cell must revive the space, so the witness is minimal
    // as a set. That is weaker than being globally smallest.
    int revived = 0;
    for (std::size_t i = 0; i < witness.size(); ++i) {
        auto probe = cells;
        probe[static_cast<std::size_t>(witness[i])] = CellConstraint::Free;
        if (countConfigurations(inst, probe).count > 0) ++revived;
    }
    std::printf("  removing any single cell revives the space: %d of %zu\n\n", revived,
                witness.size());

    std::printf("What follows, and what does not:\n");
    std::printf("  Established: a %zu-cell set forces first contact, so the minimum set meeting\n",
                witness.size());
    std::printf("  every configuration is at most %zu cells.\n", witness.size());
    std::printf("  NOT established: that no smaller set does. An adversarial worst-case bound of\n");
    std::printf("  the form W* >= 17 + beta(5) - 1 = 36 needs the opposite direction, a proof that\n");
    std::printf("  every 19-cell set leaves some configuration untouched. That is unproven here,\n");
    std::printf("  and checking it directly means enumerating C(100,19) sets. The bound is\n");
    std::printf("  therefore not claimed.\n\n");

    std::printf("Rung withdrawn: the max-coverage relaxation, and the estimate near 35 shots\n");
    std::printf("that went with it. Replacing the C(t,17) factor with the largest number of\n");
    std::printf("configurations fitting inside any t-cell set is not a licensed substitution:\n");
    std::printf("K*C(t,17) counts transcripts, and a finished board is determined by its\n");
    std::printf("transcript, while max-coverage counts boards inside one shot-set and needs\n");
    std::printf("the number of shot-sets a policy can reach, which is far larger than K.\n");
    std::printf("Done anyway it exceeds the true optimum on all six instances where the\n");
    std::printf("optimum is computable. What it does bound is the NON-ADAPTIVE optimum, which\n");
    std::printf("is the wrong problem. See tools/maxcover and docs/MAXCOVER.txt.\n\n");

    std::printf("Measured for comparison (20,000 games, seeded uniform pool):\n");
    std::printf("  random               95.354  [ 95.288,  95.421]\n");
    std::printf("  parity hunt/target   51.535  [ 51.414,  51.656]\n");
    std::printf("  density              44.369  [ 44.246,  44.491]\n\n");
    std::printf("Unresolved interval: [%.3f, 44.369], a gap of %.3f shots.\n", wf.bound,
                44.369 - wf.bound);
    std::printf("The water-filling rung raises the floor from %d to %.3f, closing %.1f%% of the\n",
                k::kCoverageBound, wf.bound,
                100.0 * (wf.bound - k::kCoverageBound) / (44.369 - k::kCoverageBound));
    std::printf("distance to the best measured policy.\n");

    const bool ok = blocked == 0 && betas[0] == 50 && betas[3] == 20 &&
                    wf.hitTranscripts == 28560 && wf.bound > 24.0 && wf.bound < 24.1;
    std::printf("\n%s\n", ok ? "OK" : "*** CHECK FAILED ***");
    return ok ? 0 : 1;
}
