// export_pool: emit a uniform sample of configurations for the browser engine.
//
// Boards come from the unranking sampler, which is a verified bijection, so the
// pool is exactly uniform over the configuration space. Each board is five
// bytes: one placement index per ship, in fleet order.
//
// Placement index for a length-L ship on a WxH board:
//   horizontal at (row, col) -> row * (W-L+1) + col
//   vertical   at (row, col) -> H*(W-L+1) + col * (H-L+1) + row
// On 10x10 every length stays under 256, which is why one byte per ship works.
// The browser decodes with the same formula.

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/profile_dp.hpp"

namespace {

int placementIndexOf(const mayflower::Instance& inst, const mayflower::ShipPlacement& p) {
    const int W = inst.width, H = inst.height, L = p.length;
    if (p.horizontal) return p.row * (W - L + 1) + p.col;
    return H * (W - L + 1) + p.col * (H - L + 1) + p.row;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace mayflower;
    const char* path = argc > 1 ? argv[1] : "web/pool.bin";
    // atoi turns an unparsable argument into 0, and a count of 0 wrote an
    // empty pool and exited 0. The artefact this produces is read by the live
    // widget and by tests, so a silent empty file is worse than a failure.
    const int wanted = argc > 2 ? std::atoi(argv[2]) : 200000;
    if (wanted < 1) {
        std::fprintf(stderr,
                     "board count must be a positive integer; got \"%s\".\n"
                     "usage: export_pool [path] [boards] [key]\n",
                     argc > 2 ? argv[2] : "");
        return 2;
    }
    const std::uint64_t key = argc > 3 ? std::strtoull(argv[3], nullptr, 0) : 0x5A17C0DEull;

    const Instance inst = standardInstance();
    const Sampler sampler(inst);
    const std::uint64_t total = sampler.total();

    std::vector<int> order = inst.fleet;   // already descending: 5,4,3,3,2
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(wanted) * order.size());

    std::uint64_t x = key;
    const std::uint64_t limit = UINT64_MAX - (UINT64_MAX % total) - 1;
    for (int i = 0; i < wanted; ++i) {
        std::uint64_t r;
        do {
            x += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            r = z ^ (z >> 31);
        } while (r > limit);

        auto ships = sampler.unrank(r % total);
        // Emit in a fixed fleet order so the decoder knows each byte's length.
        // Fixed fleet order (5,4,3,3,2) so each byte's length is known by position.
        std::sort(ships.begin(), ships.end(),
                  [&](const ShipPlacement& a, const ShipPlacement& b) {
                      if (a.length != b.length) return a.length > b.length;
                      return placementIndexOf(inst, a) < placementIndexOf(inst, b);
                  });
        for (const ShipPlacement& p : ships) {
            const int idx = placementIndexOf(inst, p);
            if (idx < 0 || idx > 255) { std::fprintf(stderr, "index out of byte range\n"); return 1; }
            bytes.push_back(static_cast<std::uint8_t>(idx));
        }
    }

    // Binary mode explicitly. On Windows a text-mode stream rewrites every 0x0A
    // byte as 0x0D 0x0A, which silently corrupts a packed pool.
    std::FILE* out = std::fopen(path, "wb");
    if (!out) { std::fprintf(stderr, "cannot open %s\n", path); return 1; }
    const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), out);
    std::fclose(out);
    if (written != bytes.size()) { std::fprintf(stderr, "short write\n"); return 1; }

    std::fprintf(stderr, "pool: %d boards, %zu bytes -> %s, key 0x%llX, drawn from %llu\n",
                 wanted, bytes.size(), path, static_cast<unsigned long long>(key),
                 static_cast<unsigned long long>(total));
    return 0;
}
