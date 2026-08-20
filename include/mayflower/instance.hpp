// Problem instance: board dimensions plus a fleet multiset.
//
// Everything downstream is written against Instance so the brute-force oracle
// can validate the DP on small boards (4x4{3,2}, 5x5{3,2,2}, ...) where literal
// enumeration is feasible.
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "mayflower/constants.hpp"

namespace mayflower {

enum class CellConstraint : std::uint8_t {
    Free = 0,
    MustBeEmpty,       // MISS
    MustBeOccupied,    // HIT
};

struct Instance {
    int width  = constants::kBoardWidth;
    int height = constants::kBoardHeight;
    std::vector<int> fleet{constants::kFleet,
                           constants::kFleet + constants::kFleetSize};

    Instance() = default;
    Instance(int w, int h, std::vector<int> f)
        : width(w), height(h), fleet(std::move(f)) {
        std::sort(fleet.begin(), fleet.end(), std::greater<int>());
        validate();
    }

    [[nodiscard]] int cellCount() const { return width * height; }
    [[nodiscard]] int cellIndex(int row, int col) const { return row * width + col; }

    [[nodiscard]] int shipCells() const {
        int s = 0;
        for (int L : fleet) s += L;
        return s;
    }

    [[nodiscard]] int maxShipLength() const {
        return fleet.empty() ? 0 : *std::max_element(fleet.begin(), fleet.end());
    }

    [[nodiscard]] std::vector<int> distinctLengths() const {
        std::vector<int> d = fleet;
        std::sort(d.begin(), d.end());
        d.erase(std::unique(d.begin(), d.end()), d.end());
        return d;
    }

    [[nodiscard]] std::vector<int> multiplicities() const {
        std::vector<int> out;
        for (int L : distinctLengths())
            out.push_back(static_cast<int>(std::count(fleet.begin(), fleet.end(), L)));
        return out;
    }

    // Placements of a length-L ship ignoring other ships.
    [[nodiscard]] int placementsFor(int L) const {
        int n = 0;
        if (width  >= L) n += height * (width  - L + 1);
        if (height >= L) n += width  * (height - L + 1);
        return n;
    }

    [[nodiscard]] std::string describe() const {
        std::string s = std::to_string(width) + "x" + std::to_string(height) + " {";
        for (std::size_t i = 0; i < fleet.size(); ++i) {
            if (i) s += ',';
            s += std::to_string(fleet[i]);
        }
        return s + "}";
    }

    void validate() const {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument("board dimensions must be positive");
        if (width * height > 128)
            throw std::invalid_argument("Board128 holds at most 128 cells");
        if (fleet.empty())
            throw std::invalid_argument("fleet must be non-empty");
        for (int L : fleet) {
            if (L < 1) throw std::invalid_argument("ship length must be >= 1");
            if (L > width && L > height)
                throw std::invalid_argument("ship of length " + std::to_string(L) +
                                            " does not fit on the board");
        }
        // The DP packs one 3-bit residual per row.
        if (maxShipLength() > 8)
            throw std::invalid_argument("profile DP supports ship length <= 8");
        if (height > 20)
            throw std::invalid_argument("profile DP supports height <= 20");
    }
};

inline Instance standardInstance() { return Instance{}; }

}  // namespace mayflower
