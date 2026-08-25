// Fold assignment over the board-id space.
//
// Every board in the bank belongs to exactly one of TRAIN, VAL or TEST, decided
// by its id alone. Nothing about a run, a policy or a date enters, so the same
// board is in the same fold forever and two tools cannot disagree about it.
//
// The assignment hashes the id to a fraction and thresholds it. That choice is
// deliberate: adding a fourth fold later, or moving a boundary, leaves every
// board on the same side of every threshold it was not moved across, whereas a
// modulus would reshuffle the whole space. A fold is meant to survive the
// project, so it must survive its own maintenance.
//
// The identical function lives in python/stats.py, and tests/test_folds.cpp
// pins a vector both must reproduce. If the two ever drift, the analysis and the
// harness would be silently reading different data.
//
// TRAIN is for building and tuning. VAL is for choosing among finished
// candidates. TEST is sealed: reading it is an event that has to be recorded in
// experiments/audit.log before the number may be quoted.
#pragma once

#include <cstdint>
#include <string>

namespace mayflower {

enum class Fold { Train, Val, Test };

inline const char* foldName(Fold f) {
    switch (f) {
        case Fold::Train: return "train";
        case Fold::Val:   return "val";
        case Fold::Test:  return "test";
    }
    return "?";
}

namespace detail {

// splitmix64, matching the mixer used across the repository and mirrored in
// python/stats.py. Keyed with a fixed salt so fold membership is independent of
// any other use of the same id.
inline std::uint64_t foldHash(std::uint64_t boardId) {
    std::uint64_t z = boardId + 0x9E3779B97F4A7C15ull + 0x5DEECE66Dull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

}  // namespace detail

// The fraction a board hashes to, in [0, 1).
[[nodiscard]] inline double foldFraction(std::uint64_t boardId) {
    return static_cast<double>(detail::foldHash(boardId) >> 11) / 9007199254740992.0;
}

// 60 / 20 / 20. The thresholds are the only place the split is written down.
inline constexpr double kTrainShare = 0.60;
inline constexpr double kValShare = 0.20;

[[nodiscard]] inline Fold foldOf(std::uint64_t boardId) {
    const double u = foldFraction(boardId);
    if (u < kTrainShare) return Fold::Train;
    if (u < kTrainShare + kValShare) return Fold::Val;
    return Fold::Test;
}

[[nodiscard]] inline bool inFold(std::uint64_t boardId, Fold want) {
    return foldOf(boardId) == want;
}

// Parses a fold name, throwing on anything else so a typo cannot silently widen
// an experiment to the whole pool.
[[nodiscard]] inline Fold foldFromName(const std::string& name) {
    if (name == "train") return Fold::Train;
    if (name == "val") return Fold::Val;
    if (name == "test") return Fold::Test;
    throw std::invalid_argument("fold must be train, val or test, not '" + name + "'");
}

}  // namespace mayflower
