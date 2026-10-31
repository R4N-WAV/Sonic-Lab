#pragma once
// The "bridge": a directed acyclic graph of processing blocks between Input and
// Output.
//
//  - Any block may feed several blocks (fan-out) and receive several blocks
//    (fan-in = sum). Output sums everything connected to it.
//  - Every block has Mix (dry/wet against its own input, latency-aligned),
//    Output gain and Enable.
//  - Latency compensation: every path into a summing point is delayed so all
//    arrive aligned. Recomputed every block from the modules' current latency.
//  - Structure edits happen on the message thread. A new "compiled" execution
//    plan is built and swapped in lock-free; the old one is freed on the message
//    thread once the audio thread has provably stopped using it.

#include "sonic/Module.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sonic
{

class Graph
{
public:
    static constexpr int kInputId = 0;
    static constexpr int kOutputId = 1;
    static constexpr int kMaxDelay = 1 << 15; // samples of alignment delay per connection

    struct Node
    {
        int id = -1;
        std::string type;
        std::string name;
        std::unique_ptr<Module> module;
        std::atomic<float> mix { 1.0f };        // 0..1
        std::atomic<float> outGainDb { 0.0f };
        std::atomic<bool> enabled { true };
        float x = 0.0f, y = 0.0f;               // position in the bridge editor
    };

    struct Edge
    {
        int from = 0, to = 0;
        bool operator== (const Edge& o) const { return from == o.from && to == o.to; }
    };

    Graph();
    ~Graph();

    // ---- structure (message thread) ----
    // Creates a block of the given type. Returns its id, or -1 for an unknown type.
    int addNode (const std::string& type, float x = 0.0f, float y = 0.0f, int forcedId = -1);
    bool removeNode (int id);
    bool connect (int from, int to);      // false if it would create a cycle / is invalid
    bool disconnect (int from, int to);
    bool wouldCreateCycle (int from, int to) const;
    void clear();                         // removes all blocks and connections
    void resetToDefault();                // empty bridge with Input -> Output

    Node* node (int id);
    const Node* node (int id) const;
    std::vector<int> nodeIds() const;     // excluding Input/Output
    std::vector<Edge> edges() const;
    uint64_t structureVersion() const { return structVersion.load(); }

    // Topological order of the blocks that are actually in the signal path.
    std::vector<int> activeOrder() const;

    // ---- lifecycle ----
    void prepare (double sampleRate, int maxBlockSize); // not real-time safe
    double sampleRate() const { return fs; }
    void collectGarbage();                               // message thread, periodically

    // ---- audio thread ----
    void setHostInfo (const HostInfo& info) { host = info; }
    // Processes stereo audio in place (numChannels 1 or 2).
    void process (float* const* io, int numChannels, int numSamples);
    int latencySamples() const { return latency.load (std::memory_order_relaxed); }

private:
    struct Compiled;
    void commit();
    void retire (Compiled* c);
    void processChunk (Compiled& c, float* l, float* r, int n);

    std::map<int, std::shared_ptr<Node>> nodes;
    std::vector<Edge> edgeList;
    int nextId = 2;

    double fs = 48000.0;
    int maxBlock = 512;
    HostInfo host;

    std::atomic<Compiled*> current { nullptr };
    std::atomic<Compiled*> inUse { nullptr };
    std::vector<Compiled*> graveyard;
    std::atomic<int> latency { 0 };
    std::atomic<uint64_t> structVersion { 0 };
    mutable std::recursive_mutex structureLock; // message-thread side only
};

} // namespace sonic
