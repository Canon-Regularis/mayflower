// Game harness and policy legality.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/game.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/policy.hpp"

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

using namespace mayflower;

// A uniform shooter finishes on the last of the k ship cells, so with N cells
// E[T] = k(N+1)/(k+1). Any harness bug in termination, sink accounting or the
// board bank moves this number.
void testRandomShooterClosedForm() {
    std::printf("[random shooter closed form]\n");
    struct Case { int w, h; std::vector<int> fleet; };
    const std::vector<Case> cases = {{6, 6, {4, 3, 2}}, {8, 8, {4, 3, 3, 2}}};

    for (const Case& c : cases) {
        const Instance inst(c.w, c.h, c.fleet);
        const BoardBank bank(inst, 0x1234u);
        RandomPolicy policy;
        const int games = 40000;
        double sum = 0, ss = 0;
        for (int i = 0; i < games; ++i) {
            const int shots = playGame(inst, bank.board(static_cast<std::uint64_t>(i)), policy,
                                       static_cast<std::uint64_t>(i)).shots;
            sum += shots;
            ss += static_cast<double>(shots) * shots;
        }
        const double mean = sum / games;
        const double sd = std::sqrt(ss / games - mean * mean);
        const double theory = static_cast<double>(inst.shipCells()) * (inst.cellCount() + 1) /
                              (inst.shipCells() + 1);
        const double half = 1.96 * sd / std::sqrt(static_cast<double>(games));
        check(std::abs(mean - theory) < half * 1.5,
              inst.describe() + " random shooter matches k(N+1)/(k+1)");
        std::printf("  %-16s measured %.4f +/- %.4f, theory %.4f\n", inst.describe().c_str(), mean,
                    half, theory);
    }
}

// Every policy must terminate, never reshoot, and never finish below the
// coverage bound.
void testPolicyLegality() {
    std::printf("[policy legality]\n");
    const Instance inst = standardInstance();
    const BoardBank bank(inst, 0xBEEFu);

    std::vector<std::unique_ptr<Policy>> policies;
    policies.push_back(std::make_unique<RandomPolicy>());
    policies.push_back(std::make_unique<ParityHuntTarget>());
    policies.push_back(std::make_unique<DensityPolicy>());

    for (auto& p : policies) {
        int worst = 0, best = 1000;
        for (int i = 0; i < 300; ++i) {
            const auto result = playGame(inst, bank.board(static_cast<std::uint64_t>(i)), *p,
                                         static_cast<std::uint64_t>(i));
            check(result.shots >= constants::kCoverageBound,
                  std::string(p->name()) + " never finishes below the coverage bound");
            check(result.shots <= inst.cellCount(),
                  std::string(p->name()) + " never exceeds the board size");
            check(result.shots - result.misses == inst.shipCells(),
                  std::string(p->name()) + " hits exactly the ship cells");
            worst = std::max(worst, result.shots);
            best = std::min(best, result.shots);
        }
        std::printf("  %-20s 300 games, shots in [%d, %d]\n", p->name(), best, worst);
    }
}

// Every stochastic policy ends in Stream::below(free.size()). Asked to choose
// with nothing free it reached `% 0` and the process died on a division by
// zero. playGameTraced never does that, since it stops once the ship cells are
// gone, but chooseShot is public and a crash is not a diagnosis.
void testPoliciesRefuseAFullBoard() {
    std::printf("[no cell left to choose]\n");
    const Instance inst(4, 4, {2});
    History full(inst);
    for (int c = 0; c < inst.cellCount(); ++c)
        full.add(c / inst.width, c % inst.width, Outcome::Miss);

    std::vector<std::unique_ptr<Policy>> policies;
    policies.push_back(std::make_unique<RandomPolicy>());
    policies.push_back(std::make_unique<ParityHuntTarget>());

    for (auto& p : policies) {
        ++gChecks;
        p->reset(inst, 1);
        try {
            const int cell = p->chooseShot(inst, full);
            ++gFailures;
            std::printf("  FAIL  %s returned cell %d from a full board\n", p->name(), cell);
        } catch (const std::invalid_argument&) {
            std::printf("  %-20s refuses rather than dividing by zero\n", p->name());
        }
    }

    // The density policy has its own fallback and must still name a cell while
    // one is free, so the guard has not made the common path throw.
    ++gChecks;
    History empty(inst);
    DensityPolicy d;
    d.reset(inst, 1);
    const int c = d.chooseShot(inst, empty);
    if (c < 0 || c >= inst.cellCount()) {
        ++gFailures;
        std::printf("  FAIL  density returned %d on an empty board\n", c);
    } else {
        std::printf("  density still chooses %d when the board is untouched\n", c);
    }
}

// Determinism: the same seed and board must reproduce the same shot count.
void testDeterminism() {
    std::printf("[determinism]\n");
    const Instance inst = standardInstance();
    const BoardBank bank(inst, 0x77u);
    std::vector<std::unique_ptr<Policy>> policies;
    policies.push_back(std::make_unique<RandomPolicy>());
    policies.push_back(std::make_unique<ParityHuntTarget>());
    policies.push_back(std::make_unique<DensityPolicy>());

    for (auto& p : policies) {
        for (int i = 0; i < 50; ++i) {
            const auto board = bank.board(static_cast<std::uint64_t>(i));
            const int a = playGame(inst, board, *p, static_cast<std::uint64_t>(i)).shots;
            const int b = playGame(inst, board, *p, static_cast<std::uint64_t>(i)).shots;
            check(a == b, std::string(p->name()) + " is reproducible for a given seed");
        }
    }
    std::printf("  all policies reproduce their shot counts\n");
}

// The bank must depend only on (key, game id), never on call order.
void testBankIsOrderIndependent() {
    std::printf("[board bank]\n");
    const Instance inst = standardInstance();
    const BoardBank bank(inst, 0x99u);
    const auto forward = bank.board(7);
    const auto interleavedA = bank.board(3);
    const auto interleavedB = bank.board(11);
    (void)interleavedA;
    (void)interleavedB;
    const auto again = bank.board(7);
    ++gChecks;
    bool same = forward.size() == again.size();
    for (std::size_t i = 0; same && i < forward.size(); ++i)
        same = forward[i].row == again[i].row && forward[i].col == again[i].col &&
               forward[i].length == again[i].length && forward[i].horizontal == again[i].horizontal;
    if (!same) { ++gFailures; std::printf("  FAIL  bank depends on call order\n"); }
    std::printf("  board(7) is stable across interleaved calls\n");
}

// An instance can pass validate() and still admit nothing, because validate()
// checks each ship against the board and never the fleet against the space.
// 2x2 {2,2,2} needs six cells and has four. The bank used to compute
// UINT64_MAX % 0 on the first draw and die on a division by zero.
void testEmptyBankIsRefused() {
    std::printf("[empty board bank]\n");
    const Instance inst(2, 2, {2, 2, 2});
    ++gChecks;
    try {
        const BoardBank bank(inst, 0x1u);
        (void)bank.board(0);
        ++gFailures;
        std::printf("  FAIL  a bank over an empty space produced a board\n");
    } catch (const std::invalid_argument&) {
        std::printf("  %s is refused rather than divided by\n", inst.describe().c_str());
    }

    // The instance itself is legal, so the refusal has to come from the bank.
    ++gChecks;
    if (Sampler(inst).total() != 0) {
        ++gFailures;
        std::printf("  FAIL  the premise moved: this instance now admits boards\n");
    } else {
        std::printf("  and the instance validates, so only the bank can catch it\n");
    }
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    testRandomShooterClosedForm();
    testPolicyLegality();
    testPoliciesRefuseAFullBoard();
    testDeterminism();
    testBankIsOrderIndependent();
    testEmptyBankIsRefused();

    const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, dt);
    return gFailures == 0 ? 0 : 1;
}
