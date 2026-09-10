// The outcome encoding the exact searchers share, and the function that reads it.
//
// A shot against a known board answers one of three things, and the encoding
// packs the sunk length into the third: 0 is a miss, 1 is a plain hit, and
// 2 + L is the shot that sank a ship of length L. src/search/exact_solver.cpp
// and tools/opponent.cpp both branch on that encoding, and both carried their
// own copy of outcomeOf, character identical apart from one using these names
// and the other bare literals. opponent.cpp then decoded the same encoding a
// third time by hand.
//
// Only the encoding and its reader are shared. Each caller keeps its own
// buildWorld, because those are genuinely different functions: they agree on
// under half their lines, and the opponent's also accumulates border scores,
// placement slot indices and the placements themselves. Merging them would be
// forcing two things together on the strength of a shared name.
//
// Internal to src/search.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mayflower::search {

// One bit per cell. Both callers refuse boards above 32 cells before building.
using Mask = std::uint32_t;

constexpr int kMiss = 0;
constexpr int kHit = 1;
constexpr int kSunkBase = 2;   // + the length of the ship that sank

// What every reader of the encoding needs to know about the enumerated boards.
// A caller carrying more extends this rather than restating it.
struct WorldBase {
    int cells = 0;
    std::vector<Mask> occupancy;                     // per board, cells occupied
    std::vector<std::vector<std::int8_t>> ship;      // per board, ship index per cell
    std::vector<std::vector<Mask>> shipMask;         // per board, cells of each ship
    std::vector<std::vector<std::int8_t>> shipLength;
};

// The answer board `b` gives to a shot at `cell`, given the cells already shot.
//
// Templated on the board index only so both callers keep their own width: the
// solver indexes with a uint16 ConfigId, the opponent tool with a size_t.
template <typename Id>
[[nodiscard]] inline int outcomeOf(const WorldBase& w, Id b, int cell, Mask shot) {
    const std::size_t i = static_cast<std::size_t>(b);
    if ((w.occupancy[i] & (Mask{1} << cell)) == 0) return kMiss;
    const int s = w.ship[i][static_cast<std::size_t>(cell)];
    const Mask others = w.shipMask[i][static_cast<std::size_t>(s)] & ~(Mask{1} << cell);
    // The ship survives while any of its other cells is still unshot.
    if ((others & ~shot) != 0) return kHit;
    return kSunkBase + w.shipLength[i][static_cast<std::size_t>(s)];
}

}  // namespace mayflower::search
