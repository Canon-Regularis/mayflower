// The gated path, which nothing else exercises.
//
// `constraintsFrom(inst, history)` produces more than a per-cell filter: it also
// fills allowH and allowV, a per-placement gate consulted on every ship START.
// That gate is what carries sunk semantics, and it is the correctness hazard the
// whole project is organised around.
//
// Both sweeps added late take those gated constraints and neither was ever run
// with them. The no-touching sweep and the weighted sweep were tested only with
// plain cell constraints, so the gate was wired in and never checked. This file
// is the check.
//
// Two differentials, both on randomly fuzzed ordered histories:
//
//   weighted    with every weight at 1 the partition function is the count, so
//               the weighted sweep must equal the integer sweep exactly on any
//               constraints at all, gated ones included.
//
//   no-touching against literal enumeration that replays the same history under
//               the same rules, sharing no code with the sweep.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "mayflower/notouch.hpp"
#include "mayflower/observations.hpp"
#include "mayflower/profile_dp.hpp"
#include "mayflower/weighted.hpp"
#include "oracle/brute_force.hpp"

namespace {

using namespace mayflower;

int failures = 0;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    std::printf("  %-62s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!detail.empty()) std::printf("      %s\n", detail.c_str());
    if (!ok) ++failures;
}

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed) {}
    std::uint64_t next() {
        s += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
};

// A random ordered history played against a random true board, so SUNK lands in
// a real position rather than a synthetic one.
History fuzz(const Instance& inst, const Sampler& sampler, Rng& rng, int shots) {
    const auto truth = sampler.unrank(rng.next() % sampler.total());
    std::vector<int> shipAt(static_cast<std::size_t>(inst.cellCount()), -1);
    std::vector<int> remaining(truth.size(), 0);
    for (std::size_t i = 0; i < truth.size(); ++i) {
        remaining[i] = truth[i].length;
        for (int k = 0; k < truth[i].length; ++k) {
            const int cell = truth[i].horizontal
                                 ? truth[i].row * inst.width + truth[i].col + k
                                 : (truth[i].row + k) * inst.width + truth[i].col;
            shipAt[static_cast<std::size_t>(cell)] = static_cast<int>(i);
        }
    }
    std::vector<int> pool(static_cast<std::size_t>(inst.cellCount()));
    for (int i = 0; i < inst.cellCount(); ++i) pool[static_cast<std::size_t>(i)] = i;
    for (int i = inst.cellCount() - 1; i > 0; --i)
        std::swap(pool[static_cast<std::size_t>(i)],
                  pool[static_cast<std::size_t>(rng.below(i + 1))]);

    History h(inst);
    for (int i = 0; i < shots && i < inst.cellCount(); ++i) {
        const int cell = pool[static_cast<std::size_t>(i)];
        const int ship = shipAt[static_cast<std::size_t>(cell)];
        if (ship < 0) {
            h.add(cell / inst.width, cell % inst.width, Outcome::Miss);
        } else if (--remaining[static_cast<std::size_t>(ship)] == 0) {
            h.add(cell / inst.width, cell % inst.width, Outcome::Sunk,
                  truth[static_cast<std::size_t>(ship)].length);
        } else {
            h.add(cell / inst.width, cell % inst.width, Outcome::Hit);
        }
    }
    return h;
}

// Every weight at 1 makes the partition function the count, whatever the
// constraints are. A gate the weighted sweep mishandled would show up here as a
// mismatch against the integer sweep that has been checked against enumeration
// since M1.
void testWeightedGated() {
    std::printf("[weighted sweep, gated constraints]\n");
    struct Case { int w, h; std::vector<int> fleet; };
    Rng rng(0x9E3779B9);
    int agreed = 0, trials = 0, nonTrivial = 0;

    for (const Case& c : std::vector<Case>{{5, 5, {3, 2, 2}}, {6, 6, {4, 3, 2}},
                                           {5, 5, {4, 3, 2}}, {6, 5, {3, 3, 2}}}) {
        const Instance inst(c.w, c.h, c.fleet);
        const Sampler sampler(inst);
        for (int t = 0; t < 40; ++t) {
            const History h = fuzz(inst, sampler, rng, 8 + rng.below(14));
            const Constraints gated = constraintsFrom(inst, h);
            if (!gated.gated()) continue;   // nothing to test if no gate was built
            for (const int cell : h.sequence())
                if (h.outcome(cell) == Outcome::Sunk) { ++nonTrivial; break; }
            ++trials;
            const std::uint64_t exact = countConfigurations(inst, gated).count;
            const WeightedResult wr = weightedCount(inst, gated, Weights::uniform());
            if (wr.total == static_cast<double>(exact)) ++agreed;
            else
                std::printf("      %s trial %d: integer %llu, weighted %.1f\n",
                            inst.describe().c_str(), t,
                            static_cast<unsigned long long>(exact), wr.total);
        }
    }
    char label[128];
    std::snprintf(label, sizeof label, "%d/%d gated histories agree bit for bit",
                  agreed, trials);
    check(agreed == trials && trials > 0, label);
    std::snprintf(label, sizeof label, "%d of them carried a SUNK, which is the gate",
                  nonTrivial);
    check(nonTrivial > trials / 4, label);
}

// Playing a history against a board given as ship masks, and deciding whether a
// board could have produced it. Written here rather than reused, so the check
// shares nothing with the sweep it is checking.
int outcomeAgainst(const std::vector<oracle::Mask>& ships, int cell,
                   oracle::Mask alreadyShot) {
    for (const oracle::Mask m : ships) {
        if (((m >> cell) & oracle::Mask{1}) == 0) continue;
        const oracle::Mask rest = m & ~(oracle::Mask{1} << cell);
        if ((rest & ~alreadyShot) != 0) return 1;               // hit, ship survives
        return 2 + oracle::popcount128(m);                      // this shot sank it
    }
    return 0;                                                   // miss
}

History historyFrom(const Instance& inst, const std::vector<oracle::Mask>& ships,
                    Rng& rng, int shots) {
    std::vector<int> pool(static_cast<std::size_t>(inst.cellCount()));
    for (int i = 0; i < inst.cellCount(); ++i) pool[static_cast<std::size_t>(i)] = i;
    for (int i = inst.cellCount() - 1; i > 0; --i)
        std::swap(pool[static_cast<std::size_t>(i)],
                  pool[static_cast<std::size_t>(rng.below(i + 1))]);

    History h(inst);
    oracle::Mask shot = 0;
    for (int i = 0; i < shots && i < inst.cellCount(); ++i) {
        const int cell = pool[static_cast<std::size_t>(i)];
        const int o = outcomeAgainst(ships, cell, shot);
        shot |= oracle::Mask{1} << cell;
        if (o == 0) h.add(cell / inst.width, cell % inst.width, Outcome::Miss);
        else if (o == 1) h.add(cell / inst.width, cell % inst.width, Outcome::Hit);
        else h.add(cell / inst.width, cell % inst.width, Outcome::Sunk, o - 2);
    }
    return h;
}

bool consistent(const std::vector<oracle::Mask>& ships, const Instance& inst,
                const History& h) {
    oracle::Mask shot = 0;
    for (const int cell : h.sequence()) {
        const int got = outcomeAgainst(ships, cell, shot);
        shot |= oracle::Mask{1} << cell;
        const Outcome want = h.outcome(cell);
        if (want == Outcome::Miss && got != 0) return false;
        if (want == Outcome::Hit && got != 1) return false;
        if (want == Outcome::Sunk && got != 2 + h.sunkLength(cell)) return false;
    }
    (void)inst;
    return true;
}

// The same histories against the no-touching sweep, checked by enumeration.
void testNoTouchGated() {
    std::printf("[no-touching sweep, gated constraints]\n");
    struct Case { int w, h; std::vector<int> fleet; };
    Rng rng(0xC0FFEE11);
    int agreed = 0, trials = 0, withSunk = 0;

    for (const Case& c : std::vector<Case>{{5, 5, {3, 2}}, {5, 5, {4, 3, 2}},
                                           {4, 4, {3, 2}}, {6, 5, {3, 2}}}) {
        const Instance inst(c.w, c.h, c.fleet);
        // The truth has to obey the ruleset under test, or SUNK could name a
        // ship no legal board holds.
        const auto boards = oracle::enumerateBoardsNoTouch(c.w, c.h, c.fleet);
        if (boards.empty()) continue;

        for (int t = 0; t < 30; ++t) {
            const auto& truth =
                boards[static_cast<std::size_t>(rng.below(static_cast<int>(boards.size())))];
            const History h = historyFrom(inst, truth, rng, 8 + rng.below(14));
            for (const int cell : h.sequence())
                if (h.outcome(cell) == Outcome::Sunk) { ++withSunk; break; }

            const Constraints gated = constraintsFrom(inst, h);
            ++trials;
            const std::uint64_t sweep = countNoTouch(inst, gated).count;

            std::uint64_t brute = 0;
            for (const auto& b : boards) brute += consistent(b, inst, h) ? 1ull : 0ull;

            if (sweep == brute) ++agreed;
            else
                std::printf("      %s trial %d: sweep %llu, enumeration %llu\n",
                            inst.describe().c_str(), t,
                            static_cast<unsigned long long>(sweep),
                            static_cast<unsigned long long>(brute));
        }
    }
    char label[128];
    std::snprintf(label, sizeof label, "%d/%d gated histories agree with enumeration",
                  agreed, trials);
    check(agreed == trials && trials > 0, label);
    std::snprintf(label, sizeof label, "%d of them carried a SUNK, which is the gate",
                  withSunk);
    check(withSunk > 0, label);
}

// The forward-backward marginals, on the path nothing else reaches.
//
// weightedMarginals replays each column from a checkpoint and combines it with a
// backward pass. That machinery was only ever run on free boards with smooth
// weights. Constraints change the shape of every layer and a gate changes which
// starts are legal at all, so this is where a checkpoint or replay mistake would
// surface. The recount path shares none of it.
void testMarginalsGated() {
    std::printf("[forward-backward marginals, constrained and gated]\n");
    Rng rng(0x0FF1CE99);
    int agreed = 0, trials = 0, forcedChecked = 0;
    bool forcedOk = true;

    struct Case { int w, h; std::vector<int> fleet; };
    for (const Case& c : std::vector<Case>{{5, 5, {3, 2}}, {5, 5, {4, 3, 2}},
                                           {4, 4, {3, 2}}}) {
        const Instance inst(c.w, c.h, c.fleet);
        const Sampler sampler(inst);

        for (int t = 0; t < 12; ++t) {
            // Alternate between a gated history and hand-set cell constraints,
            // so both ways of building Constraints are covered.
            Constraints cons;
            if (t % 2 == 0) {
                cons = constraintsFrom(inst, fuzz(inst, sampler, rng, 6 + rng.below(10)));
            } else {
                cons.cells.assign(static_cast<std::size_t>(inst.cellCount()),
                                  CellConstraint::Free);
                for (int i = 0; i < inst.cellCount(); ++i) {
                    const int r = rng.below(7);
                    if (r == 0)
                        cons.cells[static_cast<std::size_t>(i)] = CellConstraint::MustBeEmpty;
                    else if (r == 1)
                        cons.cells[static_cast<std::size_t>(i)] =
                            CellConstraint::MustBeOccupied;
                }
            }
            if (weightedCount(inst, cons, Weights::uniform()).total <= 0) continue;

            // Half the trials tilt the weights as well, so the cross of
            // "constrained" and "weighted" is covered rather than each alone.
            Weights w;
            if (t % 3 == 0) {
                w.occupied.assign(static_cast<std::size_t>(inst.cellCount()), 1.0);
                w.empty.assign(static_cast<std::size_t>(inst.cellCount()), 1.0);
                for (int i = 0; i < inst.cellCount(); ++i) {
                    w.occupied[static_cast<std::size_t>(i)] = 0.6 + 0.8 * (rng.below(100) / 100.0);
                    w.empty[static_cast<std::size_t>(i)] = 0.6 + 0.8 * (rng.below(100) / 100.0);
                }
            }
            const auto fast = weightedMarginals(inst, cons, w);
            const auto slow = weightedMarginalsByRecount(inst, cons, w);
            ++trials;
            double worst = 0;
            for (std::size_t i = 0; i < fast.size(); ++i)
                worst = std::max(worst, std::abs(fast[i] - slow[i]));
            if (worst < 1e-12) ++agreed;
            else
                std::printf("      %s trial %d: largest difference %.3e\n",
                            inst.describe().c_str(), t, worst);

            // A forced cell has no freedom left, so its marginal is not
            // approximately 0 or 1, it is 0 or 1.
            for (int i = 0; i < inst.cellCount(); ++i) {
                const CellConstraint cc = cons.cells[static_cast<std::size_t>(i)];
                if (cc == CellConstraint::MustBeEmpty) {
                    ++forcedChecked;
                    if (fast[static_cast<std::size_t>(i)] != 0.0) {
                        forcedOk = false;
                        std::printf("      empty cell %d reads %.3e\n", i,
                                    fast[static_cast<std::size_t>(i)]);
                    }
                } else if (cc == CellConstraint::MustBeOccupied) {
                    ++forcedChecked;
                    if (std::abs(fast[static_cast<std::size_t>(i)] - 1.0) > 1e-12) {
                        forcedOk = false;
                        std::printf("      occupied cell %d reads %.17g\n", i,
                                    fast[static_cast<std::size_t>(i)]);
                    }
                }
            }
        }
    }
    char label[128];
    std::snprintf(label, sizeof label,
                  "%d/%d constrained boards: both routes agree to 1e-12", agreed, trials);
    check(agreed == trials && trials > 0, label);
    std::snprintf(label, sizeof label,
                  "%d forced cells read exactly 0 or exactly 1", forcedChecked);
    check(forcedOk && forcedChecked > 0, label);
}

}  // namespace

int main() {
    std::printf("gated constraints\n=================\n");
    testWeightedGated();
    testNoTouchGated();
    testMarginalsGated();
    std::printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
