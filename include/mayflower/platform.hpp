// Measurement platform.
//
// The target is a hybrid part: 2 SMT P-cores plus 8 E-cores. An unpinned run can
// land on either, and the same workload has been observed with a 1.7x spread
// from that alone, which is the same size as the effects the ladder is trying to
// measure. Every benchmark pins first.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mayflower::platform {

struct LogicalCore {
    int index = 0;             // logical processor number within the group
    int efficiencyClass = 0;   // higher is faster; Windows reports P-cores above E-cores
    bool smtSibling = false;   // shares a physical core with an earlier logical core
};

// Empty when the topology cannot be read.
std::vector<LogicalCore> enumerateCores();

// Pin the calling thread to one logical processor. Returns false on failure.
bool pinToCore(int logicalIndex);

// Pin to a core of the fastest or slowest class available.
bool pinToFastestCore();
bool pinToSlowestCore();

// Undo pinning.
void unpin();

std::string describeTopology();

}  // namespace mayflower::platform
