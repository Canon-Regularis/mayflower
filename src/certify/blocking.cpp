#include "mayflower/certify.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <vector>

namespace mayflower {
namespace {

// A cell set is "free" when no L consecutive cells of it appear in any row or
// column. Maximising the free set and subtracting from W*H gives beta(L).
//
// Sweep cells in row-major order. State is the trailing vertical run per column,
// each in 0..L-1 and packed as a base-L digit, plus the trailing horizontal run
// in the current row. Both runs must stay below L, since reaching L would place
// L consecutive free cells in a line.
//
// The array is dense, not hashed, and the cell loop is decomposed into
// (high, digit, low) so extracting the current column's digit needs no division.
// Free, Blocked, or not yet decided. The DP takes these so the self-reduction
// can ask what the best free set is that respects the decisions made so far.
enum class Decision : std::int8_t { Either = 0, Free = 1, Blocked = 2 };

struct FreeSetDp {
    int W, H, L;
    std::vector<std::int64_t> pow;   // pow[c] = L^c
    std::int64_t vertStates = 1;
    std::vector<std::int8_t> cur, next;
    const std::vector<Decision>* fixed = nullptr;   // null means every cell is free to choose

    FreeSetDp(int w, int h, int l) : W(w), H(h), L(l) {
        pow.assign(static_cast<std::size_t>(W) + 1, 1);
        for (int c = 1; c <= W; ++c) pow[static_cast<std::size_t>(c)] = pow[static_cast<std::size_t>(c) - 1] * L;
        vertStates = pow[static_cast<std::size_t>(W)];
        const std::int64_t total = vertStates * L;
        if (total > (std::int64_t{1} << 31))
            throw std::invalid_argument("blocking DP state space too large for this board");
        cur.assign(static_cast<std::size_t>(total), -1);
        next.assign(static_cast<std::size_t>(total), -1);
    }

    [[nodiscard]] int run() {
        std::fill(cur.begin(), cur.end(), -1);
        cur[0] = 0;
        for (int row = 0; row < H; ++row) {
            for (int col = 0; col < W; ++col) step(row, col);
            foldRow();
        }
        std::int8_t best = -1;
        for (std::int64_t v = 0; v < vertStates; ++v)
            best = std::max(best, cur[static_cast<std::size_t>(v * L)]);
        return best;
    }

    void step(int row, int col) {
        std::fill(next.begin(), next.end(), -1);
        const std::int64_t low = pow[static_cast<std::size_t>(col)];
        const std::int64_t high = vertStates / (low * L);
        const Decision want = fixed ? (*fixed)[static_cast<std::size_t>(row * W + col)]
                                    : Decision::Either;
        const bool mayBlock = want != Decision::Free;
        const bool mayTake = want != Decision::Blocked;

        for (std::int64_t h = 0; h < high; ++h) {
            const std::int64_t hb = h * low * L;
            for (int d = 0; d < L; ++d) {
                const std::int64_t db = hb + static_cast<std::int64_t>(d) * low;
                for (std::int64_t lo = 0; lo < low; ++lo) {
                    const std::int64_t vert = db + lo;
                    for (int horiz = 0; horiz < L; ++horiz) {
                        const std::int8_t value = cur[static_cast<std::size_t>(vert * L + horiz)];
                        if (value < 0) continue;

                        // Cell excluded from the free set: both runs restart.
                        if (mayBlock) {
                            const std::int64_t clearedVert = hb + lo;
                            std::int8_t& blocked =
                                next[static_cast<std::size_t>(clearedVert * L)];
                            blocked = std::max(blocked, value);
                        }

                        // Cell in the free set: both runs extend and must stay below L.
                        if (mayTake && d + 1 < L && horiz + 1 < L) {
                            const std::int64_t grownVert = hb + static_cast<std::int64_t>(d + 1) * low + lo;
                            std::int8_t& taken =
                                next[static_cast<std::size_t>(grownVert * L + horiz + 1)];
                            taken = std::max(taken, static_cast<std::int8_t>(value + 1));
                        }
                    }
                }
            }
        }
        cur.swap(next);
    }

    // A new row restarts the horizontal run, so collapse the horizontal digit.
    void foldRow() {
        for (std::int64_t v = 0; v < vertStates; ++v) {
            std::int8_t best = -1;
            for (int horiz = 0; horiz < L; ++horiz)
                best = std::max(best, cur[static_cast<std::size_t>(v * L + horiz)]);
            cur[static_cast<std::size_t>(v * L)] = best;
            for (int horiz = 1; horiz < L; ++horiz)
                cur[static_cast<std::size_t>(v * L + horiz)] = -1;
        }
    }
};

}  // namespace

BlockingResult blockingNumber(int width, int height, int length) {
    if (width <= 0 || height <= 0 || length <= 0)
        throw std::invalid_argument("blockingNumber needs positive dimensions");
    BlockingResult out;
    if (length > width && length > height) {   // no placement exists, nothing to block
        out.blocking = 0;
        out.largestFreeSet = width * height;
        return out;
    }
    const auto t0 = std::chrono::steady_clock::now();
    FreeSetDp dp(width, height, length);
    out.largestFreeSet = dp.run();
    out.blocking = width * height - out.largestFreeSet;
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

// Greedy cover: repeatedly shoot the cell meeting the most still-unmet
// placements. It is an upper bound on beta(L) and on this board it happens to
// reach it for lengths 2 and 5 and to miss by one and two for 3 and 4.
std::vector<int> greedyCover(int width, int height, int length) {
    std::vector<std::vector<int>> placements;
    for (int r = 0; r < height; ++r)
        for (int c = 0; c + length <= width; ++c) {
            std::vector<int> p;
            for (int k = 0; k < length; ++k) p.push_back(r * width + c + k);
            placements.push_back(std::move(p));
        }
    if (length > 1)
        for (int c = 0; c < width; ++c)
            for (int r = 0; r + length <= height; ++r) {
                std::vector<int> p;
                for (int k = 0; k < length; ++k) p.push_back((r + k) * width + c);
                placements.push_back(std::move(p));
            }

    std::vector<char> met(placements.size(), 0);
    std::vector<int> chosen;
    std::size_t remaining = placements.size();
    while (remaining > 0) {
        std::vector<int> gain(static_cast<std::size_t>(width * height), 0);
        for (std::size_t i = 0; i < placements.size(); ++i) {
            if (met[i]) continue;
            for (int cell : placements[i]) ++gain[static_cast<std::size_t>(cell)];
        }
        int best = 0;
        for (int cell = 1; cell < width * height; ++cell)
            if (gain[static_cast<std::size_t>(cell)] > gain[static_cast<std::size_t>(best)]) best = cell;
        chosen.push_back(best);
        for (std::size_t i = 0; i < placements.size(); ++i) {
            if (met[i]) continue;
            if (std::find(placements[i].begin(), placements[i].end(), best) != placements[i].end()) {
                met[i] = 1;
                --remaining;
            }
        }
    }
    return chosen;
}

// Self-reduction to a minimum witness. Walk the cells in order, try each one as
// free, and keep that choice when the DP says a maximum free set still exists
// under the decisions made so far. The invariant is that the decided prefix
// always extends to some maximum free set, so after the last cell every cell is
// decided and the blocked ones number exactly beta(L).
//
// One DP run per cell. The caller decides whether that is affordable, since the
// DP's own cost is what makes it so.
std::vector<int> minimumCover(int width, int height, int length, int target) {
    std::vector<Decision> fixed(static_cast<std::size_t>(width * height), Decision::Either);
    for (int cell = 0; cell < width * height; ++cell) {
        fixed[static_cast<std::size_t>(cell)] = Decision::Free;
        FreeSetDp dp(width, height, length);
        dp.fixed = &fixed;
        if (dp.run() < target) fixed[static_cast<std::size_t>(cell)] = Decision::Blocked;
    }
    std::vector<int> chosen;
    for (int cell = 0; cell < width * height; ++cell)
        if (fixed[static_cast<std::size_t>(cell)] == Decision::Blocked) chosen.push_back(cell);
    return chosen;
}

BlockingWitness blockingWitness(int width, int height, int length) {
    const BlockingResult b = blockingNumber(width, height, length);

    BlockingWitness out;
    out.cells = greedyCover(width, height, length);
    out.optimal = static_cast<int>(out.cells.size()) == b.blocking;
    if (out.optimal) return out;

    // The reduction costs one DP run per cell, so the work is the cell count
    // squared times the DP's state space. Gate on that rather than on the
    // measured seconds: a wall-clock gate makes the figure depend on how busy
    // the machine was, and would draw 24 cells on an idle run and 26 on a loaded
    // one. On the standard board this admits lengths up to 4 and leaves 5 to the
    // greedy cover, which already reaches beta(5) anyway.
    constexpr double kBudgetOps = 1e11;
    double states = 1;
    for (int c = 0; c <= width; ++c) states *= length;
    const double cells = static_cast<double>(width) * height;
    if (cells * cells * states > kBudgetOps) return out;

    out.cells = minimumCover(width, height, length, b.largestFreeSet);
    out.optimal = static_cast<int>(out.cells.size()) == b.blocking;
    out.selfReduced = true;
    return out;
}

}  // namespace mayflower
