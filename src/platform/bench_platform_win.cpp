#include "mayflower/platform.hpp"

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   // EfficiencyClass needs Windows 10
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <vector>

namespace mayflower::platform {

#ifdef _WIN32

std::vector<LogicalCore> enumerateCores() {
    std::vector<LogicalCore> cores;
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (bytes == 0) return cores;

    std::vector<char> buffer(bytes);
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes))
        return cores;

    DWORD offset = 0;
    while (offset < bytes) {
        auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() +
                                                                                offset);
        if (info->Relationship == RelationProcessorCore) {
            // MinGW's SDK headers predate EfficiencyClass, which sits at a fixed
            // offset in PROCESSOR_RELATIONSHIP: Flags is byte 0 and
            // EfficiencyClass is byte 1. Reading it positionally works on both
            // old and new headers. Zero on pre-Windows-10, which degrades to a
            // single class and is handled.
            const auto* raw = reinterpret_cast<const unsigned char*>(&info->Processor);
            const int efficiency = static_cast<int>(raw[1]);
            for (WORD g = 0; g < info->Processor.GroupCount; ++g) {
                const KAFFINITY affinity = info->Processor.GroupMask[g].Mask;
                bool first = true;
                for (int bit = 0; bit < 64; ++bit) {
                    if ((affinity >> bit) & 1u) {
                        LogicalCore core;
                        core.index = bit;
                        core.efficiencyClass = efficiency;
                        core.smtSibling = !first;
                        cores.push_back(core);
                        first = false;
                    }
                }
            }
        }
        offset += info->Size;
    }
    std::sort(cores.begin(), cores.end(),
              [](const LogicalCore& a, const LogicalCore& b) { return a.index < b.index; });
    return cores;
}

bool pinToCore(int logicalIndex) {
    if (logicalIndex < 0 || logicalIndex >= 64) return false;
    const DWORD_PTR mask = static_cast<DWORD_PTR>(1) << logicalIndex;
    return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
}

bool unpin() {
    // The process mask rather than ~0. A thread mask naming a processor the
    // process does not own is documented to fail with ERROR_INVALID_PARAMETER;
    // this build happens to tolerate ~0, but a run under a job object or
    // `start /affinity` would not, and the failure would be silent.
    DWORD_PTR processMask = 0, systemMask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask))
        return false;
    if (processMask == 0) return false;
    return SetThreadAffinityMask(GetCurrentThread(), processMask) != 0;
}

#else

std::vector<LogicalCore> enumerateCores() { return {}; }
bool pinToCore(int) { return false; }
bool unpin() { return false; }

#endif

namespace {

// Prefer a core that does not share a physical core with another logical one, so
// an SMT sibling running something else cannot distort the measurement.
//
// Processor 0 is avoided when anything else is available: Windows routes device
// interrupts and DPCs there by default, which shows up as sporadic multi-second
// stalls in an otherwise clean run.
int pickCore(bool fastest) {
    const auto cores = enumerateCores();
    if (cores.empty()) return -1;
    int wantedClass = cores.front().efficiencyClass;
    for (const LogicalCore& c : cores) {
        if (fastest) wantedClass = std::max(wantedClass, c.efficiencyClass);
        else         wantedClass = std::min(wantedClass, c.efficiencyClass);
    }
    int fallback = -1;
    for (const LogicalCore& c : cores) {
        if (c.efficiencyClass != wantedClass) continue;
        if (c.smtSibling) { if (fallback < 0) fallback = c.index; continue; }
        if (c.index != 0) return c.index;
        fallback = c.index;
    }
    return fallback;
}

}  // namespace

bool pinToFastestCore() {
    const int core = pickCore(true);
    return core >= 0 && pinToCore(core);
}

bool pinToSlowestCore() {
    const int core = pickCore(false);
    return core >= 0 && pinToCore(core);
}

std::string describeTopology() {
    const auto cores = enumerateCores();
    if (cores.empty()) return "topology unavailable";

    int classes = 0;
    std::vector<int> perClass;
    for (const LogicalCore& c : cores) {
        while (static_cast<int>(perClass.size()) <= c.efficiencyClass) perClass.push_back(0);
        ++perClass[static_cast<std::size_t>(c.efficiencyClass)];
    }
    for (int n : perClass) if (n > 0) ++classes;

    std::string out = std::to_string(cores.size()) + " logical processors across " +
                      std::to_string(classes) + " efficiency classes:";
    for (std::size_t i = 0; i < perClass.size(); ++i) {
        if (perClass[i] == 0) continue;
        out += "\n  class " + std::to_string(i) + ": " + std::to_string(perClass[i]) +
               " logical (";
        bool firstOne = true;
        for (const LogicalCore& c : cores) {
            if (c.efficiencyClass != static_cast<int>(i)) continue;
            if (!firstOne) out += ",";
            out += std::to_string(c.index);
            if (c.smtSibling) out += "*";
            firstOne = false;
        }
        out += ")";
    }
    out += "\n  * marks an SMT sibling";
    return out;
}

}  // namespace mayflower::platform
