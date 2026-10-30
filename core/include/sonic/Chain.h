#pragma once
// Processing chain. Order and enable state are lock-free atomics so the UI can
// reorder or bypass stages while audio runs.
//
// v0.1: the set of module instances is fixed when the chain is built; stages
// can be reordered and enabled/disabled. Dynamic insert/remove comes later.

#include "sonic/Module.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace sonic
{

class Chain
{
public:
    static constexpr int kMaxModules = 15;

    // Not thread-safe: build the chain before audio starts.
    void addModule (std::unique_ptr<Module> m);

    int size() const { return (int) modules.size(); }
    Module& module (int index) { return *modules[(size_t) index]; }
    const Module& module (int index) const { return *modules[(size_t) index]; }

    // Processing order as a list of module indices.
    std::vector<int> order() const;
    void setOrder (const std::vector<int>& newOrder);
    void moveSlot (int fromSlot, int toSlot);

    void setEnabled (int moduleIndex, bool e) { enabled[(size_t) moduleIndex]->store (e); }
    bool isEnabled (int moduleIndex) const { return enabled[(size_t) moduleIndex]->load(); }

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void process (float* const* channels, int numChannels, int numSamples);

    int latencySamples() const;

private:
    static uint64_t pack (const std::vector<int>& o);
    static std::vector<int> unpack (uint64_t v);

    std::vector<std::unique_ptr<Module>> modules;
    std::vector<std::unique_ptr<std::atomic<bool>>> enabled;
    std::atomic<uint64_t> packedOrder { 0 };
};

} // namespace sonic
