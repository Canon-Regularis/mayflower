#include "mayflower/certify.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>
#include <vector>

namespace mayflower {
namespace {

// A progress state records how many hits each ship has taken, four bits per
// ship, packed into a single integer. Ships of equal length are
// interchangeable, so the nibbles within each length group are kept sorted.
using Progress = std::uint64_t;

constexpr int kMaxShips = 16;

int nibble(Progress p, std::size_t i) {
    return static_cast<int>((p >> (4 * i)) & 0xFull);
}

Progress setNibble(Progress p, std::size_t i, int value) {
    const std::uint64_t shift = 4 * i;
    return (p & ~(0xFull << shift)) | (static_cast<std::uint64_t>(value) << shift);
}

Progress canonicalise(Progress p, const std::vector<int>& lengths) {
    std::size_t i = 0;
    while (i < lengths.size()) {
        std::size_t j = i;
        while (j < lengths.size() && lengths[j] == lengths[i]) ++j;
        std::vector<int> group;
        for (std::size_t k = i; k < j; ++k) group.push_back(nibble(p, k));
        std::sort(group.begin(), group.end());
        for (std::size_t k = i; k < j; ++k) p = setNibble(p, k, group[k - i]);
        i = j;
    }
    return p;
}

// Announcement alphabet: a plain hit, or a hit that sinks a ship of length L.
struct Symbol {
    bool sinks = false;
    int length = 0;
};

void step(Progress p, const std::vector<int>& lengths, const Symbol& sym,
          std::vector<Progress>& out) {
    for (std::size_t i = 0; i < lengths.size(); ++i) {
        const int L = lengths[i];
        const int have = nibble(p, i);
        if (have >= L) continue;                       // already sunk
        const bool completes = have + 1 == L;
        if (completes != sym.sinks) continue;
        if (sym.sinks && L != sym.length) continue;
        out.push_back(canonicalise(setNibble(p, i, have + 1), lengths));
    }
}

}  // namespace

// Distinct announcement strings the hits of a full fleet can produce.
//
// Several hit-to-ship assignments can yield the same string, so counting
// interleavings over-counts. The automaton over progress states is
// nondeterministic; determinising it by subset construction makes every string
// correspond to exactly one path, and the paths are then counted directly.
std::uint64_t countHitTranscripts(const std::vector<int>& fleetIn) {
    std::vector<int> lengths = fleetIn;
    std::sort(lengths.begin(), lengths.end(), std::greater<int>());
    if (lengths.size() > kMaxShips) throw std::invalid_argument("too many ships");
    for (int L : lengths)
        if (L > 15) throw std::invalid_argument("ship length must fit in a nibble");

    int totalHits = 0;
    for (int L : lengths) totalHits += L;

    std::vector<Symbol> alphabet{Symbol{false, 0}};
    {
        std::vector<int> distinct = lengths;
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
        for (int L : distinct) alphabet.push_back(Symbol{true, L});
    }

    using Subset = std::vector<Progress>;
    std::map<Subset, std::uint64_t> level{{Subset{canonicalise(0, lengths)}, 1}};

    std::vector<Progress> reached;
    for (int position = 0; position < totalHits; ++position) {
        std::map<Subset, std::uint64_t> nextLevel;
        for (const auto& entry : level) {
            for (const Symbol& sym : alphabet) {
                reached.clear();
                for (Progress p : entry.first) step(p, lengths, sym, reached);
                if (reached.empty()) continue;
                std::sort(reached.begin(), reached.end());
                reached.erase(std::unique(reached.begin(), reached.end()), reached.end());
                nextLevel[reached] += entry.second;
            }
        }
        level = std::move(nextLevel);
    }

    // Every surviving string has consumed all the hits, so every state in every
    // surviving subset is fully sunk and all of them accept.
    std::uint64_t total = 0;
    for (const auto& entry : level) total += entry.second;
    return total;
}

// Water-filling lower bound on expected shots.
//
// Against a deterministic policy the map from configuration to transcript is
// injective: the transcript replays the policy, so it reveals which cells were
// shot and which of those were hits. A game ending on shot t has k hits with the
// last at t, so its transcript is fixed by choosing the positions of the other
// k-1 among the first t-1 and by one of the K announcement strings. Hence
//
//     #{configurations finishing by t} <= K * sum_{s<=t} C(s-1, k-1) = K * C(t, k)
//
// by the hockey-stick identity, so P(T <= t) <= K*C(t,k)/N and
//
//     E[T] = sum_{t>=0} P(T > t) >= sum_t max(0, 1 - K*C(t,k)/N).
//
// The bound holds for every deterministic policy, and by conditioning on the
// seed it holds for randomised ones too.
WaterFillingResult waterFillingBound(const std::vector<int>& fleet, std::uint64_t hypotheses,
                                     int cells) {
    WaterFillingResult out;
    int shipCells = 0;
    for (int L : fleet) shipCells += L;
    out.hitTranscripts = countHitTranscripts(fleet);
    out.shipCells = shipCells;

    // The survival term divides by the hypothesis count. With none, every term
    // was 1 - x/0 and the bound came back NaN, which is worse than refusing:
    // NaN compares false against everything, so a caller testing whether this
    // rung beats another silently keeps the other, and a report prints "nan".
    if (hypotheses == 0)
        throw std::invalid_argument("water filling needs at least one configuration to bound");
    if (cells < 0)
        throw std::invalid_argument("cell count must not be negative");

    // C(t, k) as a double; C(100,17) is about 3.4e19, well inside range.
    const auto binomial = [](int n, int k) -> double {
        if (k < 0 || n < k) return 0.0;
        double r = 1.0;
        for (int i = 1; i <= k; ++i) r = r * (n - k + i) / i;
        return r;
    };

    double bound = 0.0;
    for (int t = 0; t <= cells; ++t) {
        const double reachable = static_cast<double>(out.hitTranscripts) * binomial(t, shipCells);
        const double survival = 1.0 - reachable / static_cast<double>(hypotheses);
        if (survival <= 0.0) {
            out.saturatesAt = t;
            break;
        }
        bound += survival;
        out.saturatesAt = t + 1;
    }
    out.bound = bound;
    return out;
}

}  // namespace mayflower
