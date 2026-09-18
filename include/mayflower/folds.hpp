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
#include <stdexcept>
#include <string>

#include "mayflower/random.hpp"

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

// A fixed salt, so fold membership is independent of every other use of the
// same board id.
inline constexpr std::uint64_t kFoldSalt = 0x5DEECE66Dull;

namespace detail {

// splitmix64 at a salted counter position. random.hpp used to describe this as
// "a salted variant" of splitmix64 that "must stay separate", and it is not a
// variant: splitmix64(x) is mix64(x + kGoldenGamma), and the three lines
// written out here were mix64(boardId + kGoldenGamma + kFoldSalt). The salt is
// an input to the same function, not a different function, and this header did
// not even include the one that defines it.
//
// The rewrite is bit-identical, which is what lets the vector pinned in
// tests/test_folds.cpp and the mirror in python/stats.py stand unchanged.
inline std::uint64_t foldHash(std::uint64_t boardId) {
    return splitmix64(boardId + kFoldSalt);
}

}  // namespace detail

// The fraction a board hashes to, in [0, 1).
[[nodiscard]] inline double foldFraction(std::uint64_t boardId) {
    return static_cast<double>(detail::foldHash(boardId) >> 11) / kTwoPow53;
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
